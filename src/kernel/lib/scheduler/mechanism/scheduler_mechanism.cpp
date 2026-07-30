#include <arch.hpp>
#include <containers/kmin_heap.hpp>
#include <exclusive/kspinlock_irqsave.hpp>
#include <kassert/kassert.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <scheduler/policy/scheduler_policy.hpp>

#include <cerrno>
#include <cstdint>

namespace scheduler::mechanism {

static policy::SchedulerPolicy* g_scheduler_policy;

static kspinlock_irqsave g_processes_lock;
static klist<process::Process*> g_processes;
static kmin_heap<std::uint64_t, process::Process*> g_sleepers;

/// @brief switch execution from one process to another
///
/// @param old_rsp_ptr pointer to the rsp of the previous process
/// @param new_rsp the rsp of the new process
///
/// @note this function is defined in context_switch.s
///
extern "C" void context_switch(std::uint64_t* old_rsp_ptr, std::uint64_t new_rsp);

/// @brief permanently switch execution from on process to another
///
/// @param new_rsp the rsp of the new process
///
/// @note unlike context_switch, from the callers perspective, this function
/// will never return, because we will never context switch back to
/// a process that called permanent_context_switch
///
extern "C" [[noreturn]]
void permanent_context_switch(std::uintptr_t new_rsp);

/// @brief wakes the first processes that is blocked for wait_reason
///
/// @param wait_reason the reason to wake the process
///
void wake_single(process::WaitReason reason)
{
    g_processes_lock.lock();

    if (reason == process::WaitReason::SLEEP) {
        log::warn("sleeping processes should not be explicitly woken");
        goto cleanup;
    }

    for (std::size_t i = 0; i < g_processes.size(); i++) {
        process::Process* p = g_processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for(reason)) {
            continue;
        }

        p->wake();
        g_scheduler_policy->enqueue(p);

        goto cleanup;
    }

cleanup:
    g_processes_lock.unlock();
}

/// @brief wakes every process that is blocked for wait_reason
///
/// @param wait_reason the reason to wake the processes
///
void wake_all(process::WaitReason reason)
{
    g_processes_lock.lock();

    if (reason == process::WaitReason::SLEEP) {
        log::warn("sleeping processes should not be explicitly woken");
        goto cleanup;
    }

    for (std::size_t i = 0; i < g_processes.size(); i++) {
        process::Process* p = g_processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for(reason)) {
            continue;
        }

        p->wake();
        g_scheduler_policy->enqueue(p);
    }

cleanup:
    g_processes_lock.unlock();
}

static void wake_parents(int pid)
{
    for (std::size_t i = 0; i < g_processes.size(); i++) {
        process::Process* p = g_processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for_child(pid)) {
            continue;
        }

        p->wake();
        g_scheduler_policy->enqueue(p);
    }
}

/// @brief wake all sleeping processes that have a wake_time_ticks in the past
///
void wake_sleepers()
{
    const std::uint64_t now_ticks = arch::drivers::tsc::raw_ticks();

    while (!g_sleepers.empty()) {
        process::Process* p = g_sleepers.peak();

        if (now_ticks < p->wake_time_ticks) {
            break;
        }

        p->wake();
        g_scheduler_policy->enqueue(p);
        g_sleepers.pop();
    }
}

process::Process* get_next_sleeper()
{
    if (g_scheduler_policy == nullptr) {
        return nullptr;
    }

    if (g_sleepers.empty()) {
        return nullptr;
    }

    return g_sleepers.peak();
}

static process::Process* find_child(process::Process* parent, int pid)
{
    process::Process* first_match = nullptr;

    for (std::size_t i = 0; i < g_processes.size(); i++) {
        process::Process* p = g_processes[i];

        if (pid != -1 && p->pid != pid) {
            continue;
        }

        if (p->parent == nullptr || p->parent->pid != parent->pid) {
            continue;
        }

        if (p->is_zombie()) {
            return p;
        }

        if (first_match == nullptr) {
            first_match = p;
        }
    }

    return first_match;
}

/// @brief activate a process on the current cpu
///
/// @param p the process to activate
///
static void activate_process(process::Process* p)
{
    auto* cpu = arch::percpu::get();

    kassert_not_null(cpu);
    kassert_not_null(p);

    p->resume();
    p->context_switches++;

    cpu->process = p;
    cpu->kernel_rsp = p->kernel_rsp;

    arch::vmm::switch_pml4(p->pml4);
    arch::tls::set_fs_base(p->fs_base);
    arch::gdt::set_kernel_stack(p->kernel_rsp);
}

/// @brief re-enqueue a process that gave up the CPU but is still runnable
///
/// The idle process is never enqueued: it isn't a real schedulable process,
/// just mechanism's fallback when the policy has nothing ready.
///
static void enqueue_if_runnable(process::Process* p)
{
    if (p == arch::percpu::idle_process()) {
        return;
    }

    if (!p->is_ready()) {
        return;
    }

    g_scheduler_policy->enqueue(p);
}

/// @brief Terminates DEAD processes
///
static void reap()
{

    g_processes_lock.lock();

    process::Process* self = arch::percpu::current_process();

    for (std::size_t i = 0; i < g_processes.size(); i++) {
        process::Process* p = g_processes[i];

        kassert_not_null(p);

        if (!p->is_dead()) {
            continue;
        }

        // the reaper_kthread should never attempt to terminate itself,
        // even if it gets marked DEAD for some reason
        kassert(p != self, "reaper_kthread tried to kill itself");

        delete p;

        g_processes.erase(i--);
    }

    g_processes_lock.unlock();
}

/// @brief Periodically terminate DEAD processes
///
/// @return this function should never return
///
[[noreturn]]
static void reaper_kthread()
{
    static constexpr std::uint64_t REAP_INTERVAL_MS = 100;

    while (true) {
        reap();

        yield_sleep_ms(REAP_INTERVAL_MS);
    }

    // The reaper kthread should never finish, its responsible for terminating DEAD
    // processes, so if it stopped running, DEAD processes would continue to
    // accumulate, wasting resources
    kpanic("reaper_kthread terminated");
}

/// @brief wake any due sleepers, then ask the policy which process runs next
///
static process::Process* pick_next_process()
{
    wake_sleepers();

    process::Process* next = g_scheduler_policy->pick_next();

    if (next == nullptr) {
        return arch::percpu::idle_process();
    }

    return next;
}

/// @brief interrupt the current process to schedule a new one
///
static void preempt()
{
    // Certain sensitive kernel actions, including this preempt() function itself,
    // must disable process preemption to safely perform their actions, so there is
    // nothing to do if preemption has been disabled
    if (!arch::percpu::preemption_enabled()) {
        return;
    }

    // ********************************
    // **** Begin Mutual Exclusion ****
    // ********************************
    //
    // The following section must be performed within a mutually exclusive
    // spinlock that disables both CPU interrupts and preemption
    //
    // We require mutual exclusion because we are directly manipulating the
    // global list of kernel processes (g_processes) and per CPU data fields
    // including the PML4, FS register, and TSS.RSP0. We do not want any anyone
    // else to manipulate these while we are working with them.

    g_processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    process::Process* next = pick_next_process();

    // We never want a process to context switch to itself, so we can
    // just leave early if a process wants to switch to itself, after
    // releasing our spinlock of course
    if (current == next) {
        g_processes_lock.unlock();
        return;
    }

    current->pause();
    enqueue_if_runnable(current);
    activate_process(next);
    g_processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, next->kernel_rsp_saved);
}

/// @brief mark the current process as DEAD and schedule a new one
///
/// @return this function should never return
///
void yield_dead()
{
    arch::cpu::cli();
    g_processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->kill();
    process::Process* p = pick_next_process();

    kassert(current != p);
    activate_process(p);
    g_processes_lock.unlock();

    permanent_context_switch(p->kernel_rsp_saved);

    // A DEAD process should never be the target of a context_switch from another
    // process, because now that this process is marked as DEAD, the reaper_kthread
    // will pick it and terminate it completely, freeing all of the memory it used,
    // so there would be nothing to context_switch back to anyway
    kpanic("context switch back to dead process");
}

/// mark the current process as ZOMBIE, wake all parents that are
/// waiting on this pid, and schedule a new process
///
/// @return this function should never return
///
void yield_zombie()
{
    arch::cpu::cli();
    g_processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->zombify();
    wake_parents(current->pid);
    process::Process* p = pick_next_process();

    kassert(current != p);
    activate_process(p);
    g_processes_lock.unlock();

    permanent_context_switch(p->kernel_rsp_saved);

    // A ZOMBIE process should never be the target of context_switch
    kpanic("context switch back to zombie process");
}

/// blocks the current process until child_pid exits
///
/// @param child_pid the child pid
///
/// @return the exit status of the child
///
int yield_to_child(int child_pid)
{
    // loop forever until a zombie child is found, this is by design, if a
    // parent calls wait() and its child never exits, the parent will never run again
    while (true) {
        g_processes_lock.lock();

        process::Process* parent = arch::percpu::current_process();
        process::Process* child = find_child(parent, child_pid);

        // return early if a parent calls wake() but has no children to wait on
        if (child == nullptr) {
            log::warn("yield_to_child() found no children");
            g_processes_lock.unlock();
            return -ECHILD;
        }

        // once we find a zombie child we no longer need to block anymore,
        // mark the child as dead so that it can be freed and resume the parent
        if (child->is_zombie()) {
            const int exit_status = child->exit_status;

            child->kill();
            parent->resume();
            g_processes_lock.unlock();

            return exit_status;
        }

        parent->wait_for_child(child_pid);

        process::Process* p = pick_next_process();

        activate_process(p);
        g_processes_lock.unlock();
        context_switch(&parent->kernel_rsp_saved, p->kernel_rsp_saved);
    }
}

/// @brief put the current process to sleep based on a desired frequency
///
void yield_sleep_hz(std::uint64_t sleep_hz)
{
    kassert(sleep_hz > 0);

    const std::uint64_t sleep_time_ms = 1000 / sleep_hz;

    yield_sleep_ms(sleep_time_ms);
}

/// @brief put the current process to sleep
///
/// @param sleep_time_ms time in ms to sleep for
///
void yield_sleep_ms(std::uint64_t sleep_time_ms)
{
    const std::uint64_t sleep_time_us = sleep_time_ms * 1000;
    const std::uint64_t sleep_time_ns = sleep_time_us * 1000;

    yield_sleep_ns(sleep_time_ns);
}

void yield_sleep_us(std::uint64_t sleep_time_us)
{
    const std::uint64_t sleep_time_ns = sleep_time_us * 1000;

    yield_sleep_ns(sleep_time_ns);
}

void yield_sleep_ns(std::uint64_t sleep_time_ns)
{
    process::Process* current = arch::percpu::current_process();
    current->sleep_until(arch::drivers::tsc::ticks_from_now(sleep_time_ns));
    yield_blocked(process::WaitReason::SLEEP);
}

/// @brief block the current process and schedule a new one
///
/// @param wait_reason the reason the process is blocked
///
void yield_blocked(process::WaitReason reason)
{
    g_processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->wait_for(reason);

    if (reason == process::WaitReason::SLEEP) {
        g_sleepers.insert(current->wake_time_ticks, current);
    }

    process::Process* next = pick_next_process();

    // pick_next_process() wakes all sleeping processes that are past
    // their wake time, which could include this very process that is
    // trying to yield itself while sleeping. We do not want to context
    // switch a process to itself, so simply set its state back to RUNNING and carry on
    if (current == next) {
        current->resume();
        g_processes_lock.unlock();
        return;
    }

    activate_process(next);
    g_processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, next->kernel_rsp_saved);
}

/// @brief yields the current process that was just created
///
/// when a new process is created (for example after userspace calls exec())
/// the process that created the new process gets replaced and no longer
/// exists, so there is no longer anything to return back to
///
/// instead, the current process needs to be scheduled like any other
/// process so that we can context_switch() to it for the first time
///
/// @note this function must never return
void yield_new_process()
{
    g_processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    process::Process* next = pick_next_process();

    kassert(current != next);

    current->wake();
    enqueue_if_runnable(current);
    activate_process(next);
    g_processes_lock.unlock();

    permanent_context_switch(next->kernel_rsp_saved);

    kpanic("yield_new_process should not return");
}

/// @brief add a new process to the scheduler
///
/// @param p the process
///
void add_process(process::Process* p)
{
    kassert_not_null(p);

    g_processes_lock.lock();
    g_processes.push_back(p);
    g_scheduler_policy->enqueue(p);
    g_processes_lock.unlock();
}

void tick()
{
    if (g_scheduler_policy == nullptr) {
        return;
    }

    wake_sleepers();
    preempt();
}

void init(policy::SchedulerPolicy* scheduler_policy)
{
    kassert_not_null(scheduler_policy);

    g_scheduler_policy = scheduler_policy;
    g_scheduler_policy->init();

    add_process(new process::KThread(reaper_kthread));
}

}

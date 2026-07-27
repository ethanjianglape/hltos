#include <arch.hpp>
#include <kassert/kassert.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/scheduler.hpp>

#include <cerrno>
#include <cstdint>

namespace scheduler {

static Scheduler* g_scheduler;

Scheduler* get_scheduler()
{
    kassert_not_null(g_scheduler);

    return g_scheduler;
}

/// @brief switch execution from one process to another
///
/// @param old_rsp_ptr pointer to the rsp of the previous process
/// @param new_rsp the rsp of the new process
///
/// @note this function is defined in context_switch.s
///
extern "C" void context_switch(std::uint64_t* old_rsp_ptr, std::uint64_t new_rsp);

/// @brief wakes the first processes that is blocked for wait_reason
///
/// @param wait_reason the reason to wake the process
///
void Scheduler::wake_single(process::WaitReason reason)
{
    _processes_lock.lock();

    for (std::size_t i = 0; i < _processes.size(); i++) {
        process::Process* p = _processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for(reason)) {
            continue;
        }

        p->wake();

        goto cleanup;
    }

cleanup:
    _processes_lock.unlock();
}

/// @brief wakes every process that is blocked for wait_reason
///
/// @param wait_reason the reason to wake the processes
///
void Scheduler::wake_all(process::WaitReason reason)
{
    _processes_lock.lock();

    for (std::size_t i = 0; i < _processes.size(); i++) {
        process::Process* p = _processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for(reason)) {
            continue;
        }

        p->wake();
    }

    _processes_lock.unlock();
}

void Scheduler::wake_parents(int pid)
{
    for (std::size_t i = 0; i < _processes.size(); i++) {
        process::Process* p = _processes[i];

        kassert_not_null(p);

        if (!p->is_blocked()) {
            continue;
        }

        if (!p->is_waiting_for_child(pid)) {
            continue;
        }

        p->wake();
    }
}

/// @brief wake all sleeping processes that have a wake_time_ticks in the past
///
void Scheduler::wake_sleepers()
{
    const std::uint64_t now_ticks = arch::drivers::tsc::raw_ticks();

    while (!_sleepers.empty()) {
        process::Process* p = _sleepers.peak();

        if (now_ticks < p->wake_time_ticks) {
            break;
        }

        p->wake();
        _sleepers.pop();
    }
}

process::Process* Scheduler::get_next_sleeper() const
{
    if (_sleepers.empty()) {
        return nullptr;
    }

    return _sleepers.peak();
}

/// @brief activate a process on the current cpu
///
/// @param p the process to activate
///
void Scheduler::activate_process(process::Process* p)
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

/// @brief Periodically terminate DEAD processes
///
/// @return this function should never return
///
[[noreturn]]
static void reaper_kthread()
{
    while (true) {
        g_scheduler->reap();
    }

    // The reaper kthread should never finish, its responsible for terminating DEAD
    // processes, so if it stopped running, DEAD processes would continue to
    // accumulate, wasting resources
    kpanic("reaper_kthread terminated");
}

void Scheduler::reap()
{
    _processes_lock.lock();

    process::Process* self = arch::percpu::current_process();

    for (std::size_t i = 0; i < _processes.size(); i++) {
        process::Process* p = _processes[i];

        kassert_not_null(p);

        if (!p->is_dead()) {
            continue;
        }

        // the reaper_kthread should never attempt to terminate itself,
        // even if it gets marked DEAD for some reason
        if (p == self) {
            kpanic("reaper_kthread wants to kill itself");
        }

        delete p;

        _processes.erase(i--);
    }

    _processes_lock.unlock();

    yield_sleep_ms(REAP_INTERVAL_MS);
}

/// @brief interrupt the current process to schedule a new one
///
void Scheduler::preempt()
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

    _processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    process::Process* next = next_ready_process();

    // We never want a process to context switch to itself, so we can
    // just leave early if a process wants to switch to itself, after
    // releasing our spinlock of course
    if (current == next) {
        _processes_lock.unlock();
        return;
    }

    current->pause();
    activate_process(next);
    _processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, next->kernel_rsp_saved);
}

/// @brief mark the current process as DEAD and schedule a new one
///
/// @return this function should never return
///
void Scheduler::yield_dead()
{
    arch::cpu::cli();
    _processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->kill();
    process::Process* p = next_ready_process();

    kassert(current != p);
    activate_process(p);
    _processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, p->kernel_rsp_saved);

    // A DEAD process should never be the target of a context_switch from another
    // process, because now that this process is marked as DEAD, the reaper_kthread
    // will pick it and terminate it completely, freeing all of the memory it used,
    // so there would be nothing to context_switch back to anyway
    kpanic("Context switch back to dead process");
}

/// mark the current process as ZOMBIE, wake all parents that are
/// waiting on this pid, and schedule a new process
///
/// @return this function should never return
///
void Scheduler::yield_zombie()
{
    arch::cpu::cli();
    _processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->zombify();
    wake_parents(current->pid);
    process::Process* p = next_ready_process();

    kassert(current != p);
    activate_process(p);
    _processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, p->kernel_rsp_saved);

    // A ZOMBIE process should never be the target of context_switch
    kpanic("Context switch back to zombie process");
}

/// blocks the current process until child_pid exits
///
/// @param child_pid the child pid
///
/// @return the exit status of the child
///
int Scheduler::yield_to_child(int child_pid)
{
    // loop forever until a zombie child is found, this is by design, if a
    // parent calls wait() and its child never exits, the parent will never run again
    while (true) {
        _processes_lock.lock();

        process::Process* parent = arch::percpu::current_process();
        process::Process* child = find_child(parent, child_pid);

        // return early if a parent calls wake() but has no children to wait on
        if (child == nullptr) {
            log::warn("yield_to_child() found no children");
            _processes_lock.unlock();
            return -ECHILD;
        }

        // once we find a zombie child we no longer need to block anymore,
        // mark the child as dead so that it can be freed and resume the parent
        if (child->is_zombie()) {
            const int exit_status = child->exit_status;

            child->kill();
            parent->resume();
            _processes_lock.unlock();

            return exit_status;
        }

        parent->wait_for_child(child_pid);

        process::Process* p = next_ready_process();

        activate_process(p);
        _processes_lock.unlock();
        context_switch(&parent->kernel_rsp_saved, p->kernel_rsp_saved);
    }
}

/// @brief put the current process to sleep
///
/// @param sleep_time_ms time in ms to sleep for
///
void Scheduler::yield_sleep_ms(std::uint64_t sleep_time_ms)
{
    const std::uint64_t sleep_time_us = sleep_time_ms * 1000;
    const std::uint64_t sleep_time_ns = sleep_time_us * 1000;

    yield_sleep_ns(sleep_time_ns);
}

void Scheduler::yield_sleep_us(std::uint64_t sleep_time_us)
{
    const std::uint64_t sleep_time_ns = sleep_time_us * 1000;

    yield_sleep_ns(sleep_time_ns);
}

void Scheduler::yield_sleep_ns(std::uint64_t sleep_time_ns)
{
    process::Process* current = arch::percpu::current_process();
    current->sleep_until(arch::drivers::tsc::ticks_from_now(sleep_time_ns));
    yield_blocked(process::WaitReason::SLEEP);
}

/// @brief block the current process and schedule a new one
///
/// @param wait_reason the reason the process is blocked
///
void Scheduler::yield_blocked(process::WaitReason reason)
{
    _processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    current->wait_for(reason);

    if (reason == process::WaitReason::SLEEP) {
        _sleepers.insert(current->wake_time_ticks, current);
    }

    process::Process* next = next_ready_process();

    // find_next_ready_process() wakes all sleeping processes that are past
    // their wake time, which could include this very process that is
    // trying to yield itself while sleeping. We do not want to context
    // switch a process to itself, so simply set its state back to RUNNING and carry on
    if (current == next) {
        current->resume();
        _processes_lock.unlock();
        return;
    }

    activate_process(next);
    _processes_lock.unlock();
    context_switch(&current->kernel_rsp_saved, next->kernel_rsp_saved);
}

void Scheduler::yield_new_process()
{
    _processes_lock.lock();

    process::Process* current = arch::percpu::current_process();
    process::Process* next = next_ready_process();

    kassert(current != next);

    current->wake();
    activate_process(next);
    _processes_lock.unlock();

    std::uintptr_t throwaway;

    context_switch(&throwaway, next->kernel_rsp_saved);

    kpanic("yield_new_process should not return");
}

/// @brief add a new process to the scheduler
///
/// @param p the process
///
void RoundRobinScheduler::add_process(process::Process* p)
{
    kassert_not_null(p);

    _processes_lock.lock();
    _processes.push_back(p);
    _processes_lock.unlock();
}

void tick()
{
    if (g_scheduler == nullptr) {
        return;
    }

    g_scheduler->wake_sleepers();
    g_scheduler->preempt();
}

void init()
{
    log::info("scheduler: Round Robin scheduler initialized");

    g_scheduler = new RoundRobinScheduler{};
    g_scheduler->add_process(new process::KThread(reaper_kthread));
}

}

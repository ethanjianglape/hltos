#pragma once

#include <containers/klist.hpp>
#include <containers/kmin_heap.hpp>
#include <exclusive/kspinlock_irqsave.hpp>
#include <process/process.hpp>

#include <cstdint>

namespace scheduler {

class Scheduler {
private:
    static constexpr std::uint64_t REAP_INTERVAL_MS = 100;

protected:
    kspinlock_irqsave _processes_lock;

    klist<process::Process*> _processes;
    kmin_heap<std::uint64_t, process::Process*> _sleepers;

public:
    Scheduler() = default;
    virtual ~Scheduler() = default;

    Scheduler(Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;

    Scheduler& operator=(Scheduler&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    process::Process* get_next_sleeper() const;

    virtual process::Process* next_ready_process() = 0;
    virtual process::Process* find_child(process::Process* parent, int pid) = 0;

    virtual void add_process(process::Process* p) = 0;

    void wake_single(process::WaitReason reason);
    void wake_all(process::WaitReason reason);
    void wake_parents(int pid);
    void wake_sleepers();

    void activate_process(process::Process* p);
    void preempt();
    void reap();

    [[noreturn]]
    void yield_dead();

    [[noreturn]]
    void yield_zombie();

    [[noreturn]]
    void yield_new_process();

    void yield_sleep_ms(std::uint64_t sleep_time_ms);
    void yield_sleep_us(std::uint64_t sleep_time_us);
    void yield_sleep_ns(std::uint64_t sleep_time_ns);
    void yield_blocked(process::WaitReason reason);

    int yield_to_child(int child_pid);
};

class RoundRobinScheduler final : public Scheduler {
private:
protected:
public:
    RoundRobinScheduler() = default;
    virtual ~RoundRobinScheduler() = default;

    process::Process* next_ready_process() override;
    process::Process* find_child(process::Process* parent, int pid) override;

    void add_process(process::Process* p) override;
};

Scheduler* get_scheduler();

void init();

void tick();

}

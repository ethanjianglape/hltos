#pragma once

#include <containers/klist.hpp>
#include <process/process.hpp>

#include <cstdint>

namespace scheduler {

/// @brief scheduling policy: decides which process runs next
///
/// Everything else (process bookkeeping, wake/sleep, yielding, context
/// switching) is mechanism shared by every policy and lives as free
/// functions in scheduler.cpp.
///
class Scheduler {
public:
    Scheduler() = default;
    virtual ~Scheduler() = default;

    Scheduler(Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;

    Scheduler& operator=(Scheduler&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    virtual process::Process* next_ready_process(klist<process::Process*>& processes) = 0;
};

class RoundRobinScheduler final : public Scheduler {
public:
    process::Process* next_ready_process(klist<process::Process*>& processes) override;
};

void add_process(process::Process* p);

void wake_single(process::WaitReason reason);
void wake_all(process::WaitReason reason);
void wake_sleepers();

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

process::Process* get_next_sleeper();

void init();

void tick();

}

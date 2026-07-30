#pragma once

#include <process/process.hpp>
#include <scheduler/policy/scheduler_policy.hpp>

#include <cstdint>

namespace scheduler::mechanism {

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

void yield_sleep_hz(std::uint64_t sleep_hz);
void yield_sleep_ms(std::uint64_t sleep_time_ms);
void yield_sleep_us(std::uint64_t sleep_time_us);
void yield_sleep_ns(std::uint64_t sleep_time_ns);
void yield_blocked(process::WaitReason reason);

int yield_to_child(int child_pid);

process::Process* get_next_sleeper();

void init(policy::SchedulerPolicy* scheduler_policy);
void tick();

}

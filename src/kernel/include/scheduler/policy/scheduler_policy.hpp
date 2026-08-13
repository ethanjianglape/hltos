#pragma once

#include <containers/klist.hpp>
#include <process/process.hpp>

#include <cstdint>

namespace scheduler::policy {

class SchedulerPolicy {
public:
    SchedulerPolicy() = default;
    virtual ~SchedulerPolicy() = default;

    SchedulerPolicy(SchedulerPolicy&) = delete;
    SchedulerPolicy(SchedulerPolicy&&) = delete;

    SchedulerPolicy& operator=(SchedulerPolicy&) = delete;
    SchedulerPolicy& operator=(SchedulerPolicy&&) = delete;

    /// @brief initialize this scheduler
    virtual void init() = 0;

    /// @brief mark a process as ready to run
    virtual void enqueue(process::Process* p) = 0;

    /// @brief determine if a process should be preempted
    virtual bool should_preempt(process::Process* p, std::uint64_t runtime_ns) = 0;

    /// @brief choose the next ready process to run, or nullptr if nothing is ready
    virtual process::Process* pick_next() = 0;
};

class RoundRobinScheduler final : public SchedulerPolicy {
private:
    klist<process::Process*> _ready;

    static constexpr std::uint64_t TIME_SLICE_MS = 10;
    static constexpr std::uint64_t TIME_SLICE_US = TIME_SLICE_MS * 1000;
    static constexpr std::uint64_t TIME_SLICE_NS = TIME_SLICE_US * 1000;

public:
    void init() override;
    void enqueue(process::Process* p) override;
    bool should_preempt(process::Process* p, std::uint64_t runtime_ns) override;
    process::Process* pick_next() override;
};

}

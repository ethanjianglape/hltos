#pragma once

#include <containers/klist.hpp>
#include <process/process.hpp>

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

    /// @brief choose and remove the next ready process to run, or nullptr
    /// if nothing is ready (mechanism falls back to the idle process)
    virtual process::Process* pick_next() = 0;
};

class RoundRobinScheduler final : public SchedulerPolicy {
private:
    klist<process::Process*> _ready;

public:
    void init() override;
    void enqueue(process::Process* p) override;
    process::Process* pick_next() override;
};

}

#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <scheduler/policy/scheduler_policy.hpp>

namespace scheduler::policy {

void RoundRobinScheduler::init()
{
    log::info("scheduler: round robin scheduler initialized");
}

void RoundRobinScheduler::enqueue(process::Process* p)
{
    kassert_not_null(p);
    _ready.push_back(p);
}

bool RoundRobinScheduler::should_preempt(process::Process* p, std::uint64_t runtime_ns)
{
    kassert_not_null(p);

    return runtime_ns >= TIME_SLICE_NS;
}

/// @brief pop the front of the ready queue
///
/// @return pointer to the next ready process, or nullptr if the queue is empty
///
process::Process* RoundRobinScheduler::pick_next()
{
    if (_ready.empty()) {
        return nullptr;
    }

    process::Process* p = _ready.front();
    _ready.pop_front();

    return p;
}

}

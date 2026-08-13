#include <arch.hpp>
#include <exclusive/kmutex.hpp>
#include <log/log.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>

void kmutex::lock()
{
    while (true) {
        if (_lock.exchange(0) == 1) {
            return;
        }

        scheduler::mechanism::yield_mutex(this);
    }
}

void kmutex::unlock()
{
    _lock.store(1);

    scheduler::mechanism::wake_mutex(this);
}

#include "scheduler/scheduler.hpp"
#include <arch.hpp>
#include <syscall/sys_sleep.hpp>

namespace syscall {

int sys_sleep_ms(std::uint64_t ms)
{
    scheduler::yield_sleep_ms(ms);

    return 0;
}

}

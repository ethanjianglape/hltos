#include <arch.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <syscall/sys_sleep.hpp>

namespace syscall {

int sys_sleep_ms(std::uint64_t ms)
{
    scheduler::mechanism::yield_sleep_ms(ms);

    return 0;
}

}

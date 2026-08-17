#include "log/log.hpp"
#include <arch.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <syscall/sys_sleep.hpp>

namespace syscall {

int sys_sleep_ns(std::uint64_t ns)
{
    log::debugf("user process sleep for {}ns", ns);

    scheduler::mechanism::yield_sleep_ns(ns);

    return 0;
}

}

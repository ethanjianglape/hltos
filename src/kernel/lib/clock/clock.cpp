#include <arch.hpp>
#include <clock/clock.hpp>

#include <cstdint>

namespace clock {

std::uint64_t get_time_ms()
{
    return arch::drivers::tsc::get_time_ms();
}

std::uint64_t get_time_us()
{
    return arch::drivers::tsc::get_time_us();
}

std::uint64_t get_time_ns()
{
    return arch::drivers::tsc::get_time_ns();
}

}

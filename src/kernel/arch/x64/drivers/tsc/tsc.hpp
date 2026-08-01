#pragma once

#include <cstdint>

namespace x64::drivers::tsc {

void init();

inline std::uint64_t rdtsc();

std::uint64_t get_ticks();
std::uint64_t get_time_ns();
std::uint64_t get_time_us();
std::uint64_t get_time_ms();
std::uint64_t get_tsc_freq();

std::uint64_t raw_ticks();
std::uint64_t ns_to_ticks(std::uint64_t ns);
std::uint64_t ns_to_raw_ticks(std::uint64_t ns);
std::uint64_t ticks_from_now(std::uint64_t ns);

void sleep_ms(std::uint64_t time_ms);
void sleep_us(std::uint64_t time_us);

}

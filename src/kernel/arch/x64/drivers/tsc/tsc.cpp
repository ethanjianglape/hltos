#include "tsc.hpp"

#include <arch/x64/cpu/cpu.hpp>
#include <arch/x64/drivers/pit/pit.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>

#include <cstdint>

namespace x64::drivers::tsc {

static std::uint64_t boot_tsc = 0;
static std::uint64_t tsc_freq = 0;

[[gnu::always_inline]]
static inline std::uint64_t rdtsc()
{
    std::uint64_t eax;
    std::uint64_t edx;

    asm volatile("rdtsc" : "=a"(eax), "=d"(edx) : :);

    std::uint64_t tsc = (edx << 32) | eax;

    return tsc;
}

[[gnu::always_inline]]
static inline std::uint64_t rdtscp()
{
    std::uint64_t eax;
    std::uint64_t ecx;
    std::uint64_t edx;

    asm volatile("rdtscp" : "=a"(eax), "=c"(ecx), "=d"(edx) : :);

    std::uint64_t tsc = (edx << 32) | eax;

    return tsc;
}

[[gnu::always_inline]]
static inline std::uint64_t micro_bench_start()
{
    cpu::lfence();
    return rdtsc();
}

[[gnu::always_inline]]
static inline std::uint64_t micro_bench_end()
{
    return rdtscp();
}

std::uint64_t get_ticks()
{
    return rdtsc() - boot_tsc;
}

std::uint64_t get_time_ns()
{
    const std::uint64_t delta = get_ticks();
    const std::uint64_t secs = delta / tsc_freq;
    const std::uint64_t remainder_cycles = delta % tsc_freq;

    return secs * 1000000000ULL + (remainder_cycles * 1000000000ULL) / tsc_freq;
}

std::uint64_t get_time_us()
{
    return get_time_ns() / 1000ULL;
}

std::uint64_t get_time_ms()
{
    return get_time_ns() / 1000000ULL;
}

std::uint64_t get_tsc_freq()
{
    return tsc_freq;
}

// Raw TSC value, with no boot offset applied. This is the domain
// IA32_TSC_DEADLINE (and the TSC register itself) operates in, so anything
// that arms hardware or compares against it - the scheduler's sleep
// deadlines, the APIC timer - should be measured in this, not get_ticks().
std::uint64_t raw_ticks()
{
    return rdtsc();
}

std::uint64_t ns_to_ticks(std::uint64_t ns)
{
    const std::uint64_t secs = ns / 1000000000ULL;
    const std::uint64_t remainder_ns = ns % 1000000000ULL;

    return secs * tsc_freq + (remainder_ns * tsc_freq) / 1000000000ULL;
}

std::uint64_t ns_to_raw_ticks(std::uint64_t ns)
{
    return ns_to_ticks(ns) + boot_tsc;
}

std::uint64_t ticks_from_now(std::uint64_t ns)
{
    return raw_ticks() + ns_to_ticks(ns);
}

// Invariant TSC support is determined by CPUID.0x80000007.EDX[8]
static bool check_invariant_tsc_support()
{
    constexpr std::uint32_t CPUID_FEAT_EDX_INVARIANT_TSC = (1 << 8);

    std::uint32_t eax;
    std::uint32_t edx;

    cpu::cpuid(0x80000007, &eax, &edx);

    return (edx & CPUID_FEAT_EDX_INVARIANT_TSC) != 0;
}

void sleep_ms(std::uint64_t time_ms)
{
    const std::uint64_t target = get_time_ms() + time_ms;

    while (get_time_ms() < target) {
        cpu::pause();
    }
}

void sleep_us(std::uint64_t time_us)
{
    const std::uint64_t target = get_time_us() + time_us;

    while (get_time_us() < target) {
        cpu::pause();
    }
}

void init()
{
    if (!check_invariant_tsc_support()) {
        kpanic("Invariant TSC not available");
    }

    boot_tsc = micro_bench_start();

    const std::uint64_t t0 = boot_tsc;
    constexpr std::uint32_t sleep_ms = 20;
    constexpr std::uint32_t total_ms = 100;

    // sleep for a total of 100ms (5 x 20ms)
    // note: the PIT physically cannot sleep for longer than ~54ms at a time
    for (std::uint32_t i = 0; i < total_ms / sleep_ms; i++) {
        x64::drivers::pit::sleep_ms(sleep_ms);
    }

    const std::uint64_t t1 = micro_bench_end();

    tsc_freq = ((t1 - t0) * 1000) / total_ms;

    log::infof("TSC: frequency = {}hz ({}Mhz) (calibrated by PIT)", tsc_freq, tsc_freq / 1000000);
}

}

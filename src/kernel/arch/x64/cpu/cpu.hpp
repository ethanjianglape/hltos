#pragma once

#include <cpuid.h>
#include <cstdint>

// Forward declaration to avoid circular include
namespace x64::irq {
struct InterruptFrame;
}

namespace x64::cpu {

void early_init();
void late_init();

// Dump current CPU state (control regs, segment regs, flags)
void dump();

// Dump CPU state from an interrupt context (includes all GPRs, RIP, etc.)
void dump(const irq::InterruptFrame* frame);

[[gnu::always_inline]]
inline void outb(const std::uint16_t port, const std::uint8_t value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

[[gnu::always_inline]]
inline std::uint8_t inb(const std::uint16_t port)
{
    std::uint8_t ret;

    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");

    return ret;
}

[[gnu::always_inline]]
inline void outw(const std::uint16_t port, const std::uint16_t value)
{
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

[[gnu::always_inline]]
inline std::uint16_t inw(const std::uint16_t port)
{
    std::uint16_t ret;

    asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");

    return ret;
}

[[gnu::always_inline]]
inline void outl(const std::uint16_t port, const std::uint32_t value)
{
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port) : "memory");
}

[[gnu::always_inline]]
inline std::uint32_t inl(const std::uint16_t port)
{
    std::uint32_t ret;

    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");

    return ret;
}

[[gnu::always_inline]]
inline void cpuid(std::uint32_t code, std::uint32_t* a)
{
    asm volatile("cpuid" : "=a"(*a) : "a"(code) : "ebx", "ecx", "memory");
}

[[gnu::always_inline]]
inline void cpuid(std::uint32_t code, std::uint32_t* a, std::uint32_t* d)
{
    asm volatile("cpuid" : "=a"(*a), "=d"(*d) : "a"(code) : "ebx", "ecx", "memory");
}

[[gnu::always_inline]]
inline void cpuid(std::uint32_t code, std::uint32_t* eax, std::uint32_t* ebx, std::uint32_t* ecx)
{
    asm volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx) : "a"(code) : "edx", "memory");
}

[[gnu::always_inline]]
inline void cpuid(std::uint32_t code, std::uint32_t* eax, std::uint32_t* ebx, std::uint32_t* ecx, std::uint32_t* edx)
{
    asm volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(code) : "memory");
}

[[gnu::always_inline]]
inline std::uint64_t rdmsr(std::uint32_t msr)
{
    std::uint32_t low;
    std::uint32_t high;

    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");

    return ((uint64_t)high << 32) | low;
}

[[gnu::always_inline]]
inline void wrmsr(uint32_t msr, uint64_t value)
{
    const std::uint32_t low = value & 0xFFFFFFFF;
    const std::uint32_t high = value >> 32;

    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr) : "memory");
}

[[gnu::always_inline]]
inline void hlt()
{
    asm volatile("hlt");
}

[[gnu::always_inline]]
inline void pause()
{
    asm volatile("pause");
}

[[gnu::always_inline]]
inline void sti()
{
    asm volatile("sti" : : : "memory");
}

[[gnu::always_inline]]
inline void cli()
{
    asm volatile("cli" : : : "memory");
}

[[gnu::always_inline]]
inline void stac()
{
    asm volatile("stac" : : : "memory");
}

[[gnu::always_inline]]
inline void clac()
{
    asm volatile("clac" : : : "memory");
}

[[gnu::always_inline]]
inline std::uint64_t read_rflags()
{
    std::uint64_t rflags;
    asm volatile("pushfq; pop %0" : "=r"(rflags) : : "memory");
    return rflags;
}

[[gnu::always_inline]]
inline void write_rflags(std::uint64_t rflags)
{
    asm volatile("push %%rax; popfq" : : "a"(rflags) : "memory", "cc");
}

[[gnu::always_inline]]
inline std::uint64_t read_cr4()
{
    std::uint64_t cr4;
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

[[gnu::always_inline]]
inline void write_cr4(std::uint64_t cr4)
{
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
}

[[gnu::always_inline]]
inline void lfence()
{
    asm volatile("lfence");
}

[[gnu::always_inline]]
inline void mfence()
{
    asm volatile("mfence");
}

}

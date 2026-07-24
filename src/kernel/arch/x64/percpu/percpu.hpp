#pragma once

#include <cstdint>

namespace process {
struct Process;
}

namespace x64::percpu {
constexpr std::uint32_t MSR_GS_BASE = 0xC0000101;        // Active GS base
constexpr std::uint32_t MSR_KERNEL_GS_BASE = 0xC0000102; // Swapped by SWAPGS

/**
 * Per-CPU data structure. Each CPU core has one of these, accessed via GS.
 *
 * The field order matters! Assembly code (syscall_entry.s) accesses these
 * by hardcoded offsets. If you reorder fields, update the assembly too.
 *
 * Layout:
 *   offset 0x00: self        — Pointer to this struct (for C++ access)
 *   offset 0x08: kernel_rsp  — Kernel stack pointer for syscall entry
 *   offset 0x10: user_rsp    — Saved user stack pointer during syscall
 *   offset 0x18: process     — Currently running process on this CPU
 */
struct [[gnu::packed]] PerCPU {
    PerCPU* self;              // See percpu.cpp for explanation
    std::uint64_t kernel_rsp;  // Where to switch RSP on syscall entry
    std::uint64_t user_rsp;    // User's RSP saved here during syscall
    process::Process* process; // Current process running on this CPU
    process::Process* idle_process;
    bool preemption_enabled;
};

void early_init();
void init();

bool preemption_enabled();
void disable_preemption();
void enable_preemption();

PerCPU* get();

process::Process* idle_process();
process::Process* current_process();

}

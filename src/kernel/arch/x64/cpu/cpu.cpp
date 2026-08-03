/**
 * CPU Utility Functions
 * =====================
 *
 * This file contains CPU-related functions that are too complex for the
 * header-only inline functions in cpu.hpp (e.g., functions that need logging).
 */

#include "cpu.hpp"

#include <arch/x64/interrupts/irq.hpp>
#include <cstdint>
#include <fmt/fmt.hpp>
#include <log/log.hpp>

namespace x64::cpu {

// =========================================================================
// Register Reading Helpers
// =========================================================================

static inline std::uint64_t read_cr0()
{
    std::uint64_t value;
    asm volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline std::uint64_t read_cr2()
{
    std::uint64_t value;
    asm volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

static inline std::uint64_t read_cr3()
{
    std::uint64_t value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(std::uint64_t cr0)
{
    asm volatile("mov %0, %%cr0" : : "r"(cr0));
}

static inline std::uint16_t read_cs()
{
    std::uint16_t value;
    asm volatile("mov %%cs, %0" : "=r"(value));
    return value;
}

static inline std::uint16_t read_ds()
{
    std::uint16_t value;
    asm volatile("mov %%ds, %0" : "=r"(value));
    return value;
}

static inline std::uint16_t read_es()
{
    std::uint16_t value;
    asm volatile("mov %%es, %0" : "=r"(value));
    return value;
}

static inline std::uint16_t read_fs()
{
    std::uint16_t value;
    asm volatile("mov %%fs, %0" : "=r"(value));
    return value;
}

static inline std::uint16_t read_gs()
{
    std::uint16_t value;
    asm volatile("mov %%gs, %0" : "=r"(value));
    return value;
}

static inline std::uint16_t read_ss()
{
    std::uint16_t value;
    asm volatile("mov %%ss, %0" : "=r"(value));
    return value;
}

// =========================================================================
// RFLAGS Decoder
// =========================================================================

static void log_rflags(std::uint64_t rflags)
{
    log::debug("RFLAGS: ", fmt::hex{rflags}, " [",
        (rflags & (1 << 0)) ? "CF " : "",  // Carry
        (rflags & (1 << 2)) ? "PF " : "",  // Parity
        (rflags & (1 << 4)) ? "AF " : "",  // Auxiliary
        (rflags & (1 << 6)) ? "ZF " : "",  // Zero
        (rflags & (1 << 7)) ? "SF " : "",  // Sign
        (rflags & (1 << 8)) ? "TF " : "",  // Trap
        (rflags & (1 << 9)) ? "IF " : "",  // Interrupt Enable
        (rflags & (1 << 10)) ? "DF " : "", // Direction
        (rflags & (1 << 11)) ? "OF " : "", // Overflow
        "]");
}

// =========================================================================
// CR0 Decoder
// =========================================================================

static void log_cr0(std::uint64_t cr0)
{
    log::debug("CR0: ", fmt::hex{cr0}, " [",
        (cr0 & (1 << 0)) ? "PE " : "",    // Protected Mode Enable
        (cr0 & (1 << 1)) ? "MP " : "",    // Monitor Coprocessor
        (cr0 & (1 << 2)) ? "EM " : "",    // Emulation
        (cr0 & (1 << 3)) ? "TS " : "",    // Task Switched
        (cr0 & (1 << 4)) ? "ET " : "",    // Extension Type
        (cr0 & (1 << 5)) ? "NE " : "",    // Numeric Error
        (cr0 & (1 << 16)) ? "WP " : "",   // Write Protect
        (cr0 & (1 << 18)) ? "AM " : "",   // Alignment Mask
        (cr0 & (1UL << 29)) ? "NW " : "", // Not Write-through
        (cr0 & (1UL << 30)) ? "CD " : "", // Cache Disable
        (cr0 & (1UL << 31)) ? "PG " : "", // Paging
        "]");
}

static void enable_sse()
{
    std::uint64_t cr0 = read_cr0();
    std::uint64_t cr4 = read_cr4();

    cr0 &= ~(1UL << 2);
    cr0 |= (1 << 1);
    cr4 |= (1 << 9) | (1 << 10);

    write_cr4(cr4);
    write_cr0(cr0);
}

// Vendor ID string ("AuthenticAMD") is returned across EBX:EDX:ECX by CPUID.0
static bool is_amd()
{
    constexpr std::uint32_t AMD_MAGIC_EBX = 0x68747541; // "Auth"
    constexpr std::uint32_t AMD_MAGIC_EDX = 0x69746e65; // "enti"
    constexpr std::uint32_t AMD_MAGIC_ECX = 0x444d4163; // "cAMD"

    std::uint32_t eax;
    std::uint32_t ebx;
    std::uint32_t ecx;
    std::uint32_t edx;

    cpuid(0, &eax, &ebx, &ecx, &edx);

    return ebx == AMD_MAGIC_EBX && edx == AMD_MAGIC_EDX && ecx == AMD_MAGIC_ECX;
}

/// @brief sets bit 1 of MSR 0xC001_1029 (DE_CFG)
///
/// On AMD, LFENCE is only guaranteed dispatch-serializing (i.e. usable as a
/// speculation barrier / to order RDTSC) if DE_CFG bit 1 is set.
///
/// Intel guarantees this unconditionally; AMD requires the OS to opt in.
///
/// Every mainstream OS sets this at boot as part of the Spectre v1 mitigation, so we
/// do the same rather than relying on firmware having done it for us.
///
/// @see https://en.wikipedia.org/wiki/Spectre_(security_vulnerability)
static void enable_amd_lfence_serializing()
{
    constexpr std::uint32_t MSR_AMD_DE_CFG = 0xC0011029;
    constexpr std::uint64_t DE_CFG_LFENCE_SERIALIZE = (1ULL << 1);

    const std::uint64_t de_cfg = rdmsr(MSR_AMD_DE_CFG);
    wrmsr(MSR_AMD_DE_CFG, de_cfg | DE_CFG_LFENCE_SERIALIZE);

    log::info("CPU: AMD detected, enabled LFENCE dispatch-serialization (Spectre v1 mitigation, DE_CFG bit 1)");
}

void early_init()
{
    cli();
    enable_sse();
}

void late_init()
{
    if (is_amd()) {
        enable_amd_lfence_serializing();
    }
}

// =========================================================================
// dump() - Dump Current CPU State
// =========================================================================
//
// Logs all readable CPU registers to the debug log. Useful for debugging.
// Note: General purpose registers shown here are the values AT THE TIME
// OF THE DUMP, not from any particular context (and will include values
// used by the dump function itself).

void dump()
{
    // Read all GP registers first, before we clobber them with function calls
    std::uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    std::uint64_t r8, r9, r10, r11, r12, r13, r14, r15;

    asm volatile(
        "mov %%rax, %0\n"
        "mov %%rbx, %1\n"
        "mov %%rcx, %2\n"
        "mov %%rdx, %3\n"
        "mov %%rsi, %4\n"
        "mov %%rdi, %5\n"
        "mov %%rbp, %6\n"
        "mov %%rsp, %7\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx),
        "=m"(rsi), "=m"(rdi), "=m"(rbp), "=m"(rsp));

    asm volatile(
        "mov %%r8,  %0\n"
        "mov %%r9,  %1\n"
        "mov %%r10, %2\n"
        "mov %%r11, %3\n"
        "mov %%r12, %4\n"
        "mov %%r13, %5\n"
        "mov %%r14, %6\n"
        "mov %%r15, %7\n"
        : "=m"(r8), "=m"(r9), "=m"(r10), "=m"(r11),
        "=m"(r12), "=m"(r13), "=m"(r14), "=m"(r15));

    log::debug("========== CPU Register Dump ==========");

    // General purpose registers
    log::debug("General Purpose Registers:");
    log::debug("RAX: ", fmt::hex{rax}, "  RBX: ", fmt::hex{rbx});
    log::debug("RCX: ", fmt::hex{rcx}, "  RDX: ", fmt::hex{rdx});
    log::debug("RSI: ", fmt::hex{rsi}, "  RDI: ", fmt::hex{rdi});
    log::debug("RBP: ", fmt::hex{rbp}, "  RSP: ", fmt::hex{rsp});
    log::debug("R8:  ", fmt::hex{r8}, "  R9:  ", fmt::hex{r9});
    log::debug("R10: ", fmt::hex{r10}, "  R11: ", fmt::hex{r11});
    log::debug("R12: ", fmt::hex{r12}, "  R13: ", fmt::hex{r13});
    log::debug("R14: ", fmt::hex{r14}, "  R15: ", fmt::hex{r15});

    // Control registers
    log::debug("Control Registers:");
    log_cr0(read_cr0());
    log::debug("CR2: ", fmt::hex{read_cr2()}, " (last page fault address)");
    log::debug("CR3: ", fmt::hex{read_cr3()}, " (page table base)");
    log::debug("CR4: ", fmt::hex{read_cr4()});

    // Segment registers
    log::debug("Segment Registers:");
    log::debug("CS: ", fmt::hex{read_cs()});
    log::debug("DS: ", fmt::hex{read_ds()});
    log::debug("ES: ", fmt::hex{read_es()});
    log::debug("SS: ", fmt::hex{read_ss()});
    log::debug("FS: ", fmt::hex{read_fs()});
    log::debug("GS: ", fmt::hex{read_gs()});

    // Flags
    log_rflags(read_rflags());

    log::debug("======== End CPU Register Dump ========");
}

// =========================================================================
// dump(InterruptFrame*) - Dump Registers from Interrupt Context
// =========================================================================
//
// Logs the CPU state captured during an interrupt. This is more useful than
// dump() because it shows the state at the moment the interrupt occurred,
// not the current state (which would be inside the dump function itself).

void dump(const irq::InterruptFrame* frame)
{
    log::debug("========== CPU Register Dump (Interrupt Context) ==========");

    // Instruction pointer and stack
    log::debug("RIP: ", fmt::hex{frame->rip});
    log::debug("RSP: ", fmt::hex{frame->rsp});
    log::debug("RBP: ", fmt::hex{frame->rbp});

    // General purpose registers
    log::debug("RAX: ", fmt::hex{frame->rax}, "  RBX: ", fmt::hex{frame->rbx});
    log::debug("RCX: ", fmt::hex{frame->rcx}, "  RDX: ", fmt::hex{frame->rdx});
    log::debug("RSI: ", fmt::hex{frame->rsi}, "  RDI: ", fmt::hex{frame->rdi});
    log::debug("R8:  ", fmt::hex{frame->r8}, "  R9:  ", fmt::hex{frame->r9});
    log::debug("R10: ", fmt::hex{frame->r10}, "  R11: ", fmt::hex{frame->r11});
    log::debug("R12: ", fmt::hex{frame->r12}, "  R13: ", fmt::hex{frame->r13});
    log::debug("R14: ", fmt::hex{frame->r14}, "  R15: ", fmt::hex{frame->r15});

    // Segment registers from interrupt frame
    log::debug("CS:  ", fmt::hex{frame->cs}, "  SS:  ", fmt::hex{frame->ss});

    // Flags
    log_rflags(frame->rflags);

    // Control registers (current values, not from frame)
    log_cr0(read_cr0());
    log::debug("CR2: ", fmt::hex{read_cr2()}, " (last page fault address)");
    log::debug("CR3: ", fmt::hex{read_cr3()}, " (page table base)");

    // Interrupt info
    log::debug("Vector: ", frame->vector, "  Error Code: ", fmt::hex{frame->err});
}

}

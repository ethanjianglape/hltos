/**
 * @file percpu.cpp
 * @brief Per-CPU data structures using the GS segment register.
 *
 * What is Per-CPU Data?
 *
 *   In a multi-core system, each CPU needs its own private copy of certain
 *   data: the current process, the kernel stack pointer, scratch space, etc.
 *   We can't use a global variable because all CPUs would see the same value.
 *
 *   The solution is per-CPU data: each CPU has its own PerCPU struct, and we
 *   use a CPU feature to access "my" struct without knowing which CPU we are.
 *
 * How Does GS Work?
 *
 *   In 64-bit long mode, most segment registers (CS, DS, SS, ES) are neutered
 *   — they exist for privilege levels but don't provide base addresses. The
 *   exceptions are FS and GS, which still add a base address to memory ops.
 *
 *   The GS base is stored in an MSR (Model Specific Register), not in the GDT.
 *   When you write `mov %gs:0x10, %rax`, the CPU computes: GS_BASE + 0x10.
 *
 *   Each CPU has its own MSRs, so each CPU can have a different GS_BASE
 *   pointing to its own PerCPU struct. Accessing `%gs:offset` automatically
 *   gives you YOUR CPU's data, with no "which CPU am I?" checks needed.
 *
 * The Two GS MSRs:
 *
 *   x86-64 provides TWO GS base MSRs for kernel/user separation:
 *
 *     MSR_GS_BASE (0xC0000101)        — The "active" GS base used by %gs:
 *     MSR_KERNEL_GS_BASE (0xC0000102) — A holding area, swapped by SWAPGS
 *
 *   The SWAPGS instruction atomically swaps these two values. This lets us:
 *     - Keep kernel per-CPU pointer in one MSR
 *     - Keep user's GS value in the other MSR
 *     - Swap instantly on kernel entry/exit
 *
 * Syscall Entry Flow:
 *
 *   1. User is running (GS_BASE = user value, KERNEL_GS_BASE = &PerCPU)
 *   2. User executes SYSCALL instruction
 *   3. syscall_entry immediately does SWAPGS (now GS_BASE = &PerCPU)
 *   4. syscall_entry reads kernel_rsp from %gs:8 to get a safe stack
 *   5. ... handle syscall ...
 *   6. Before SYSRET, do SWAPGS again (restore user's GS)
 *   7. Return to user
 *
 *   This is why we set GS_BASE = &PerCPU at boot: we start in kernel mode.
 *   When we first enter userspace, we SWAPGS (user gets GS_BASE, kernel
 *   value moves to KERNEL_GS_BASE). From then on, SWAPGS toggles correctly.
 *
 * Why the "self" Pointer?
 *
 *   Assembly can easily do `mov %gs:0x08, %rax` to get kernel_rsp, but C++
 *   can't emit GS-relative memory accesses directly. We could use inline asm
 *   for every field access, but that's tedious.
 *
 *   Instead, we store the struct's own address at offset 0. The get() function
 *   uses one inline asm to read %gs:0, getting a normal pointer. From there,
 *   C++ can access fields normally: `get()->process`, `get()->kernel_rsp`, etc.
 */

#include "percpu.hpp"
#include "kassert/kassert.hpp"
#include "kpanic/kpanic.hpp"

#include <arch/x64/cpu/cpu.hpp>
#include <cstdint>
#include <fmt/fmt.hpp>
#include <log/log.hpp>
#include <process/process.hpp>

namespace x64::percpu {

/**
 * @brief Initializes per-CPU data for the bootstrap processor.
 *
 * Allocates a PerCPU struct and sets GS_BASE to point to it. At boot we're
 * in kernel mode, so GS_BASE gets the kernel value. KERNEL_GS_BASE starts
 * as 0 (unused until first SWAPGS when entering userspace).
 */

void early_init()
{
    static PerCPU per;

    per.self = &per;
    per.kernel_rsp = 0;
    per.user_rsp = 0;
    per.process = nullptr;
    per.idle_process = nullptr;
    per.preemption_enabled = false;

    cpu::wrmsr(MSR_GS_BASE, reinterpret_cast<std::uintptr_t>(&per));
    cpu::wrmsr(MSR_KERNEL_GS_BASE, 0);
}

[[noreturn]]
static void idle_process_kthread()
{
    while (true) {
        cpu::sti();
        cpu::hlt();
    }

    kpanic("kernel idle thread finished");
}

void init()
{
    auto* per_cpu_data = new PerCPU{};

    // Set GS_BASE to our per-CPU struct. We're in kernel mode at boot.
    cpu::wrmsr(MSR_GS_BASE, reinterpret_cast<std::uintptr_t>(per_cpu_data));

    // KERNEL_GS_BASE is the "other" slot for SWAPGS. Starts unused.
    cpu::wrmsr(MSR_KERNEL_GS_BASE, 0);

    per_cpu_data->self = per_cpu_data; // For C++ access via get()
    per_cpu_data->kernel_rsp = 0;      // Set by scheduler before running process
    per_cpu_data->user_rsp = 0;        // Saved by syscall_entry
    per_cpu_data->idle_process = new process::KThread(idle_process_kthread);
    per_cpu_data->process = per_cpu_data->idle_process;
    per_cpu_data->preemption_enabled = true;

    log::info("percpu: GS_BASE = ", fmt::hex{per_cpu_data});
}

/**
 * @brief Returns a pointer to the current CPU's PerCPU struct.
 *
 * Uses inline assembly to read the self pointer from %gs:0. This is the
 * only place we need inline asm — after this, normal C++ pointer access
 * works for all fields.
 */
PerCPU* get()
{
    constexpr std::size_t PER_CPU_SELF_OFFSET = offsetof(x64::percpu::PerCPU, self);
    PerCPU* ptr;

    // Read the self pointer from offset 0 of GS-relative memory
    asm volatile("mov %%gs:%c1, %0" : "=r"(ptr) : "i"(PER_CPU_SELF_OFFSET) : "memory");

    kassert_not_null(ptr);

    return ptr;
}

bool preemption_enabled()
{
    PerCPU* percpu = get();

    return percpu->preemption_enabled;
}

void disable_preemption()
{
    PerCPU* percpu = get();

    percpu->preemption_enabled = false;
}

void enable_preemption()
{
    PerCPU* percpu = get();

    percpu->preemption_enabled = true;
}

process::Process* idle_process()
{
    auto* cpu = get();
    auto* p = cpu->idle_process;

    kassert_not_null(p);

    return p;
}

process::Process* current_process()
{
    auto* cpu = get();
    auto* p = cpu->process;

    kassert_not_null(p);

    return p;
}

}

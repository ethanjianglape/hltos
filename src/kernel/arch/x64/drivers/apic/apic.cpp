/**
 * @file apic.cpp
 * @brief Advanced Programmable Interrupt Controller (APIC) driver.
 *
 * @note This file is unusually heavily commented. Unlike most OS concepts
 * (memory management, scheduling, etc.) which follow general design principles,
 * the APIC is pure x86 hardware magic. There's no way to intuit that "enabling
 * the APIC requires setting bit 11 of MSR 0x1B" - you either read the Intel
 * manual or you don't know. The excessive comments here serve as a reference
 * so you don't have to constantly look up the manual for every register bit.
 *
 * TL;DR: The APIC routes hardware interrupts (keyboard, timer, etc.) to the CPU.
 *        We enable it, configure which interrupt goes to which vector, set up a
 *        periodic timer for scheduling, and call send_eoi() after each interrupt.
 *
 * What is the APIC?
 *
 *   The APIC is the modern interrupt controller for x86 systems, replacing the
 *   legacy 8259 PIC (which we disable in pic.cpp). It actually consists of two
 *   distinct components that work together:
 *
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                              System Overview                            │
 *   └─────────────────────────────────────────────────────────────────────────┘
 *
 *   ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
 *   │   Keyboard   │     │    Timer     │     │   Network    │
 *   │   (IRQ 1)    │     │   (IRQ 0)    │     │   (IRQ 11)   │
 *   └──────┬───────┘     └──────┬───────┘     └──────┬───────┘
 *          │                    │                    │
 *          ▼                    ▼                    ▼
 *   ┌─────────────────────────────────────────────────────────────────────────┐
 *   │                          I/O APIC                                       │
 *   │                  (Usually at 0xFEC00000)                                │
 *   │                                                                         │
 *   │  Routes external device interrupts to the appropriate CPU core.         │
 *   │  Contains a Redirection Table: IRQ → which CPU + which vector           │
 *   └───────────────────────────────┬─────────────────────────────────────────┘
 *                                   │
 *                                   │ (Interrupt messages via system bus)
 *                                   │
 *          ┌────────────────────────┼────────────────────────┐
 *          │                        │                        │
 *          ▼                        ▼                        ▼
 *   ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
 *   │ Local APIC  │          │ Local APIC  │          │ Local APIC  │
 *   │   (CPU 0)   │          │   (CPU 1)   │          │   (CPU 2)   │
 *   │             │          │             │          │             │
 *   │ @ 0xFEE00000│          │ @ 0xFEE00000│          │ @ 0xFEE00000│
 *   └──────┬──────┘          └──────┬──────┘          └──────┬──────┘
 *          │                        │                        │
 *          ▼                        ▼                        ▼
 *   ┌─────────────┐          ┌─────────────┐          ┌─────────────┐
 *   │   CPU 0     │          │   CPU 1     │          │   CPU 2     │
 *   └─────────────┘          └─────────────┘          └─────────────┘
 *
 * Local APIC (LAPIC):
 *
 *   Each CPU core has its own Local APIC, memory-mapped at 0xFEE00000 (same
 *   virtual address, but each core sees its own LAPIC). The LAPIC handles:
 *
 *   - Receiving interrupts from the I/O APIC
 *   - Local timer (we use this for scheduling)
 *   - Inter-Processor Interrupts (IPIs) for multi-core communication
 *   - Priority arbitration (which interrupt to handle first)
 *   - End-of-Interrupt signaling (EOI)
 *
 * I/O APIC:
 *
 *   The I/O APIC sits on the motherboard and routes external device interrupts
 *   to the appropriate CPU. It has a Redirection Table that maps each IRQ to:
 *
 *   - A destination CPU (or set of CPUs)
 *   - An interrupt vector number
 *   - Trigger mode (edge/level)
 *   - Polarity (active high/low)
 *   - Delivery mode (fixed, lowest priority, etc.)
 *
 * Why APIC instead of PIC?
 *
 *   1. Multi-core support: PIC can only signal one CPU; APIC routes to any core
 *   2. More interrupts: 224 vectors vs PIC's 15
 *   3. MSI support: Modern PCIe devices use Message Signaled Interrupts
 *   4. Better priority: 16 priority levels vs PIC's fixed priority
 *   5. Performance: No need to mask/unmask, just send EOI
 *
 * Timer Calibration:
 *
 *   The LAPIC timer runs at the CPU's bus frequency, which varies by system.
 *   We calibrate it by:
 *   1. Set a known count and start it
 *   2. Wait a known time (using PIT, which has a fixed frequency)
 *   3. Read how many ticks elapsed → now we know ticks/ms
 *   4. Configure periodic interrupts at our desired rate
 */

#include "apic.hpp"
#include "arch/x64/drivers/tsc/tsc.hpp"
#include "kassert/kassert.hpp"
#include "process/process.hpp"
#include <acpi/madt.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <timer/timer.hpp>

#include <arch/x64/cpu/cpu.hpp>
#include <arch/x64/interrupts/irq.hpp>
#include <arch/x64/memory/vmm.hpp>
#include <log/log.hpp>

#include <arch/x64/drivers/pit/pit.hpp>
#include <cstdint>
#include <kpanic/kpanic.hpp>

namespace x64::drivers::apic {

// Local APIC registers (offset from base address 0xFEE00000)
constexpr std::uint32_t LAPIC_ID = 0x0020;               // Local APIC ID
constexpr std::uint32_t LAPIC_VERSION = 0x0030;          // Local APIC Version
constexpr std::uint32_t LAPIC_TPR = 0x0080;              // Task Priority Register
constexpr std::uint32_t LAPIC_EOI = 0x00B0;              // End of Interrupt
constexpr std::uint32_t LAPIC_SPURIOUS = 0x00F0;         // Spurious Interrupt Vector Register
constexpr std::uint32_t LAPIC_ESR = 0x0280;              // Error Status Register
constexpr std::uint32_t LAPIC_ICR_LOW = 0x0300;          // Interrupt Command Register (low)
constexpr std::uint32_t LAPIC_ICR_HIGH = 0x0310;         // Interrupt Command Register (high)
constexpr std::uint32_t LAPIC_TIMER = 0x0320;            // LVT Timer Register
constexpr std::uint32_t LAPIC_TIMER_INIT_COUNT = 0x0380; // Timer Initial Count
constexpr std::uint32_t LAPIC_TIMER_CURRENT = 0x0390;    // Timer Current Count
constexpr std::uint32_t LAPIC_TIMER_DIVIDE = 0x03E0;     // Timer Divide Configuration
constexpr std::uint32_t APIC_LVT_INT_MASKED = 0x10000;

constexpr std::uint32_t TIMER_MODE_ONESHOT = 0;
constexpr std::uint32_t TIMER_MODE_PERIODIC = (1 << 17); // 0x20000;
constexpr std::uint32_t TIMER_MODE_TSC_DEADLINE = (1 << 18);
constexpr std::uint32_t TIMER_DIV_BY_16 = 0x3;

// LAPIC base address (standard location)
constexpr std::uint32_t LAPIC_BASE_ADDR = 0xFEE00000;

// Spurious interrupt vector register bits
constexpr std::uint32_t LAPIC_SPURIOUS_ENABLE = 0x100; // Bit 8: APIC Software Enable/Disable

// MSR for APIC base address
constexpr std::uint32_t MSR_APIC_BASE = 0x1B;
constexpr std::uint32_t MSR_APIC_BASE_ENABLE = 0x800; // Bit 11: Enable Local APIC

constexpr std::uint32_t IA32_TSC_DEADLINE = 0x6E0;

// CPUID feature flags
constexpr std::uint32_t CPUID_FEAT_EDX_APIC = (1 << 9); // APIC available

// I/O APIC registers (indirect access via IOREGSEL/IOWIN)
constexpr std::uintptr_t IOAPIC_IOREGSEL = 0x00; // Register select (write reg number here)
constexpr std::uintptr_t IOAPIC_IOWIN = 0x10;    // Data window (read/write reg value here)

// I/O APIC Redirection Table entries (each pin has 2 32-bit registers)
// IOREDTBL[n] = base + 2*n, where n is the pin number (0-23)
constexpr std::uint32_t IOAPIC_REDTBL_BASE = 0x10; // First redirection entry

static std::uint64_t interrupt_deadline;
static std::uint64_t interrupt_delta;

// =========================================================================
// LAPIC/IOAPIC Base Address Management
// =========================================================================
//
// Both the Local APIC and I/O APIC are memory-mapped devices. We access
// their registers by reading/writing to specific memory addresses.
//
// The physical addresses come from ACPI tables (parsed during boot):
//   - LAPIC: Usually 0xFEE00000 (but can vary)
//   - IOAPIC: Usually 0xFEC00000 (but can vary)
//
// We map these physical addresses to virtual addresses before use.

static volatile std::uint8_t* lapic_virt_base = nullptr;

volatile std::uint8_t* get_lapic_addr() { return lapic_virt_base; }

/**
 * @brief Sets the Local APIC base address by mapping the physical address.
 * @param lapic_phys_addr Physical address of the LAPIC (from ACPI MADT).
 */
void set_lapic_addr()
{
    std::uint64_t phys_addr = ::acpi::madt::get_lapic_addr();

    lapic_virt_base = reinterpret_cast<volatile std::uint8_t*>(vmm::map_phys(phys_addr, vmm::PAGE_WRITE | vmm::PAGE_CACHE_DISABLE));
}

// Helper to get the register offset for a redirection table entry
// Each entry is 64 bits = 2 registers (low at 0x10+2n, high at 0x11+2n)
constexpr std::uint32_t IOAPIC_REDTBL_LO(std::uint32_t pin) { return IOAPIC_REDTBL_BASE + pin * 2; }
constexpr std::uint32_t IOAPIC_REDTBL_HI(std::uint32_t pin) { return IOAPIC_REDTBL_BASE + pin * 2 + 1; }

// =========================================================================
// LAPIC Register Access
// =========================================================================
//
// The Local APIC uses simple memory-mapped I/O. Each register is at a fixed
// offset from the base address. We just read/write to those addresses.
//
// Important: Registers are 32-bit aligned on 16-byte boundaries. For example:
//   - ID register:      base + 0x020
//   - Version register: base + 0x030
//   - EOI register:     base + 0x0B0
//
// The 'volatile' keyword is critical - it tells the compiler these memory
// locations can change outside of program control (hardware changes them).

/**
 * @brief Reads a 32-bit value from a Local APIC register.
 * @param reg Register offset from the LAPIC base address.
 * @return The 32-bit value read from the register.
 */
[[gnu::unused]]
static inline std::uint32_t lapic_read(std::uint32_t reg)
{
    return *reinterpret_cast<volatile std::uint32_t*>(lapic_virt_base + reg);
}

/**
 * @brief Writes a 32-bit value to a Local APIC register.
 * @param reg Register offset from the LAPIC base address.
 * @param value The 32-bit value to write.
 */
static inline void lapic_write(std::uint32_t reg, std::uint32_t value)
{
    volatile auto* addr = reinterpret_cast<volatile std::uint32_t*>(lapic_virt_base + reg);

    *addr = value;
}

// =========================================================================
// IOAPIC Register Access
// =========================================================================
//
// The I/O APIC uses indirect register access (unlike the LAPIC's direct access).
// There are only two directly accessible registers:
//
//   IOREGSEL (offset 0x00): Write the register number you want to access
//   IOWIN    (offset 0x10): Read/write the value of the selected register
//
// To read register N:  write N to IOREGSEL, then read from IOWIN
// To write register N: write N to IOREGSEL, then write to IOWIN
//
// Why indirect access? The I/O APIC has many registers (24+ redirection
// entries × 2 registers each = 48+ registers), but only occupies a small
// memory region. Indirect access keeps the memory footprint small.

/**
 * @brief Reads a 32-bit value from an I/O APIC register using indirect access.
 * @param reg Register number to read from.
 * @return The 32-bit value read from the register.
 */
[[gnu::unused]]
static inline std::uint32_t ioapic_read(volatile std::uint8_t* base, std::uint32_t reg)
{
    kassert_not_null(base);

    *reinterpret_cast<volatile std::uint32_t*>(base + IOAPIC_IOREGSEL) = reg;
    return *reinterpret_cast<volatile std::uint32_t*>(base + IOAPIC_IOWIN);
}

/**
 * @brief Writes a 32-bit value to an I/O APIC register using indirect access.
 * @param reg Register number to write to.
 * @param value The 32-bit value to write.
 */
static inline void ioapic_write(volatile std::uint8_t* base, std::uint32_t reg, std::uint32_t value)
{
    kassert_not_null(base);

    log::debugf("IOAPIC: writing value {} to register {}", fmt::hex{value}, reg);

    *reinterpret_cast<volatile std::uint32_t*>(base + IOAPIC_IOREGSEL) = reg;
    *reinterpret_cast<volatile std::uint32_t*>(base + IOAPIC_IOWIN) = value;
}

/**
 * @brief Signals End of Interrupt (EOI) to the Local APIC.
 *
 * After handling an interrupt, we MUST signal "End of Interrupt" to the LAPIC.
 * This tells the LAPIC we're done and it can deliver the next pending interrupt.
 *
 * @warning If we forget to send EOI, the LAPIC thinks we're still handling the
 *          interrupt and won't deliver any more interrupts of equal or lower
 *          priority - the system will appear to hang.
 *
 * @note The value written doesn't matter (we use 0). The act of writing to the
 *       EOI register is what signals completion.
 */
void send_eoi()
{
    lapic_write(LAPIC_EOI, 0);
}

/**
 * @brief Checks if the CPU supports APIC via CPUID.
 *
 * Before using the APIC, we need to verify the CPU actually has one.
 * The CPUID instruction returns feature flags that tell us what the CPU supports.
 *
 * CPUID with EAX=1 returns feature flags in EDX. Bit 9 indicates APIC support:
 *   - EDX bit 9 = 1: APIC is present
 *   - EDX bit 9 = 0: No APIC (ancient CPU or disabled in BIOS)
 *
 * @note Virtually all x86-64 CPUs have an APIC, but good to check anyway.
 *
 * @return true if APIC is supported, false otherwise.
 */
bool check_support()
{
    std::uint32_t eax;
    std::uint32_t edx;

    cpu::cpuid(1, &eax, &edx);

    return (edx & CPUID_FEAT_EDX_APIC) != 0;
}

/**
 * @brief Enables the APIC globally via the IA32_APIC_BASE MSR.
 *
 * The APIC has a global enable/disable bit in the IA32_APIC_BASE MSR (0x1B).
 * MSR = Model Specific Register, accessed via RDMSR/WRMSR instructions.
 *
 * IA32_APIC_BASE register layout:
 *   - Bits 0-7:   Reserved
 *   - Bit 8:      BSP flag (1 = this is the bootstrap processor) - read only
 *   - Bit 9:      Reserved
 *   - Bit 10:     x2APIC enable (we don't use this, it's for newer APIC mode)
 *   - Bit 11:     APIC Global Enable - THIS IS WHAT WE SET
 *   - Bits 12-35: APIC Base Address (physical, 4KB aligned)
 *   - Bits 36-63: Reserved
 *
 * @note Even though the LAPIC might already be enabled by the BIOS, we set
 *       bit 11 explicitly to be sure. This doesn't change the base address.
 */
void enable_apic()
{
    std::uint64_t apic_msr = cpu::rdmsr(MSR_APIC_BASE);
    apic_msr |= MSR_APIC_BASE_ENABLE; // Set bit 11
    cpu::wrmsr(MSR_APIC_BASE, apic_msr);
}

/**
 * @brief Configures the Spurious Interrupt Vector Register (SVR).
 *
 * Sets the SVR to 0x1FF which:
 *   - Bit 8 (0x100): Software enable for the LAPIC
 *   - Bits 0-7 (0xFF): Spurious interrupt vector
 *
 * @see init() for details on why both MSR and SVR enables are needed.
 */
void configure_svr()
{
    lapic_write(LAPIC_SPURIOUS, 0x1FF);
}

/**
 * @brief Routes a legacy ISA IRQ to the specified interrupt vector via the IOAPIC.
 *
 * Looks up the correct IOAPIC and pin for the given IRQ using the MADT, applies
 * any Interrupt Source Override flags (polarity/trigger mode), and programs the
 * IOAPIC redirection table entry.
 *
 * Each redirection table entry is 64 bits with the following layout:
 *
 *   - Bits 0-7:   Interrupt Vector (which IDT entry to invoke, 0x10-0xFE)
 *   - Bits 8-10:  Delivery Mode:
 *                   - 000 = Fixed (deliver to CPUs listed in destination)
 *                   - 001 = Lowest Priority (deliver to lowest-priority CPU)
 *                   - 010 = SMI (System Management Interrupt)
 *                   - 100 = NMI
 *                   - 101 = INIT
 *                   - 111 = ExtINT (external interrupt, legacy mode)
 *   - Bit 11:     Destination Mode (0 = Physical, 1 = Logical)
 *   - Bit 12:     Delivery Status (read-only, 0 = idle, 1 = pending)
 *   - Bit 13:     Pin Polarity (0 = Active High, 1 = Active Low)
 *   - Bit 14:     Remote IRR (read-only, for level-triggered)
 *   - Bit 15:     Trigger Mode (0 = Edge, 1 = Level)
 *   - Bit 16:     Mask (0 = Enabled, 1 = Masked/Disabled)
 *   - Bits 17-55: Reserved
 *   - Bits 56-63: Destination (which CPU(s) to deliver to)
 *
 * Unless overridden by the MADT, we use defaults: Fixed delivery, Physical mode,
 * Active High, Edge triggered, Enabled, deliver to CPU 0.
 *
 * @param irq Legacy ISA IRQ number (0-15).
 * @param vector The interrupt vector number to deliver to the CPU.
 */
void ioapic_route_irq(std::uint8_t irq, std::uint8_t vector)
{
    const std::uint32_t gsi = acpi::madt::get_gsi_for_irq(irq);
    const acpi::madt::IOApic* ioapic = acpi::madt::get_ioapic_for_gsi(gsi);

    if (ioapic == nullptr) {
        kpanic("No IOAPIC found for GSI: ", gsi);
    }

    const acpi::madt::InterruptSourceOverride* iso = acpi::madt::get_override_for_irq(irq);
    volatile std::uint8_t* ioapic_addr = acpi::madt::get_mapped_ioapic_addr(ioapic);

    std::uint32_t pin = gsi - ioapic->gsi_base;
    std::uint64_t entry = vector;

    if (iso) {
        if ((iso->flags & 0x3) == 0x3) {
            entry |= (1 << 13); // Active Low
        }

        if ((iso->flags & 0xC) == 0xC) {
            entry |= (1 << 15); // Level Triggered
        }
    }

    ioapic_write(ioapic_addr, IOAPIC_REDTBL_LO(pin), static_cast<std::uint32_t>(entry));
    ioapic_write(ioapic_addr, IOAPIC_REDTBL_HI(pin), static_cast<std::uint32_t>(entry >> 32));
}

/// @brief Timer interrupt handler called every LAPIC timer tick.
///
/// Signals End of Interrupt, runs the generic per-tick handlers (assumed to
/// all return promptly within this same tick), then runs the scheduler's
/// preemption logic as a separate, explicit last step.
///
/// @param frame the stack frame generated by this interrupt
///
static void apic_timer_handler(irq::InterruptFrame* frame)
{
    // TSC deadline mode only fires a single time once the current value of the TSC
    // is >= interrupt_deadline, so to continue triggering interrupts, we need to
    // update interrupt_deadline every time we receive an interrupt
    interrupt_deadline += interrupt_delta;

    // If a sleeping process needs to wake before the next periodic tick would
    // otherwise fire, arm the timer for that deadline instead, so sleepers are
    // woken precisely rather than waiting for the next 1ms tick to notice them
    process::Process* next_sleeper = scheduler::mechanism::get_next_sleeper();

    if (next_sleeper) {
        const std::uint64_t sleeper_deadline = next_sleeper->wake_time_ticks;

        if (sleeper_deadline < interrupt_deadline) {
            interrupt_deadline = sleeper_deadline;
        }
    }

    cpu::wrmsr(IA32_TSC_DEADLINE, interrupt_deadline);

    // EOI must be sent to allow this LAPIC to fire another interrupt in the future, this
    // must be done before calling the scheduler because the scheduler may or may not return
    send_eoi();
    timer::tick(frame);
    scheduler::mechanism::tick();
}

/// @brief Calibrates and initializes the LAPIC timer for periodic interrupts.
///
/// The LAPIC has a built-in timer that can generate periodic interrupts.
/// This is essential for preemptive multitasking (the scheduler needs a
/// regular "tick" to switch between processes).
///
/// **The Problem:**
/// The LAPIC timer runs off the CPU's bus clock, which varies by system.
/// A 3 GHz CPU might have a 100 MHz bus clock, or 133 MHz, or something
/// else entirely. We can't just hardcode a timer value.
///
/// **The Solution:**
/// Utilize TSC deadline mode to calibrate the LAPIC timer. This causes the
/// LAPIC to fire of an interrupt as soon as the TSC reaches a
/// fixed value. The frequency of the TSC is already known at this point, so
/// we can calcuate the exact number of TSC ticks needed to produce a 1ms pulse.
///
/// see tsc.cpp for more information on the TSC
///
/// **Timer Registers:**
///   - LAPIC_TIMER (LVT): Mode and vector configuration
///   - IA32_TSC_DEADLINE: Sets the value the TSC must be at for the next interrupt to fire
///
/// **LVT Timer Register bits:**
///   - Bits 0-7:   Interrupt vector
///   - Bit 16:     Mask (1 = disabled)
///   - Bits 17-18: Timer mode:
///       - 00 = One-shot (fire once, stop)
///       - 01 = Periodic (auto-reload and repeat)
///       - 10 = TSC-Deadline (this is the one we will use)
///
static void timer_init()
{
    // configure the LAPIC to use IRQ 0 for interrupts and enter TSC deadline mode
    lapic_write(LAPIC_TIMER, irq::VECTOR_TIMER | TIMER_MODE_TSC_DEADLINE);
    irq::register_irq_handler(irq::VECTOR_TIMER, apic_timer_handler);

    // calculate the number of TSC ticks needed to achieve the desired interrupt frequency
    // based on the known frequency of the TSC (usually around 3-4Ghz)
    constexpr std::uint64_t interrupt_frequency_ms = 1;
    constexpr std::uint64_t interrupt_frequency_ns = interrupt_frequency_ms * 1000000;

    interrupt_delta = (tsc::get_tsc_freq() * interrupt_frequency_ns) / 1000000000;
    interrupt_deadline = interrupt_delta;

    // The TSC deadline value is written to an MSR, not the LAPIC itself
    cpu::wrmsr(IA32_TSC_DEADLINE, interrupt_deadline);

    log::infof("APIC: TSC deadline mode @ {}ms ({} ticks)", interrupt_frequency_ms, interrupt_delta);
}

/**
 * @brief Initializes the APIC subsystem (Local APIC and LAPIC timer).
 *
 * This function performs LAPIC initialization:
 *   1. Verifies APIC support via CPUID
 *   2. Enables the APIC globally via MSR
 *   3. Configures the Spurious Interrupt Vector Register (SVR)
 *   4. Clears the Task Priority Register (TPR) to accept all interrupts
 *   5. Initializes the LAPIC timer for scheduling
 *
 * Device drivers should call ioapic_route_irq() to set up their own
 * interrupt routing through the I/O APIC.
 *
 * @pre The MADT must be parsed before calling this function.
 * @pre The legacy PIC must be disabled (see pic.cpp).
 */
void init()
{
    if (!check_support()) {
        kpanic("APIC not supported - required for this kernel");
    }

    set_lapic_addr();

    kassert_not_null(lapic_virt_base);

    // Step 1: Enable the APIC globally via the MSR
    enable_apic();

    // Step 2: Configure the Spurious Interrupt Vector Register (SVR)
    //
    // The SVR serves two purposes:
    //   1. Bit 8: Software enable/disable for the LAPIC
    //      Even after setting MSR bit 11, the LAPIC won't work until we also
    //      set SVR bit 8. Think of MSR as "hardware enable" and SVR as
    //      "software enable" - both must be set.
    //
    //   2. Bits 0-7: Spurious interrupt vector
    //      Sometimes the APIC generates "spurious" interrupts - phantom
    //      interrupts that don't correspond to any real device. This happens
    //      due to race conditions in interrupt acknowledgment. We assign
    //      these to vector 0xFF so we can ignore them.
    //
    // 0x1FF = 0001 1111 1111 = bit 8 (enable) + vector 0xFF
    configure_svr();

    // Step 3: Clear the Task Priority Register (TPR)
    //
    // The TPR controls which interrupts the CPU will accept. It has a priority
    // value (0-15), and the CPU will only accept interrupts with priority
    // HIGHER than the TPR value.
    //
    // Priority is determined by the interrupt vector: priority = vector / 16
    //   - Vector 0x20 (32) = priority 2
    //   - Vector 0xFF (255) = priority 15
    //
    // By setting TPR to 0, we accept ALL interrupts (nothing is blocked).
    // An OS scheduler might temporarily raise TPR during critical sections.
    lapic_write(LAPIC_TPR, 0);

    // Step 4: Set up the LAPIC timer for periodic ticks
    timer_init();
}

}

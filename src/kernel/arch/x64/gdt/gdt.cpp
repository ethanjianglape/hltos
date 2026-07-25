/**
 * @file gdt.cpp
 * @brief Global Descriptor Table (GDT) and Task State Segment (TSS) initialization.
 *
 * What is the GDT?
 *   The GDT is a CPU data structure that defines memory segments. In legacy
 *   16/32-bit modes, segments provided memory protection via base addresses
 *   and limits. In 64-bit long mode, segmentation is essentially disabled -
 *   the CPU enforces a flat memory model and ignores base/limit fields.
 *
 *   However, the GDT is still REQUIRED for:
 *     - Privilege levels (Ring 0 kernel vs Ring 3 userspace)
 *     - Distinguishing code segments from data segments
 *     - The Task State Segment (TSS) for stack switching on ring transitions
 *
 * GDT Entry Layout (8 bytes):
 *
 *   In 64-bit mode, only the access byte and L/D flags matter for code/data
 *   segments. Base and limit are ignored (treated as 0 and max respectively).
 *
 *   63        56 55   52 51   48 47        40 39       32
 *   ┌──────────┬───────┬───────┬────────────┬───────────┐
 *   │ Base     │ Flags │ Limit │  Access    │   Base    │
 *   │ [31:24]  │ G L D │[19:16]│  Byte      │  [23:16]  │
 *   └──────────┴───────┴───────┴────────────┴───────────┘
 *   31                  16 15                           0
 *   ┌─────────────────────┬─────────────────────────────┐
 *   │   Base [15:0]       │       Limit [15:0]          │
 *   └─────────────────────┴─────────────────────────────┘
 *
 *   Access Byte (what actually matters in long mode):
 *   ┌───┬───────┬───┬───┬───┬───┬───┐
 *   │ P │  DPL  │ S │ E │DC │RW │ A │
 *   │   │ (2b)  │   │   │   │   │   │
 *   └───┴───────┴───┴───┴───┴───┴───┘
 *     7   6   5   4   3   2   1   0
 *
 *     P   = Present (must be 1)
 *     DPL = Privilege level (0 = kernel, 3 = user)
 *     S   = Descriptor type (1 = code/data, 0 = system like TSS)
 *     E   = Executable (1 = code, 0 = data)
 *     RW  = Readable (code) / Writable (data)
 *
 *   Flags (bits 52-55):
 *     G = Granularity (1 = limit in 4KB blocks)
 *     L = Long mode (1 = 64-bit code segment) <-- Critical for 64-bit!
 *     D = Default operand size (must be 0 when L=1)
 *
 * Our GDT Layout:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │  Selector   │  Entry        │  Ring  │  Purpose             │
 *   ├─────────────┼───────────────┼────────┼──────────────────────┤
 *   │  0x00       │  NULL         │  -     │  Required by CPU     │
 *   │  0x08       │  Kernel Code  │  0     │  Kernel execution    │
 *   │  0x10       │  Kernel Data  │  0     │  Kernel data access  │
 *   │  0x18       │  User Data    │  3     │  Userspace data      │
 *   │  0x20       │  User Code    │  3     │  Userspace execution │
 *   │  0x28       │  TSS          │  0     │  Stack switching     │
 *   └─────────────────────────────────────────────────────────────┘
 *
 *   Note: User Data (0x18) comes BEFORE User Code (0x20). This ordering
 *   is required by the SYSRET instruction, which expects:
 *     CS = IA32_STAR[63:48] + 16, SS = IA32_STAR[63:48] + 8
 *   With STAR configured for selector base 0x10, SYSRET sets SS=0x18, CS=0x20.
 *
 * Task State Segment (TSS):
 *
 *   The TSS is a special structure that holds stack pointers for privilege
 *   level transitions. When an interrupt occurs in Ring 3, the CPU needs
 *   to switch to a Ring 0 stack - it reads RSP0 from the TSS automatically.
 *
 *   TSS Descriptor is 16 bytes (spans 2 GDT slots) because it needs a
 *   full 64-bit base address to locate the TSS structure in memory.
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                     TSS Structure (104 bytes)               │
 *   ├─────────────────────────────────────────────────────────────┤
 *   │  reserved     (4 bytes)                                     │
 *   │  RSP0         (8 bytes)  <-- Kernel stack for Ring 3->0     │
 *   │  RSP1         (8 bytes)      (unused in modern OSes)        │
 *   │  RSP2         (8 bytes)      (unused in modern OSes)        │
 *   │  reserved     (8 bytes)                                     │
 *   │  IST1-IST7    (56 bytes)     Interrupt Stack Table          │
 *   │  reserved     (10 bytes)                                    │
 *   │  IOPB offset  (2 bytes)      I/O permission bitmap offset   │
 *   └─────────────────────────────────────────────────────────────┘
 *
 *   When a syscall or interrupt transitions from user to kernel mode,
 *   the CPU automatically loads RSP from TSS.RSP0, giving the kernel
 *   a safe stack to execute on.
 *
 * How Does This Actually Work?
 *
 *   The C++ code here just populates data structures in memory - it doesn't
 *   directly interact with the CPU's segmentation hardware. The "magic"
 *   happens via dedicated CPU instructions:
 *
 *     LGDT [gdtr]  - Load GDT Register: tells the CPU where our GDT lives
 *     LTR  ax      - Load Task Register: points the CPU to our TSS entry
 *
 *   These instructions are in gdt.s (assembly). Once executed, the CPU
 *   knows where to find segment descriptors and will automatically use
 *   them for privilege checks, stack switching, etc.
 *
 *   In other words: we fill out the paperwork (GDT/TSS structures), then
 *   hand it to the CPU (LGDT/LTR), and the CPU handles the rest. There's
 *   no runtime "GDT code" - it's purely a hardware feature we configure.
 *
 *   This is a one-time setup during boot. Once LGDT/LTR are called, we never
 *   call them again - the GDT and TSS descriptor stay fixed. The one exception
 *   is TSS.RSP0: some OSes update this field on context switches so each
 *   process/thread gets its own kernel stack, but that's just a memory write,
 *   not re-running LTR.
 */

#include "gdt.hpp"

#include <fmt/fmt.hpp>
#include <log/log.hpp>

extern "C" void load_gdt(x64::gdt::Gdtr* ptr);

extern "C" void load_tss();

namespace x64::gdt {

static GdtTable gdt_table;
static Gdtr gdtr;
static TssEntry tss;

// When transitioning from user to kernel code, this is the stack
// that the TSS will point to in TSS.RSP0. For now it is just a static
// 16KiB array.
alignas(4096) static std::uint8_t kernel_stack[4096 * 4];

/**
 * @brief Constructs a GDT entry from its component fields.
 * @param base Base address (ignored in 64-bit mode for code/data segments).
 * @param limit Segment limit (ignored in 64-bit mode for code/data segments).
 * @param access Access byte defining segment type and privilege level.
 * @param flags Flags including granularity and 64-bit mode flag.
 * @return The constructed GdtEntry.
 */
GdtEntry make_gdt_entry(std::uint32_t base, std::uint32_t limit, std::uint8_t access, std::uint8_t flags)
{
    GdtEntry entry{};

    // Base address is 32 bits split between 1 16-bit field and 2 8-bit fields
    entry.base_low = base & 0xFFFF;
    entry.base_mid = (base >> 16) & 0xFF;
    entry.base_high = (base >> 24) & 0xFF;

    // Limit is 20 bits split between 1 16-bit field and 1 4-bit field
    entry.limit_low = limit & 0xFFFF;
    entry.limit_high = (limit >> 16) & 0x0F;

    entry.access = access;
    entry.flags = flags;

    return entry;
}

/**
 * @brief Constructs the TSS descriptor for the GDT.
 *
 * The TSS descriptor is 16 bytes (spans 2 GDT slots) because it requires
 * a full 64-bit base address to locate the TSS structure in memory.
 *
 * @return The constructed TssDescriptor.
 */
TssDescriptor make_tss_descriptor()
{
    TssDescriptor desc{};

    std::uint64_t base = reinterpret_cast<std::uint64_t>(&tss);
    std::uint32_t limit = sizeof(TssEntry) - 1;

    desc.limit_low = limit & 0xFFFF;
    desc.base_low = base & 0xFFFF;
    desc.base_mid = (base >> 16) & 0xFF;
    desc.access = ACCESS_PRESENT | ACCESS_RING_0 | ACCESS_TSS;
    desc.limit_flags = (limit >> 16) & 0x0F;
    desc.base_high = (base >> 24) & 0xFF;
    desc.base_upper = (base >> 32) & 0xFFFFFFFF;
    desc.reserved = 0x0;

    return desc;
}

/**
 * @brief Populates the GDT with all segment descriptors.
 *
 * Creates the null descriptor, kernel code/data, user code/data, and TSS entries.
 */
void init_gdt_table()
{
    // Entry 0: null descriptor
    gdt_table.zero = {};

    // Entry 1: kernel code
    gdt_table.kernel_code = make_gdt_entry(0, 0xFFFFF, KERNEL_CODE, FLAGS_64BIT_4KB);

    // Entry 2: kernel data
    gdt_table.kernel_data = make_gdt_entry(0, 0xFFFFF, KERNEL_DATA, FLAGS_64BIT_4KB);

    // Entry 3: user data
    gdt_table.user_data = make_gdt_entry(0, 0xFFFFF, USER_DATA, FLAGS_64BIT_4KB);

    // Entry 4: user code
    gdt_table.user_code = make_gdt_entry(0, 0xFFFFF, USER_CODE, FLAGS_64BIT_4KB);

    // Entry 5-6: TSS
    gdt_table.tss = make_tss_descriptor();
}

/**
 * @brief Initializes the Task State Segment structure.
 *
 * Sets RSP0 to point to the kernel stack for ring 3 to ring 0 transitions.
 */
void init_tss()
{
    tss = {};

    tss.rsp0 = reinterpret_cast<std::uint64_t>(kernel_stack) + sizeof(kernel_stack);
    tss.iopb_offset = sizeof(TssEntry);

    log::infof("GDT: TSS kernel_stack @ [{} - {}]",
        fmt::hex{kernel_stack},
        fmt::hex{tss.rsp0});
}

/**
 * @brief Updates TSS.RSP0 to the given stack top.
 *
 * Called on every process switch so that a ring3->ring0 transition (an
 * interrupt or exception taken from userspace) lands on the CURRENTLY
 * running process's own kernel stack, rather than the static boot-time one.
 */
void set_kernel_stack(std::uintptr_t rsp0)
{
    tss.rsp0 = rsp0;
}

/**
 * @brief Logs a GDT entry's fields for debugging.
 * @param entry The GDT entry to log.
 * @param i The index of the entry in the GDT.
 * @param name Human-readable name for the entry.
 */
void log_gdt_entry(GdtEntry& entry, int i, const char* name)
{
    log::info("GDT[", i, "]: ", name, " [base (", fmt::hex{entry.base_low}, ",",
        fmt::hex{entry.base_mid}, ",", fmt::hex{entry.base_high}, ") limit (",
        fmt::hex{entry.limit_low}, ",", fmt::hex{entry.limit_high}, ") flags (",
        fmt::bin{entry.flags}, ") access (", fmt::hex{entry.access}, ")]");
}

/**
 * @brief Initializes the GDT and TSS, then loads them into the CPU.
 *
 * This is a one-time setup during boot. After LGDT and LTR are executed,
 * the CPU uses these structures automatically for privilege checks and
 * stack switching.
 */
void init()
{
    init_gdt_table();
    init_tss();

    gdtr.limit = sizeof(gdt_table) - 1;
    gdtr.base = reinterpret_cast<std::uint64_t>(&gdt_table);

    load_gdt(&gdtr);
    load_tss();

    log::info("GDT: created with 6 entries");
    log::info("GDT: limit = ", fmt::hex{gdtr.limit});
    log::info("GDT: base = ", fmt::hex{gdtr.base});

    log_gdt_entry(gdt_table.zero, 0, "NULL");
    log_gdt_entry(gdt_table.kernel_code, 1, "Kernel Code");
    log_gdt_entry(gdt_table.kernel_data, 2, "Kernel Data");
    log_gdt_entry(gdt_table.user_data, 3, "User Data");
    log_gdt_entry(gdt_table.user_code, 4, "User Code");
}

}

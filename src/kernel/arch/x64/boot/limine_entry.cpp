/**
 * @file limine_entry.cpp
 * @brief Limine bootloader entry point.
 *
 * This is where the Limine bootloader transfers control to the kernel. By this
 * point, Limine has already set up 64-bit long mode, the HHDM (Higher Half
 * Direct Map), and provided boot info via its protocol (memory map, framebuffer,
 * RSDP, etc.). We just call kernel_main() and hang if it ever returns.
 *
 * Other bootloaders (GRUB, custom) would have their own entry files with
 * different setup requirements, Limine handles more for us than most.
 */

#include <arch/x64/cpu/cpu.hpp>
#include <kpanic/kpanic.hpp>

extern "C" {
using ctor_fn = void (*)();
extern ctor_fn __init_array_start[];
extern ctor_fn __init_array_end[];

extern void kernel_main(void);
}

// Calls C++ global/static constructors. The linker collects one function
// pointer per non-trivially-constructed global into .init_array; nothing
// calls them unless we walk that table ourselves before touching any global.
static void call_global_constructors()
{
    for (ctor_fn* fn = __init_array_start; fn != __init_array_end; ++fn) {
        (*fn)();
    }
}

extern "C" [[noreturn]]
void _start(void)
{
    call_global_constructors();

    kernel_main();

    kpanic("kernel_main returned to _start");
}

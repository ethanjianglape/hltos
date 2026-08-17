#include <arch/x64/cpu/cpu.hpp>
#include <arch/x64/drivers/apic/apic.hpp>
#include <arch/x64/drivers/keyboard/keyboard.hpp>
#include <arch/x64/drivers/pic/pic.hpp>
#include <arch/x64/drivers/serial/serial.hpp>
#include <arch/x64/drivers/tsc/tsc.hpp>
#include <arch/x64/gdt/gdt.hpp>
#include <arch/x64/interrupts/idt.hpp>
#include <arch/x64/percpu/percpu.hpp>
#include <arch/x64/trap/syscall_entry.hpp>

#include <boot/boot.hpp>
#include <console/console.hpp>
#include <containers/kstring.hpp>
#include <framebuffer/framebuffer.hpp>
#include <fs/devfs/dev_tty.hpp>
#include <fs/devfs/devfs.hpp>
#include <fs/procfs/procfs.hpp>
#include <fs/tmpfs/tmpfs.hpp>
#include <gfx/gfx.hpp>
#include <log/log.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>
#include <scheduler/policy/scheduler_policy.hpp>
#include <userspace/userspace.hpp>

#ifdef KERNEL_TESTS
#include <test/test.hpp>
#endif

extern "C" [[noreturn]]
void kernel_main(void)
{
    x64::cpu::early_init();
    x64::percpu::early_init();
    x64::drivers::serial::init();
    x64::drivers::tsc::init();

    boot::init();

    x64::drivers::pic::init();
    x64::drivers::apic::init();
    x64::drivers::keyboard::init();

    x64::gdt::init();
    x64::idt::init();
    x64::cpu::late_init();
    x64::trap::init();
    x64::percpu::init();

    console::init();
    fs::init();

#ifdef KERNEL_TESTS
    test::run_all();
#endif

    userspace::init();

    while (true) {
        x64::percpu::enable_preemption();
        x64::cpu::sti();
        x64::cpu::hlt();
    }
}

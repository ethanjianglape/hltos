#include <arch.hpp>
#include <kpanic/kpanic.hpp>

[[noreturn]]
void kpanic_halt()
{
    arch::percpu::disable_preemption();

    while (true) {
        arch::cpu::cli();
        arch::cpu::hlt();
    }

    __builtin_unreachable();
}

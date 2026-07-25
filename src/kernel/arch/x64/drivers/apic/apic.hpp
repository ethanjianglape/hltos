#pragma once

#include <cstdint>

namespace x64::drivers::apic {

volatile std::uint8_t* get_lapic_addr();

bool check_support();
void enable_apic();
void init();
void send_eoi();
void ioapic_route_irq(std::uint8_t irq, std::uint8_t vector);

}

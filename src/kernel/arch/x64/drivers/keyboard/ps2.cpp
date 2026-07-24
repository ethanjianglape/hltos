#include "ps2.hpp"
#include "keyboard.hpp"

#include <arch/x64/cpu/cpu.hpp>
#include <arch/x64/drivers/apic/apic.hpp>
#include <arch/x64/interrupts/irq.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>
#include <process/process.hpp>
#include <scheduler/scheduler.hpp>

namespace x64::drivers::keyboard {

static bool extended_pending = false;

/**
 * @brief Waits until the controller's input buffer is empty (ready to receive).
 * @return true if ready, false if timeout.
 */
static bool wait_input_ready()
{
    for (int i = 0; i < PS2_TIMEOUT; i++) {
        if ((cpu::inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Waits until the controller's output buffer has data to read.
 * @return true if data available, false if timeout.
 */
static bool wait_output_ready()
{
    for (int i = 0; i < PS2_TIMEOUT; i++) {
        if ((cpu::inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Sends a command to the PS/2 controller (port 0x64).
 */
static bool ps2_send_command(std::uint8_t cmd)
{
    if (!wait_input_ready()) {
        return false;
    }

    cpu::outb(PS2_COMMAND_PORT, cmd);
    return true;
}

/**
 * @brief Sends data to the PS/2 data port (port 0x60).
 */
static bool ps2_send_data(std::uint8_t data)
{
    if (!wait_input_ready()) {
        return false;
    }

    cpu::outb(PS2_DATA_PORT, data);
    return true;
}

/**
 * @brief Reads data from the PS/2 data port (port 0x60).
 */
static bool ps2_read_data(std::uint8_t& out)
{
    if (!wait_output_ready()) {
        return false;
    }

    out = cpu::inb(PS2_DATA_PORT);
    return true;
}

/**
 * @brief Flushes any pending data in the controller's output buffer.
 */
static void ps2_flush()
{
    while (cpu::inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        cpu::inb(PS2_DATA_PORT);
    }
}

static void handle_scancode(std::uint8_t byte)
{
    if (byte == EXTENDED_PREFIX) {
        extended_pending = true;
        return;
    }

    bool released = (byte & RELEASE_MASK) != 0;
    std::uint8_t code = byte & ~RELEASE_MASK;

    ScanCode scancode = ScanCode::Nil;
    ExtendedScanCode extended = ExtendedScanCode::Nil;

    if (extended_pending) {
        extended_pending = false;
        extended = static_cast<ExtendedScanCode>(code);
    } else {
        scancode = static_cast<ScanCode>(code);
    }

    // Update modifier state
    update_modifiers(scancode, extended, released);

    // Build and push key event
    KeyEvent event{
        .scancode = scancode,
        .extended_scancode = extended,
        .released = released,
        .shift_held = is_shift_held(),
        .control_held = is_control_held(),
        .alt_held = is_alt_held(),
        .caps_lock_on = is_caps_lock_on(),
    };

    push_event(event);
}

static void keyboard_interrupt_handler(irq::InterruptFrame*)
{
    while (cpu::inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        std::uint8_t byte = cpu::inb(PS2_DATA_PORT);

        handle_scancode(byte);
    }

    // Tell the scheduler there is keyboard input ready for any process
    // that is currently waiting for it
    scheduler::get_scheduler()->wake_single(process::WaitReason::KEYBOARD);

    apic::send_eoi();
}

static bool ps2_controller_exists()
{
    std::uint8_t status = cpu::inb(PS2_STATUS_PORT);
    return status != 0xFF;
}

static bool ps2_self_test()
{
    if (!ps2_send_command(PS2_CMD_SELF_TEST)) {
        log::error("PS/2: Failed to send self-test command");
        return false;
    }

    std::uint8_t response;
    if (!ps2_read_data(response)) {
        log::error("PS/2: Self-test timeout");
        return false;
    }

    if (response != PS2_SELF_TEST_OK) {
        log::error("PS/2: Self-test failed (response: ", response, ")");
        return false;
    }

    return true;
}

static bool ps2_test_port1()
{
    if (!ps2_send_command(PS2_CMD_TEST_PORT1)) {
        log::error("PS/2: Failed to send port 1 test command");
        return false;
    }

    std::uint8_t response;
    if (!ps2_read_data(response)) {
        log::error("PS/2: Port 1 test timeout");
        return false;
    }

    if (response != PS2_PORT_TEST_OK) {
        log::error("PS/2: Port 1 test failed (response: ", response, ")");
        return false;
    }

    return true;
}

static bool keyboard_reset()
{
    if (!ps2_send_data(KB_CMD_RESET)) {
        log::error("Keyboard: Failed to send reset command");
        return false;
    }

    std::uint8_t response;
    if (!ps2_read_data(response)) {
        log::error("Keyboard: Reset ACK timeout");
        return false;
    }

    if (response != KB_RESPONSE_ACK) {
        log::error("Keyboard: Reset not acknowledged (response: ", response, ")");
        return false;
    }

    // Wait for self-test result
    if (!ps2_read_data(response)) {
        log::error("Keyboard: Self-test result timeout");
        return false;
    }

    if (response != KB_RESPONSE_SELF_TEST_OK) {
        log::error("Keyboard: Self-test failed (response: ", response, ")");
        return false;
    }

    return true;
}

namespace ps2 {

bool init()
{
    // Step 1: Check if PS/2 controller exists
    if (!ps2_controller_exists()) {
        log::error("PS/2: No controller detected");
        return false;
    }

    // Step 2: Disable both PS/2 ports during initialization
    ps2_send_command(PS2_CMD_DISABLE_PORT1);
    ps2_send_command(PS2_CMD_DISABLE_PORT2);

    // Step 3: Flush the output buffer
    ps2_flush();

    // Step 4: Read and modify controller configuration
    ps2_send_command(PS2_CMD_READ_CONFIG);
    std::uint8_t config;
    if (!ps2_read_data(config)) {
        log::error("PS/2: Failed to read configuration");
        return false;
    }

    // Disable IRQs and translation during setup
    config &= ~(PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_PORT2_IRQ | PS2_CONFIG_TRANSLATION);

    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    // Step 5: Controller self-test
    if (!ps2_self_test()) {
        return false;
    }

    // Note: Self-test may reset the controller, so restore config
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    // Step 6: Test first PS/2 port
    if (!ps2_test_port1()) {
        return false;
    }

    // Step 7: Enable first PS/2 port
    ps2_send_command(PS2_CMD_ENABLE_PORT1);

    // Step 8: Reset keyboard device
    if (!keyboard_reset()) {
        return false;
    }

    // Step 9: Enable IRQ for port 1 in configuration
    ps2_send_command(PS2_CMD_READ_CONFIG);
    if (ps2_read_data(config)) {
        config |= PS2_CONFIG_PORT1_IRQ | PS2_CONFIG_TRANSLATION;
        ps2_send_command(PS2_CMD_WRITE_CONFIG);
        ps2_send_data(config);
    }

    // Step 10: Configure IOAPIC routing and register handler
    apic::ioapic_route_irq(irq::IRQ_KEYBOARD, irq::VECTOR_KEYBOARD);
    irq::register_irq_handler(irq::VECTOR_KEYBOARD, keyboard_interrupt_handler);

    return true;
}

}
}

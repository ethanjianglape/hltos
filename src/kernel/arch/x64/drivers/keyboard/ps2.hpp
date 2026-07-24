#pragma once

#include <cstdint>

namespace x64::drivers::keyboard {
// =========================================================================
// PS/2 Controller Constants
// =========================================================================

// I/O Ports
constexpr std::uint16_t PS2_DATA_PORT = 0x60;    // Read/write data
constexpr std::uint16_t PS2_STATUS_PORT = 0x64;  // Read status
constexpr std::uint16_t PS2_COMMAND_PORT = 0x64; // Write commands

// Status register bits (read from port 0x64)
constexpr std::uint8_t PS2_STATUS_OUTPUT_FULL = 0x01; // Data available to read
constexpr std::uint8_t PS2_STATUS_INPUT_FULL = 0x02;  // Controller busy, don't write

// Controller commands (write to port 0x64)
constexpr std::uint8_t PS2_CMD_READ_CONFIG = 0x20;   // Read configuration byte
constexpr std::uint8_t PS2_CMD_WRITE_CONFIG = 0x60;  // Write configuration byte
constexpr std::uint8_t PS2_CMD_DISABLE_PORT2 = 0xA7; // Disable second PS/2 port
constexpr std::uint8_t PS2_CMD_ENABLE_PORT2 = 0xA8;  // Enable second PS/2 port
constexpr std::uint8_t PS2_CMD_TEST_PORT2 = 0xA9;    // Test second PS/2 port
constexpr std::uint8_t PS2_CMD_SELF_TEST = 0xAA;     // Controller self-test
constexpr std::uint8_t PS2_CMD_TEST_PORT1 = 0xAB;    // Test first PS/2 port
constexpr std::uint8_t PS2_CMD_DISABLE_PORT1 = 0xAD; // Disable first PS/2 port
constexpr std::uint8_t PS2_CMD_ENABLE_PORT1 = 0xAE;  // Enable first PS/2 port

// Configuration byte bits
constexpr std::uint8_t PS2_CONFIG_PORT1_IRQ = 0x01;   // Enable IRQ1 for port 1
constexpr std::uint8_t PS2_CONFIG_PORT2_IRQ = 0x02;   // Enable IRQ12 for port 2
constexpr std::uint8_t PS2_CONFIG_PORT1_CLOCK = 0x10; // Disable port 1 clock
constexpr std::uint8_t PS2_CONFIG_PORT2_CLOCK = 0x20; // Disable port 2 clock
constexpr std::uint8_t PS2_CONFIG_TRANSLATION = 0x40; // Enable scancode translation

// Self-test responses
constexpr std::uint8_t PS2_SELF_TEST_OK = 0x55;
constexpr std::uint8_t PS2_PORT_TEST_OK = 0x00;

// Keyboard commands (write to port 0x60)
constexpr std::uint8_t KB_CMD_RESET = 0xFF; // Reset keyboard

// Keyboard responses
constexpr std::uint8_t KB_RESPONSE_ACK = 0xFA;          // Command acknowledged
constexpr std::uint8_t KB_RESPONSE_SELF_TEST_OK = 0xAA; // Self-test passed

// Timeout for polling (iterations)
constexpr int PS2_TIMEOUT = 100000;

// =========================================================================
// PS/2 Scancode Set 1
// =========================================================================
//
// Key release = make code | 0x80
// Extended keys are prefixed with 0xE0

enum class ScanCode : std::uint8_t {
    Nil = 0x00,

    // Row 1: Escape and Function keys
    Escape = 0x01,
    F1 = 0x3B,
    F2 = 0x3C,
    F3 = 0x3D,
    F4 = 0x3E,
    F5 = 0x3F,
    F6 = 0x40,
    F7 = 0x41,
    F8 = 0x42,
    F9 = 0x43,
    F10 = 0x44,
    F11 = 0x57,
    F12 = 0x58,

    // Row 2: Number row
    Grave = 0x29, // ` ~
    Key1 = 0x02,
    Key2 = 0x03,
    Key3 = 0x04,
    Key4 = 0x05,
    Key5 = 0x06,
    Key6 = 0x07,
    Key7 = 0x08,
    Key8 = 0x09,
    Key9 = 0x0A,
    Key0 = 0x0B,
    Minus = 0x0C,  // - _
    Equals = 0x0D, // = +
    Backspace = 0x0E,

    // Row 3: QWERTY top row
    Tab = 0x0F,
    Q = 0x10,
    W = 0x11,
    E = 0x12,
    R = 0x13,
    T = 0x14,
    Y = 0x15,
    U = 0x16,
    I = 0x17,
    O = 0x18,
    P = 0x19,
    LeftBracket = 0x1A,  // [ {
    RightBracket = 0x1B, // ] }
    Backslash = 0x2B,    // \ |

    // Row 4: Home row
    CapsLock = 0x3A,
    A = 0x1E,
    S = 0x1F,
    D = 0x20,
    F = 0x21,
    G = 0x22,
    H = 0x23,
    J = 0x24,
    K = 0x25,
    L = 0x26,
    Semicolon = 0x27,  // ; :
    Apostrophe = 0x28, // ' "
    Enter = 0x1C,

    // Row 5: Bottom row
    LeftShift = 0x2A,
    Z = 0x2C,
    X = 0x2D,
    C = 0x2E,
    V = 0x2F,
    B = 0x30,
    N = 0x31,
    M = 0x32,
    Comma = 0x33,  // , <
    Period = 0x34, // . >
    Slash = 0x35,  // / ?
    RightShift = 0x36,

    // Row 6: Bottom modifiers and space
    LeftCtrl = 0x1D,
    LeftAlt = 0x38,
    Space = 0x39,

    // Lock keys
    NumLock = 0x45,
    ScrollLock = 0x46,

    // Keypad
    KeypadAsterisk = 0x37,
    KeypadMinus = 0x4A,
    KeypadPlus = 0x4E,
    Keypad7 = 0x47,
    Keypad8 = 0x48,
    Keypad9 = 0x49,
    Keypad4 = 0x4B,
    Keypad5 = 0x4C,
    Keypad6 = 0x4D,
    Keypad1 = 0x4F,
    Keypad2 = 0x50,
    Keypad3 = 0x51,
    Keypad0 = 0x52,
    KeypadPeriod = 0x53,
};

// Extended scancodes (preceded by 0xE0)
enum class ExtendedScanCode : std::uint8_t {
    Nil = 0x00,

    // The raw byte after 0xE0 prefix
    RightAlt = 0x38,
    RightCtrl = 0x1D,
    KeypadSlash = 0x35,
    KeypadEnter = 0x1C,

    Insert = 0x52,
    Delete = 0x53,
    Home = 0x47,
    End = 0x4F,
    PageUp = 0x49,
    PageDown = 0x51,

    UpArrow = 0x48,
    DownArrow = 0x50,
    LeftArrow = 0x4B,
    RightArrow = 0x4D,

    LeftGui = 0x5B, // Windows key
    RightGui = 0x5C,
    Menu = 0x5D,    // Context menu key
};

// Special prefix bytes
constexpr std::uint8_t EXTENDED_PREFIX = 0xE0;
constexpr std::uint8_t RELEASE_MASK = 0x80;

// =========================================================================
// PS/2 Backend API
// =========================================================================

namespace ps2 {

/// @brief Initializes the PS/2 controller and keyboard.
/// @return true if initialization successful.
///
bool init();
}

}

#include "keyboard.hpp"

#include <containers/klist.hpp>
#include <containers/kvector.hpp>
#include <exclusive/kspinlock_irqsave.hpp>
#include <kpanic/kpanic.hpp>
#include <log/log.hpp>

namespace x64::drivers::keyboard {

static klist<KeyEvent> event_buffer;

static bool shift_held = false;
static bool control_held = false;
static bool alt_held = false;
static bool caps_lock = false;

static kspinlock_irqsave g_keyboard_spinlock;

void update_modifiers(ScanCode scancode, ExtendedScanCode extended, bool released)
{
    // Standard scancodes
    if (scancode == ScanCode::LeftShift || scancode == ScanCode::RightShift) {
        shift_held = !released;
    } else if (scancode == ScanCode::LeftCtrl) {
        control_held = !released;
    } else if (scancode == ScanCode::LeftAlt) {
        alt_held = !released;
    } else if (scancode == ScanCode::CapsLock && !released) {
        caps_lock = !caps_lock;
    }

    // Extended scancodes
    if (extended == ExtendedScanCode::RightCtrl) {
        control_held = !released;
    } else if (extended == ExtendedScanCode::RightAlt) {
        alt_held = !released;
    }
}

bool is_shift_held() { return shift_held; }
bool is_control_held() { return control_held; }
bool is_alt_held() { return alt_held; }
bool is_caps_lock_on() { return caps_lock; }

void push_event(const KeyEvent& event)
{
    g_keyboard_spinlock.lock();
    event_buffer.push_back(event);
    g_keyboard_spinlock.unlock();
}

KeyEvent* poll()
{
    g_keyboard_spinlock.lock();

    if (event_buffer.empty()) {
        g_keyboard_spinlock.unlock();
        return nullptr;
    }

    // Copy front element to static storage, then remove from buffer
    static KeyEvent current_event;
    current_event = event_buffer.front();
    event_buffer.erase(0);

    g_keyboard_spinlock.unlock();

    return &current_event;
}

KeyEvent* read()
{
    while (true) {
        if (KeyEvent* event = poll()) {
            return event;
        }

        cpu::hlt();
    }
}

void init()
{
    if (!ps2::init()) {
        kpanic("PS/2 keyboard initialization failed");
    }

    log::info("keyboard: PS/2 keyboard initialized");
}

}

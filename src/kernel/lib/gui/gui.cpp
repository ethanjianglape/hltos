#include "console/console.hpp"
#include "containers/kvector.hpp"
#include "framebuffer/framebuffer.hpp"
#include "log/log.hpp"
#include <cstdint>
#include <gui/gui.hpp>

#include <process/process.hpp>
#include <scheduler/scheduler.hpp>

namespace gui {

class Square {
public:
    int x;
    int y;
    int w;
    int h;
    int dx;
    int dy;
    std::uint32_t color;

    Square(int x, int y, int w, int h, int dx, int dy, std::uint32_t color)
        : x{x}
        , y{y}
        , w{w}
        , h{h}
        , dx{dx}
        , dy{dy}
        , color{color}
    {
    }

    void draw()
    {
        framebuffer::fill_rect(x, y, w, h, color);
    }

    void move()
    {
        x += dx;
        y += dy;

        if (x <= 0) {
            x = 0;
            dx = -dx;
        }

        if (x + w >= (int)framebuffer::get_screen_width()) {
            x = framebuffer::get_screen_width() - w;
            dx = -dx;
        }

        if (y <= 0) {
            y = 0;
            dy = -dy;
        }

        if (y + h >= (int)framebuffer::get_screen_height()) {
            y = framebuffer::get_screen_height() - h;
            dy = -dy;
        }
    }
};

class Player : public Square {
public:
    bool left;
    bool right;
    bool up;
    bool down;
    int speed = 3;

    Player()
        : Square{0, 0, 100, 100, 0, 0, 0x00ABABAB}
    {
    }

    void move()
    {
        if (left) {
            x -= speed;
        } else if (right) {
            x += speed;
        }

        if (up) {
            y -= speed;
        } else if (down) {
            y += speed;
        }

        if (x <= 0) {
            x = 0;
        }

        if (x + w >= (int)framebuffer::get_screen_width()) {
            x = framebuffer::get_screen_width() - w;
        }

        if (y <= 0) {
            y = 0;
        }

        if (y + h >= (int)framebuffer::get_screen_height()) {
            y = framebuffer::get_screen_height() - h;
        }
    }
};

static kvector<Square> squares;
static Player* player;

static void
gui_render_kthread()
{
    constexpr int target_fps = 60;
    constexpr int ms_per_frame = 1000 / target_fps;

    while (true) {
        framebuffer::clear_black();

        player->draw();

        for (Square& square : squares) {
            square.draw();
        }

        framebuffer::outline_rect(100, 100, 100, 100, 0x0000FFFF);

        scheduler::get_scheduler()->yield_sleep(ms_per_frame);
    }
}

static void gui_tick_kthread()
{
    constexpr int target_fps = 100;
    constexpr int ms_per_frame = 1000 / target_fps;

    while (true) {
        player->move();

        for (Square& square : squares) {
            square.move();
        }

        scheduler::get_scheduler()->yield_sleep(ms_per_frame);
    }
}

static void gui_input_kthread()
{
    namespace keyboard = arch::drivers::keyboard;

    using ScanCode = keyboard::ScanCode;
    using ExtendedScanCode = keyboard::ExtendedScanCode;

    while (true) {
        while (keyboard::KeyEvent* event = keyboard::poll()) {
            keyboard::ScanCode scancode = event->scancode;
            keyboard::ExtendedScanCode extended = event->extended_scancode;

            // bool caps = event->shift_held || event->caps_lock_on;
            // bool ctrl = event->control_held;

            switch (extended) {
            case ExtendedScanCode::LeftArrow:
                player->left = !event->released;
                break;
            case ExtendedScanCode::RightArrow:
                player->right = !event->released;
                break;
            case ExtendedScanCode::UpArrow:
                player->up = !event->released;
                break;
            case ExtendedScanCode::DownArrow:
                player->down = !event->released;
                break;
            default:
                break;
            }
        }

        scheduler::get_scheduler()->yield_blocked(process::WaitReason::KEYBOARD);
    }
}

void init()
{
    squares.emplace_back(0, 0, 10, 10, 1, 1, framebuffer::RGB_GREEN);
    squares.emplace_back(300, 300, 15, 15, 2, 1, framebuffer::RGB_RED);
    squares.emplace_back(100, 500, 50, 50, 3, 2, 0x00FF00FF);
    squares.emplace_back(666, 666, 25, 25, -5, -6, 0x00FFFF00);
    squares.emplace_back(50, 50, 200, 20, -3, 5, 0x0000FFFF);

    player = new Player{};

    scheduler::get_scheduler()->add_process(new process::KThread{gui_render_kthread});
    scheduler::get_scheduler()->add_process(new process::KThread{gui_tick_kthread});
    scheduler::get_scheduler()->add_process(new process::KThread{gui_input_kthread});
}

}

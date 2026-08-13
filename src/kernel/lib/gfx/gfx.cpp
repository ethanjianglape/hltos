#include "fmt/fmt.hpp"
#include "log/log.hpp"
#include <containers/kvector.hpp>
#include <cstdint>
#include <framebuffer/framebuffer.hpp>
#include <gfx/fonts/font8x16.hpp>
#include <gfx/gfx.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>

namespace gfx {

class Square {
public:
    int x;
    int y;
    int w;
    int h;
    int dx;
    int dy;
    std::uint32_t color;
    bool solid;

    Square(int x, int y, int w, int h, int dx, int dy, std::uint32_t color, bool solid = true)
        : x{x}
        , y{y}
        , w{w}
        , h{h}
        , dx{dx}
        , dy{dy}
        , color{color}
        , solid{solid}
    {
    }

    void draw()
    {
        if (solid) {
            framebuffer::fill_rect(x, y, w, h, color);
        } else {
            framebuffer::outline_rect(x, y, w, h, color);
        }
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

class Line {
public:
    int x0, x0_start;
    int y0, y0_start;
    int x1, x1_start;
    int y1, y1_start;

    int dx;
    int dy;

    Line(int a, int b, int c, int d)
        : x0{a}
        , x0_start{a}
        , y0{b}
        , y0_start{b}
        , x1{c}
        , x1_start{c}
        , y1{d}
        , y1_start{d}
        , dx{0}
        , dy{1}
    {
    }

    void draw()
    {
        framebuffer::draw_line(x0, y0, x1, y1, 0x00FF0000);
    }

    void move()
    {
        x0 += dx;
        y0 += dy;

        x1 -= dx;
        y1 -= dy;

        if (dy != 0) {
            if (y0 == y1_start || y1 == y0_start) {
                dy = 0;
                dx = 1;
            } else if (y0 == y0_start || y1 == y1_start) {
                dy = 0;
                dx = -1;
            }
        } else if (dx != 0) {
            if (x0 == x1_start || x1 == x0_start) {
                dy = -1;
                dx = 0;
            } else if (x0 == x0_start || x1 == x1_start) {
                dy = 1;
                dx = 0;
            }
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

class Text {
public:
    int x;
    int y;
    int fg;
    int bg;
    kstring str;
    fonts::Font8x16 font;

    Text(int x, int y, const char* c_str)
        : x{x}
        , y{y}
        , fg{0x00FFFFFF}
        , bg{-1}
        , str{c_str}
    {
    }

    void draw()
    {
        framebuffer::draw_str(x, y, &font, str, fg, bg);
    }
};

static kvector<Square> squares;
static kvector<Line> lines;
static kvector<Text> texts;
static Player* player;

static void
gfx_render_kthread()
{
    constexpr float target_fps = 120;
    constexpr float ms_per_frame = 1000 / target_fps;
    constexpr int us_per_frame = ms_per_frame * 1000;

    while (true) {
        framebuffer::clear_black();

        for (Square& square : squares) {
            square.draw();
        }

        for (Line& line : lines) {
            line.draw();
        }

        player->draw();

        for (Text& text : texts) {
            text.draw();
        }

        scheduler::mechanism::yield_sleep_us(us_per_frame);
    }
}

static void gfx_tick_kthread()
{
    constexpr float target_fps = 60;

    while (true) {
        player->move();

        for (Square& square : squares) {
            square.move();
        }

        for (Line& line : lines) {
            line.move();
        }

        Text& text = texts.front();

        text.str = fmt::sprintf("player at ({}, {})", player->x, player->y);

        scheduler::mechanism::yield_sleep_hz(target_fps);
    }
}

static void gfx_input_kthread()
{
    namespace keyboard = arch::drivers::keyboard;

    // using ScanCode = keyboard::ScanCode;
    using ExtendedScanCode = keyboard::ExtendedScanCode;

    while (true) {
        while (keyboard::KeyEvent* event = keyboard::poll()) {
            // keyboard::ScanCode scancode = event->scancode;
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

        scheduler::mechanism::yield_blocked(process::WaitReason::KEYBOARD);
    }
}

void init()
{
    log::info("gfx: initialized");

    squares.emplace_back(0, 0, 10, 10, 1, 1, framebuffer::RGB_GREEN);
    squares.emplace_back(300, 300, 15, 15, 2, 1, framebuffer::RGB_RED);
    squares.emplace_back(100, 500, 50, 50, 3, 2, 0x00FF00FF);
    squares.emplace_back(666, 666, 25, 25, -5, -6, 0x00FFFF00);
    squares.emplace_back(50, 50, 200, 20, -3, 5, 0x0000FFFF);
    squares.emplace_back(100, 100, 200, 200, 0, 0, 0x00D3D3D3);

    lines.emplace_back(100, 100, 300, 300);

    texts.emplace_back(0, 0, "this is a string");

    player = new Player{};

    scheduler::mechanism::add_process(new process::KThread{gfx_render_kthread});
    scheduler::mechanism::add_process(new process::KThread{gfx_tick_kthread});
    scheduler::mechanism::add_process(new process::KThread{gfx_input_kthread});
}

}

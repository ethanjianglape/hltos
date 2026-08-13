#include <algo/algo.hpp>
#include <arch.hpp>
#include <clock/clock.hpp>
#include <console/console.hpp>
#include <containers/kvector.hpp>
#include <crt/crt.h>
#include <exclusive/kspinlock.hpp>
#include <fmt/fmt.hpp>
#include <framebuffer/framebuffer.hpp>
#include <gfx/fonts/font8x16.hpp>
#include <kassert/kassert.hpp>
#include <log/log.hpp>
#include <memory/memory.hpp>
#include <process/process.hpp>
#include <scheduler/mechanism/scheduler_mechanism.hpp>

#include <cstdint>

namespace framebuffer {

static std::uint64_t fb_width;
static std::uint64_t fb_height;
static std::uint64_t fb_num_pixels;
static std::uint64_t fb_pitch;
static std::uint16_t fb_bpp;

static std::uint8_t* vram = nullptr;
static std::uint8_t* vram_end = nullptr;
static std::uint64_t vram_size;

static std::uint8_t* vram_buff = nullptr;
static std::uint8_t* vram_buff_end = nullptr;

static bool needs_redraw = false;

static kspinlock g_fb_spinlock;

static kvector<std::uint64_t> redraw_times;

std::uint32_t get_screen_width()
{
    return fb_width;
}

std::uint32_t get_screen_height()
{
    return fb_height;
}

static inline constexpr std::size_t get_pixel_offset(std::uint32_t x, std::uint32_t y)
{
    return (y * fb_pitch) + (x * (fb_bpp / 8));
}

static void redraw()
{
    g_fb_spinlock.lock();

    if (needs_redraw) {
        const auto start = clock::get_time_us();

        std::uint64_t* vram_ptr = reinterpret_cast<std::uint64_t*>(vram);
        std::uint64_t* vram_buff_ptr = reinterpret_cast<std::uint64_t*>(vram_buff);

        for (std::size_t i = 0; i < vram_size / 8; i++) {
            *(vram_ptr + i) = *(vram_buff_ptr + i);
        }

        needs_redraw = false;
        const auto end = clock::get_time_us();
        redraw_times.push_back(end - start);
    }

    g_fb_spinlock.unlock();
}

static void redraw_kthread()
{
    constexpr std::uint64_t target_hz = 120;

    while (true) {
        redraw();

        scheduler::mechanism::yield_sleep_hz(target_hz);
    }
}

static void debug_kthread()
{
    while (true) {
        g_fb_spinlock.lock();

        if (!redraw_times.empty()) {
            std::uint64_t avg_us = 0;

            for (std::uint64_t us : redraw_times) {
                avg_us += us;
            }

            avg_us /= redraw_times.size();
            redraw_times.clear();

            log::debugf("framebuffer: average redraw time: {}us", avg_us);
        }

        g_fb_spinlock.unlock();

        scheduler::mechanism::yield_sleep_ms(2000);
    }
}

void init(const FrameBufferInfo& info)
{
    fb_width = info.width;
    fb_height = info.height;
    fb_num_pixels = fb_width * fb_height;
    fb_pitch = info.pitch;
    fb_bpp = info.bpp;

    vram_size = fb_num_pixels * (fb_bpp / 8);
    vram = info.vram;
    vram_end = vram + vram_size;

    vram_buff = new std::uint8_t[vram_size];
    vram_buff_end = vram_buff + vram_size;

    scheduler::mechanism::add_process(new process::KThread(redraw_kthread));
    // scheduler::mechanism::add_process(new process::KThread(debug_kthread));

    log::infof("framebuffer: {}x{} @ {} bpp (pitch={})", fb_width, fb_height, fb_bpp, fb_pitch);
    log::infof("framebuffer: {} total pixels", fb_num_pixels);
    log::infof("framebuffer: VRAM @ [{} - {}] ({} bytes)", fmt::hex{vram}, fmt::hex{vram_end}, vram_size);
    log::infof("framebuffer: VRAM back buffer @ [{} - {}]", fmt::hex{vram_buff}, fmt::hex{vram_buff_end});
}

static std::uint32_t get_pixel(std::uint32_t x, std::uint32_t y)
{
    std::uint32_t pixel = 0x00000000;
    const std::size_t offset = get_pixel_offset(x, y);

    pixel |= vram_buff[offset + RGB_OFFB];
    pixel |= vram_buff[offset + RGB_OFFG] << 8;
    pixel |= vram_buff[offset + RGB_OFFR] << 16;

    return pixel;
}

static void draw_pixel(
    std::uint32_t x,
    std::uint32_t y,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue)
{
    const std::size_t offset = get_pixel_offset(x, y);

    vram_buff[offset + RGB_OFFB] = blue;
    vram_buff[offset + RGB_OFFG] = green;
    vram_buff[offset + RGB_OFFR] = red;

    needs_redraw = true;
}

static void draw_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color)
{
    const std::uint8_t blue = color & 0xFF;
    const std::uint8_t green = (color >> 8) & 0xFF;
    const std::uint8_t red = (color >> 16) & 0xFF;

    draw_pixel(x, y, red, green, blue);
}

void invert_rec(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h)
{
    g_fb_spinlock.lock();

    for (std::uint32_t px = x; px < x + w; px++) {
        for (std::uint32_t py = y; py < y + h; py++) {
            const auto color = get_pixel(px, py);
            draw_pixel(px, py, ~color);
        }
    }

    g_fb_spinlock.unlock();
}

static inline void draw_x_dominant_line(
    int x0,
    int y0,
    int x1,
    int y1,
    int dx,
    int dy,
    int color)
{
    g_fb_spinlock.lock();

    const std::uint8_t blue = color & 0xFF;
    const std::uint8_t green = (color >> 8) & 0xFF;
    const std::uint8_t red = (color >> 16) & 0xFF;

    const int sx = x1 >= x0 ? 1 : -1;
    const int sy = y1 >= y0 ? 1 : -1;

    int d = 2 * dy - dx;
    int x = x0;
    int y = y0;

    while (x != x1) {
        draw_pixel(x, y, red, green, blue);

        if (d > 0) {
            y += sy;
            d = d + (2 * (dy - dx));
        } else {
            d = d + 2 * dy;
        }

        x += sx;
    }

    g_fb_spinlock.unlock();
}

static inline void draw_y_dominant_line(
    int x0,
    int y0,
    int x1,
    int y1,
    int dx,
    int dy,
    int color)
{
    g_fb_spinlock.lock();

    const std::uint8_t blue = color & 0xFF;
    const std::uint8_t green = (color >> 8) & 0xFF;
    const std::uint8_t red = (color >> 16) & 0xFF;

    const int sx = x1 >= x0 ? 1 : -1;
    const int sy = y1 >= y0 ? 1 : -1;

    int d = 2 * dx - dy;
    int x = x0;
    int y = y0;

    while (y != y1) {
        draw_pixel(x, y, red, green, blue);

        if (d > 0) {
            x += sx;
            d = d + (2 * (dx - dy));

        } else {
            d = d + 2 * dx;
        }

        y += sy;
    }

    g_fb_spinlock.unlock();
}

void draw_line(int x0, int y0, int x1, int y1, int color)
{
    kassert(x0 >= 0 && x0 < static_cast<int>(fb_width), "x0 out of bounds");
    kassert(x1 >= 0 && x1 < static_cast<int>(fb_width), "x1 out of bounds");
    kassert(y0 >= 0 && y0 < static_cast<int>(fb_height), "y0 out of bounds");
    kassert(y1 >= 0 && y1 < static_cast<int>(fb_height), "y1 out of bounds");

    const int dx = algo::abs(x1 - x0);
    const int dy = algo::abs(y1 - y0);

    if (dx >= dy) {
        return draw_x_dominant_line(x0, y0, x1, y1, dx, dy, color);
    }

    return draw_y_dominant_line(x0, y0, x1, y1, dx, dy, color);
}

void outline_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color)
{
    kassert(x + w < fb_width);
    kassert(y + h < fb_height);

    g_fb_spinlock.lock();

    const std::uint8_t blue = color & 0xFF;
    const std::uint8_t green = (color >> 8) & 0xFF;
    const std::uint8_t red = (color >> 16) & 0xFF;

    // top
    for (std::uint32_t px = x; px < x + w; px++) {
        draw_pixel(px, y, red, green, blue);
    }

    // bottom
    for (std::uint32_t px = x; px < x + w; px++) {
        draw_pixel(px, y + h, red, green, blue);
    }

    // left
    for (std::uint32_t py = y; py < y + h; py++) {
        draw_pixel(x, py, red, green, blue);
    }

    // right
    for (std::uint32_t py = y; py < y + h; py++) {
        draw_pixel(x + w, py, red, green, blue);
    }

    g_fb_spinlock.unlock();
}

void fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color)
{
    g_fb_spinlock.lock();

    const auto blue = color & 0xFF;
    const auto green = (color >> 8) & 0xFF;
    const auto red = (color >> 16) & 0xFF;

    for (std::uint32_t px = x; px < x + w; px++) {
        for (std::uint32_t py = y; py < y + h; py++) {
            draw_pixel(px, py, red, green, blue);
        }
    }

    g_fb_spinlock.unlock();
}

void draw_str(std::uint32_t x, std::uint32_t y, gfx::fonts::Font* font, kstring_view str, int fg, int bg)
{
    kassert_not_null(font);

    for (std::size_t i = 0; i < str.size(); i++) {
        draw_char(x + (i * font->font_width()), y, font, str[i], fg, bg);
    }
}

void draw_char(std::uint32_t x, std::uint32_t y, gfx::fonts::Font* font, char c, int fg, int bg)
{
    kassert_not_null(font);
    kassert(x + font->font_width() < fb_width, "x out of bounds");
    kassert(y + font->font_height() < fb_height, "y out of bounds");

    g_fb_spinlock.lock();

    const std::uint8_t* glyph = font->get_glyph(c);

    for (int gy = 0; gy < font->font_height(); gy++) {
        const std::uint8_t byte = glyph[gy];

        for (int gx = 0; gx < font->font_width(); gx++) {
            const std::uint8_t pixel = (byte >> (font->font_width() - gx - 1)) & 1;

            if (pixel == 1) {
                draw_pixel(x + gx, y + gy, fg);
            } else if (bg >= 0) {
                draw_pixel(x + gx, y + gy, bg);
            }
        }
    }

    g_fb_spinlock.unlock();
}

void clear_black()
{
    clear(RGB_BLACK);
}

void clear(std::uint32_t color)
{
    g_fb_spinlock.lock();

    auto* start = reinterpret_cast<std::uint32_t*>(vram_buff);
    auto* end = reinterpret_cast<std::uint32_t*>(vram_buff_end);

    while (start != end) {
        *start = color;
        start++;
    }

    g_fb_spinlock.unlock();
}

void log()
{
    log::info("Screen = ", fb_width, "x", fb_height, "x", fb_bpp);
    log::info("VRAM   = ", fmt::hex{vram});
    log::info("VRAM Back Buffer = ", fmt::hex{vram_buff});
}

}

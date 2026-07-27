#include "algo/algo.hpp"
#include "arch/x64/drivers/tsc/tsc.hpp"
#include <arch.hpp>
#include <console/console.hpp>
#include <crt/crt.h>
#include <exclusive/kspinlock.hpp>
#include <fmt/fmt.hpp>
#include <framebuffer/framebuffer.hpp>
#include <log/log.hpp>
#include <memory/memory.hpp>
#include <process/process.hpp>
#include <scheduler/scheduler.hpp>
#include <timer/timer.hpp>

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

std::uint32_t get_screen_width()
{
    return fb_width;
}

std::uint32_t get_screen_height()
{
    return fb_height;
}

inline constexpr std::size_t get_pixel_offset(std::uint32_t x, std::uint32_t y)
{
    return (y * fb_pitch) + (x * (fb_bpp / 8));
}

static void redraw()
{
    g_fb_spinlock.lock();
    memcpy(vram, static_cast<const std::uint8_t*>(vram_buff), vram_size);
    needs_redraw = false;
    g_fb_spinlock.unlock();
}

static void redraw_kthread()
{
    constexpr int target_fps = 60;
    constexpr int ms_per_frame = 1000 / target_fps;
    constexpr int us_per_frame = ms_per_frame * 1000;

    while (true) {
        redraw();

        // arch::drivers::tsc::sleep_us(us_per_frame);
        scheduler::yield_sleep_us(us_per_frame);
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

    vram_buff = kalloc<std::uint8_t>(vram_size);
    vram_buff_end = vram_buff + vram_size;

    scheduler::add_process(new process::KThread(redraw_kthread));

    log::infof("Framebuffer: {}x{} @ {} bpp (pitch={})", fb_width, fb_height, fb_bpp, fb_pitch);
    log::infof("Framebuffer: {} total pixels", fb_num_pixels);
    log::infof("Framebuffer: VRAM @ [{} - {}] ({} bytes)", fmt::hex{vram}, fmt::hex{vram_end}, vram_size);
    log::infof("Framebuffer: VRAM double buffer @ [{}-{}]", fmt::hex{vram_buff}, fmt::hex{vram_buff_end});
}

static std::uint32_t get_pixel(std::uint32_t x, std::uint32_t y)
{
    std::uint32_t pixel = 0x00000000;
    const auto offset = get_pixel_offset(x, y);

    pixel |= vram_buff[offset + RGB_OFFB];
    pixel |= vram_buff[offset + RGB_OFFG] << 8;
    pixel |= vram_buff[offset + RGB_OFFR] << 16;

    return pixel;
}

static void draw_pixel(std::uint32_t x,
    std::uint32_t y,
    std::uint8_t red,
    std::uint8_t green,
    std::uint8_t blue)
{
    const auto offset = get_pixel_offset(x, y);

    vram_buff[offset + RGB_OFFB] = blue;
    vram_buff[offset + RGB_OFFG] = green;
    vram_buff[offset + RGB_OFFR] = red;

    needs_redraw = true;
}

static void draw_pixel(std::uint32_t x, std::uint32_t y, std::uint32_t color)
{
    const auto blue = color & 0xFF;
    const auto green = (color >> 8) & 0xFF;
    const auto red = (color >> 16) & 0xFF;

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

void draw_line(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t color)
{
}

void outline_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color)
{
    g_fb_spinlock.lock();

    const auto blue = color & 0xFF;
    const auto green = (color >> 8) & 0xFF;
    const auto red = (color >> 16) & 0xFF;

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

void draw_glyph(const std::uint32_t x, const std::uint32_t y, std::uint32_t w, std::uint32_t h, const std::uint8_t* glyph, std::uint32_t fg, std::uint32_t bg)
{
    g_fb_spinlock.lock();

    for (std::uint8_t gy = 0; gy < h; gy++) {
        const std::uint8_t byte = glyph[gy];

        for (std::uint8_t gx = 0; gx < w; gx++) {
            const std::uint8_t pixel = (byte >> (w - gx - 1)) & 1;

            if (pixel == 1) {
                draw_pixel(x + gx, y + gy, fg);
            } else {
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

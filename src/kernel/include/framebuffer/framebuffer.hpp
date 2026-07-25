#pragma once

#include <console/console.hpp>

#include <cstdint>

namespace framebuffer {
constexpr std::uint32_t RGB_BLACK = 0x00000000;
constexpr std::uint32_t RGB_RED = 0x00FF0000;
constexpr std::uint32_t RGB_GREEN = 0x0000FF00;
constexpr std::uint32_t RGB_BLUE = 0x000000FF;

constexpr std::size_t RGB_OFFB = 0;
constexpr std::size_t RGB_OFFG = 1;
constexpr std::size_t RGB_OFFR = 2;

struct FrameBufferInfo {
    std::uint64_t width;
    std::uint64_t height;
    std::uint64_t pitch;
    std::uint16_t bpp;
    std::uint8_t* vram;
};

void init(const FrameBufferInfo& info);

std::uint32_t get_screen_width();
std::uint32_t get_screen_height();

void invert_rec(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
void draw_line(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t color);
void outline_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color);
void fill_rect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color);
void draw_glyph(const std::uint32_t x, const std::uint32_t y, std::uint32_t w, std::uint32_t h, const std::uint8_t* glyph, std::uint32_t fg, std::uint32_t bg);

void clear_black();
void clear(std::uint32_t color);

void log();
}

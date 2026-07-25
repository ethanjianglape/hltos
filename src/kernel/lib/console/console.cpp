#include <console/ansi.hpp>
#include <console/console.hpp>
#include <console/font8x16.hpp>
#include <containers/kvector.hpp>
#include <framebuffer/framebuffer.hpp>
#include <log/log.hpp>
#include <timer/timer.hpp>

#include <cstddef>
#include <cstdint>

namespace console {
namespace fb = framebuffer;

static std::uint32_t cursor_col = 0;
static std::uint32_t cursor_row = 0;

static std::uint32_t current_fg = static_cast<std::uint32_t>(RgbColor::WHITE);
static std::uint32_t current_bg = static_cast<std::uint32_t>(RgbColor::BLACK);

static std::uint32_t prev_fg = current_fg;
static std::uint32_t prev_bg = current_bg;

static std::size_t screen_cols;
static std::size_t screen_rows;

static std::size_t viewport_offset;

static bool cursor_enabled = true;

static kvector<kvector<ConsoleChar>> buffer;

std::uint32_t get_cursor_pixel_x(int offset = 0)
{
    return (cursor_col + offset) * fonts::FONT_WIDTH;
}

std::uint32_t get_cursor_pixel_y(int offset = 0)
{
    return (cursor_row - viewport_offset + offset) * fonts::FONT_HEIGHT;
}

void ensure_valid_cursor_buffer_pos(std::size_t row, std::size_t col)
{
    while (row >= buffer.size()) {
        buffer.push_back({});
    }

    auto& line = buffer[row];

    while (col >= line.size()) {
        line.emplace_back(' ', current_fg, current_bg, true);
    }
}

bool cursor_within_viewport()
{
    return (cursor_row - viewport_offset) < screen_rows;
}

kvector<char> empty_line()
{
    return kvector<char>(screen_cols, ' ');
}

void init()
{
    cursor_col = 0;
    cursor_row = 0;
    viewport_offset = 0;

    screen_cols = fb::get_screen_width() / fonts::FONT_WIDTH;
    screen_rows = fb::get_screen_height() / fonts::FONT_HEIGHT;

    cursor_enabled = true;

    log::infof("console: {}x{} characters", screen_cols, screen_rows);
    log::infof("console: font {}x{} pixels", fonts::FONT_WIDTH, fonts::FONT_HEIGHT);
    log::infof("console: cursor set to ({}, {})", cursor_col, cursor_row);
}

void enable_cursor()
{
    if (!cursor_enabled) {
        cursor_enabled = true;
    }
}

void disable_cursor()
{
    if (cursor_enabled) {
        cursor_enabled = false;
    }
}

void draw_cursor()
{
    const std::uint32_t px = get_cursor_pixel_x();
    const std::uint32_t py = get_cursor_pixel_y();

    fb::invert_rec(px, py, fonts::FONT_WIDTH, fonts::FONT_HEIGHT);
}

void clear_to_eol()
{
    for (std::size_t col = cursor_col; col < screen_cols; col++) {
        ensure_valid_cursor_buffer_pos(cursor_row, col);
        buffer[cursor_row][col].c = ' ';
        buffer[cursor_row][col].dirty = true;
    }
}

void erase_in_line(std::size_t from, std::size_t to)
{
    for (std::size_t col = from; col < to; col++) {
        ensure_valid_cursor_buffer_pos(cursor_row, col);
        buffer[cursor_row][col].c = ' ';
        buffer[cursor_row][col].dirty = true;
    }
}

std::size_t get_cursor_x() { return cursor_col; }
std::size_t get_screen_cols() { return screen_cols; }
std::size_t get_screen_rows() { return screen_rows; }

void set_cursor_x(std::uint32_t x)
{
    set_cursor(x, cursor_row);
}

void set_cursor_y(std::uint32_t y)
{
    set_cursor(cursor_col, y);
}

void set_cursor(std::uint32_t col, std::uint32_t row)
{
    if (cursor_col == col && cursor_row == row) {
        return;
    }

    ensure_valid_cursor_buffer_pos(row, col);

    draw_cursor();

    cursor_col = col;
    cursor_row = row;

    if (cursor_col >= screen_cols) {
        newline();
    }

    if (cursor_row - viewport_offset >= screen_rows) {
        scroll();
    }

    draw_cursor();
}

void move_cursor(std::int32_t dx, std::int32_t dy)
{
    set_cursor(cursor_col + dx, cursor_row + dy);
}

void newline()
{
    buffer[cursor_row][cursor_col].dirty = true;
    set_cursor(0, cursor_row + 1);
    redraw();
}

void scroll_up()
{
    if (viewport_offset > 0) {
        viewport_offset--;
        redraw(true);
    }
}

void scroll_down()
{
    if (viewport_offset < cursor_row) {
        viewport_offset++;
        redraw(true);
    }
}

void scroll_into_view()
{
    if (cursor_within_viewport()) {
        return;
    }

    viewport_offset = cursor_row;
}

void scroll()
{
    viewport_offset++;
    redraw(true);
}

void backspace()
{
    if (cursor_col == 0) {
        return;
    }

    move_cursor(-1, 0);
}

void draw_character_at(char c, std::size_t row, std::size_t col, std::uint32_t fg, std::uint32_t bg)
{
    const std::uint32_t pixel_x = col * fonts::FONT_WIDTH;
    const std::uint32_t pixel_y = row * fonts::FONT_HEIGHT;

    if (c == ' ') {
        fb::fill_rect(pixel_x, pixel_y, fonts::FONT_WIDTH, fonts::FONT_HEIGHT, bg);
        return;
    }

    const std::uint8_t* glyph = fonts::get_glyph(c);

    fb::draw_glyph(pixel_x, pixel_y, fonts::FONT_WIDTH, fonts::FONT_HEIGHT, glyph, fg, bg);
}

int put(char c)
{
    if (c == '\n') {
        newline();
        return 1;
    }

    ensure_valid_cursor_buffer_pos(cursor_row, cursor_col);

    buffer[cursor_row][cursor_col].c = c;
    buffer[cursor_row][cursor_col].dirty = true;

    move_cursor(1, 0);

    return 1;
}

int put(kstring_view sv)
{
    for (std::size_t i = 0; i < sv.length(); i++) {
        put(sv[i]);
    }

    return 0;
}

int put(const kstring& str)
{
    auto begin = str.begin();
    auto end = str.end();

    while (begin != end) {
        char c = *begin;

        if (c == '\033') {
            begin += ansi::parse_ansi_escape(begin, end);
        } else {
            put(c);
            ++begin;
        }
    }

    return 0;
}

void set_color(std::uint32_t fg, std::uint32_t bg)
{
    prev_fg = current_fg;
    prev_bg = current_bg;

    current_fg = fg;
    current_bg = bg;
}

void set_color(RgbColor fg, RgbColor bg)
{
    set_color(static_cast<std::uint32_t>(fg), static_cast<std::uint32_t>(bg));
}

void reset_color()
{
    current_fg = prev_fg;
    current_bg = prev_bg;
}

std::uint32_t get_current_fg()
{
    return current_fg;
}

std::uint32_t get_current_bg()
{
    return current_bg;
}

void clear()
{
    fb::clear_black();
    set_cursor(0, cursor_row + 1);
    viewport_offset = cursor_row;
    redraw();
}

void redraw(bool draw_clean)
{
    if (draw_clean) {
        fb::clear(current_bg);
    }

    if (cursor_enabled && cursor_within_viewport()) {
        draw_cursor();
    }

    for (std::size_t row = viewport_offset; row < buffer.size(); row++) {
        kvector<ConsoleChar>& line = buffer[row];

        for (std::size_t col = 0; col < screen_cols; col++) {
            if (row - viewport_offset < screen_rows) {
                if (col < line.size()) {
                    ConsoleChar& cc = line[col];

                    if (draw_clean || cc.dirty) {
                        draw_character_at(cc.c, row - viewport_offset, col, cc.fg, cc.bg);
                    }

                    cc.dirty = false;
                } else {
                    draw_character_at(' ', row - viewport_offset, col, current_fg, current_bg);
                }
            }
        }
    }

    if (cursor_enabled && cursor_within_viewport()) {
        draw_cursor();
    }
}
}

#pragma once

#include <cstdint>
namespace gfx::fonts {

class Font {
private:
public:
    virtual int font_width() const = 0;
    virtual int font_height() const = 0;

    virtual const std::uint8_t* get_glyph(char c) const = 0;
};

}

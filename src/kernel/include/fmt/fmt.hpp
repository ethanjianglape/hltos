#pragma once

#include <concepts>
#include <containers/kstring.hpp>
#include <cstddef>
#include <cstdint>

namespace fmt {

template <typename T>
struct hex {
    T value;
};
template <typename T>
struct bin {
    T value;
};

template <typename T>
struct oct {
    T value;
};

template <typename T>
hex(T) -> hex<T>;

template <typename T>
bin(T) -> bin<T>;

template <typename T>
oct(T) -> oct<T>;

template <typename Ptr>
concept ptr_type = std::is_pointer_v<Ptr>;

constexpr char DEC_CHARS[] = "0123456789";
constexpr char HEX_CHARS[] = "0123456789ABCDEF";
constexpr char OCT_CHARS[] = "01234567";
constexpr char BIN_CHARS[] = "01";

enum class NumberFormat : std::uint32_t {
    DEC = 1,
    HEX = 2,
    BIN = 3,
    OCT = 4
};

inline bool is_numeric(char c)
{
    return c >= '0' && c <= '9';
}

inline bool is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

inline int number_format_divisor(NumberFormat format)
{
    switch (format) {
    case NumberFormat::DEC:
        return 10;
    case NumberFormat::HEX:
        return 16;
    case NumberFormat::OCT:
        return 8;
    case NumberFormat::BIN:
        return 2;
    default:
        return 10;
    }
}

inline std::size_t insert(char (&buffer)[128], std::size_t pos, std::size_t index, std::size_t num, char pad)
{
    if (pos >= index) {
        return 0;
    }

    std::size_t i = index - 1;

    while (true) {
        buffer[i + num] = buffer[i];

        if (i == pos) {
            break;
        }

        i--;
    }

    while (i < pos + num) {
        buffer[i++] = pad;
    }

    return num;
}

inline std::size_t pad_zero(char (&buffer)[128], std::size_t index, int min_len = 4)
{
    std::size_t len = index - 2;
    std::size_t zeros = (min_len - len) & (min_len - 1);

    return insert(buffer, 2, index, zeros, '0');
}

inline char number_format_char(int i, NumberFormat format)
{
    switch (format) {
    case NumberFormat::DEC:
        return DEC_CHARS[i];
    case NumberFormat::HEX:
        return HEX_CHARS[i];
    case NumberFormat::OCT:
        return OCT_CHARS[i];
    case NumberFormat::BIN:
        return BIN_CHARS[i];
    default:
        return ' ';
    }
}

inline const char* to_string(std::uintmax_t unum, char (&buffer)[128], NumberFormat format = NumberFormat::DEC)
{
    std::size_t index = 0;

    if (format == NumberFormat::HEX) {
        buffer[index++] = '0';
        buffer[index++] = 'x';
    } else if (format == NumberFormat::BIN) {
        buffer[index++] = '0';
        buffer[index++] = 'b';
    } else if (format == NumberFormat::OCT) {
        buffer[index++] = '0';
        buffer[index++] = 'o';
    }

    if (unum == 0) {
        buffer[index++] = '0';
    }

    const auto divisor = number_format_divisor(format);
    int ri = 0;
    char reverse[64];

    while (unum > 0) {
        const auto index = unum % divisor;
        const auto c = number_format_char(index, format);

        reverse[ri++] = c;
        unum /= divisor;
    }

    while (ri > 0) {
        buffer[index++] = reverse[--ri];
    }

    // Pad hex and bin formats with 0 so they fit within 4/8/16/32/64 bits
    if (format != NumberFormat::DEC) {
        index += pad_zero(buffer, index);
    }

    buffer[index] = '\0';

    return buffer;
}

inline const char* to_string(std::intmax_t num, char (&buffer)[128], NumberFormat format = NumberFormat::DEC)
{
    std::size_t index = 0;
    std::uintmax_t unum;

    if (num < 0) {
        buffer[index++] = '-';
        unum = -static_cast<std::uintmax_t>(num);
    } else {
        unum = num;
    }

    if (format == NumberFormat::HEX) {
        buffer[index++] = '0';
        buffer[index++] = 'x';
    } else if (format == NumberFormat::BIN) {
        buffer[index++] = '0';
        buffer[index++] = 'b';
    } else if (format == NumberFormat::OCT) {
        buffer[index++] = '0';
        buffer[index++] = 'o';
    }

    if (unum == 0) {
        buffer[index++] = '0';
    }

    const auto divisor = number_format_divisor(format);
    int ri = 0;
    char reverse[64];

    while (unum > 0) {
        const auto index = unum % divisor;
        const auto c = number_format_char(index, format);

        reverse[ri++] = c;
        unum /= divisor;
    }

    while (ri > 0) {
        buffer[index++] = reverse[--ri];
    }

    // Pad hex and bin formats with 0 so they fit within 4/8/16/32/64 bits
    if (format != NumberFormat::DEC) {
        index += pad_zero(buffer, index);
    }

    buffer[index] = '\0';

    return buffer;
}

inline const char* to_string(std::signed_integral auto num, char (&buffer)[128], NumberFormat format = NumberFormat::DEC)
{
    return to_string(static_cast<std::intmax_t>(num), buffer, format);
}

inline const char* to_string(std::unsigned_integral auto num, char (&buffer)[128], NumberFormat format = NumberFormat::DEC)
{
    return to_string(static_cast<std::uintmax_t>(num), buffer, format);
}

template <std::integral T>
inline const char* to_string(hex<T> h, char (&buffer)[128])
{
    return to_string(h.value, buffer, NumberFormat::HEX);
}

template <ptr_type T>
inline const char* to_string(hex<T> h, char (&buffer)[128])
{
    return to_string(reinterpret_cast<std::uintptr_t>(h.value), buffer, NumberFormat::HEX);
}

template <std::integral T>
inline const char* to_string(bin<T> h, char (&buffer)[128])
{
    return to_string(h.value, buffer, NumberFormat::BIN);
}

template <std::integral T>
inline const char* to_string(oct<T> h, char (&buffer)[128])
{
    return to_string(h.value, buffer, NumberFormat::OCT);
}

inline const char* to_string(ptr_type auto ptr, char (&buffer)[128])
{
    return to_string(reinterpret_cast<std::uintptr_t>(ptr), buffer, NumberFormat::HEX);
}

inline const char* to_string(const kstring& str, char (&)[128])
{
    return str.c_str();
}

inline const char* to_string(const char* str, char (&)[128])
{
    return str;
}

inline std::uintmax_t parse_uint(const char* str, std::size_t len, NumberFormat format = NumberFormat::DEC)
{
    const auto divisor = number_format_divisor(format);

    std::uintmax_t result = 0;

    for (std::size_t i = 0; i < len && str[i]; i++) {
        result = result * divisor + (str[i] - '0');
    }

    return result;
}

inline std::uintmax_t parse_uint(const kstring& str, NumberFormat format = NumberFormat::DEC)
{
    return parse_uint(str.c_str(), str.size(), format);
}

inline int parse_int(char c)
{
    return c - '0';
}

inline kstring sprintf(kstring_view format)
{
    return format;
}

template <typename T, typename... Rest>
inline kstring sprintf(kstring_view format, T&& first, Rest&&... rest)
{
    const auto pos = format.find("{}");

    if (pos == kstring_view::npos) {
        return format;
    }

    kstring str{};
    char buffer[128];

    str += format.substr(0, pos);
    str += to_string(first, buffer);
    str += sprintf(format.substr(pos + 2), rest...);

    return str;
}

}

#pragma once

#include <algo/algo.hpp>
#include <arch.hpp>
#include <arch/x64/drivers/serial/serial.hpp>
#include <containers/klist.hpp>
#include <containers/kstring.hpp>
#include <containers/kstring_view.hpp>
#include <containers/kvector.hpp>
#include <fmt/fmt.hpp>

#include <concepts>
#include <utility>

namespace serial = arch::drivers::serial;

namespace kprint_detail {

inline void print_one(const char* str)
{
    serial::puts(str);
}

inline void print_one(kstring_view sv)
{
    serial::puts(sv);
}

template <typename T>
inline void print_one(const klist<T>& list)
{
    serial::putchar('[');
    serial::puts(algo::join(list, ','));
    serial::putchar(']');
}

template <typename T>
inline void print_one(const kvector<T>& v)
{
    serial::putchar('[');
    serial::puts(algo::join(v, ','));
    serial::putchar(']');
}

inline void print_one(bool b)
{
    serial::puts(b ? "true" : "false");
}

inline void print_one(unsigned char* str)
{
    serial::puts(str);
}

inline void print_one(char c)
{
    serial::putchar(c);
}

inline void print_one(std::integral auto num)
{
    char buffer[128];
    serial::puts(fmt::to_string(num, buffer));
}

template <std::integral T>
inline void print_one(fmt::hex<T> h)
{
    char buffer[128];
    serial::puts(fmt::to_string(h, buffer));
}

template <fmt::ptr_type T>
inline void print_one(fmt::hex<T> h)
{
    char buffer[128];
    serial::puts(fmt::to_string(h, buffer));
}

template <std::integral T>
inline void print_one(fmt::bin<T> b)
{
    char buffer[128];
    serial::puts(fmt::to_string(b, buffer));
}

template <std::integral T>
inline void print_one(fmt::oct<T> o)
{
    char buffer[128];
    serial::puts(fmt::to_string(o, buffer));
}

inline void print_one(fmt::ptr_type auto ptr)
{
    char buffer[128];
    serial::puts(fmt::to_string(ptr, buffer));
}
}

template <typename... Args>
void kprint(Args... args)
{
    (kprint_detail::print_one(args), ...);
}

inline void kprintf(kstring_view format)
{
    serial::puts(format);
}

template <typename T, typename... Rest>
void kprintf(kstring_view format, T&& first, Rest&&... rest)
{
    const auto pos = format.find("{}");

    if (pos != kstring_view::npos) {
        kprint_detail::print_one(format.substr(0, pos));
        kprint_detail::print_one(first);

        kprintf(format.substr(pos + 2), std::forward<Rest>(rest)...);
    } else {
        kprint_detail::print_one(format);
    }
}

inline void kprintln()
{
    kprint_detail::print_one('\n');
}

template <typename... Args>
void kprintln(Args... args)
{
    (kprint_detail::print_one(args), ..., kprintln());
}

#pragma once

#include <containers/kstring.hpp>
#include <containers/kstring_view.hpp>
#include <kprint/kprint.hpp>

#include <utility>

namespace log {

inline void print_timestamp()
{
    const auto ns = arch::drivers::tsc::get_time_us();
    kprint("[t=", ns, "us] ");
}

template <typename... Ts>
void info(Ts... args)
{
    print_timestamp();
    kprintln("<INFO> ", args...);
}

inline void infof(kstring_view format)
{
    print_timestamp();
    kprint("<INFO> ");
    kprintln(format);
}

template <typename T, typename... Rest>
void infof(kstring_view format, T first, Rest... rest)
{
    print_timestamp();
    kprint("<INFO> ");
    kprintf(format, first, rest...);
    kprintln();
}

inline void init_start(const char* str)
{
    print_timestamp();
    kprintln("<INFO> ", str, " initializing...");
}

inline void init_end(const char* str)
{
    print_timestamp();
    kprintln("<INFO> ", str, " initialized!");
}

template <typename... Ts>
void warn(Ts... args)
{
    print_timestamp();
    kprintln("<WARN> ", args...);
}

template <typename... Ts>
void error(Ts... args)
{
    print_timestamp();
    kprintln("<ERROR> ", args...);
}

template <typename T, typename... Rest>
void errorf(kstring_view format, T&& first, Rest&&... rest)
{
    print_timestamp();
    kprint("<ERROR> ");
    kprintf(format, first, std::forward<Rest>(rest)...);
    kprintln();
}

template <typename... Ts>
void success(Ts... args)
{
    print_timestamp();
    kprintln("<SUCCESS> ", args...);
}

template <typename... Ts>
void debug(Ts... args)
{
    print_timestamp();
    kprintln("<DEBUG> ", args...);
}

inline void debugf(kstring_view format)
{
    print_timestamp();
    kprint("<DEBUG> ");
    kprint(format);
    kprintln();
}

template <typename T, typename... Rest>
void debugf(kstring_view format, T first, Rest... rest)
{
    print_timestamp();
    kprint("<DEBUG> ");
    kprintf(format, first, rest...);
    kprintln();
}

inline void section(const char* str)
{
    info("================================");
    info('=', str);
    info("================================");
}

}

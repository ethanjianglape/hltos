#pragma once

#include "fmt/fmt.hpp"
#include <concepts>
#include <containers/klist.hpp>
#include <containers/kstring.hpp>
#include <containers/kvector.hpp>

namespace algo {
// Splits a string into components around a delim.
// Removes empty values.
// ex: split("a::b:c:::d", ':') -> ["a", "b", "c", "d"]
kvector<kstring> split(kstring::const_iterator begin,
    kstring::const_iterator end,
    char delim = ' ');

kvector<kstring> split(kstring_view::const_iterator begin,
    kstring_view::const_iterator end,
    char delim = ' ');

// Splits a string into components around a delim.
// Keeps empty values.
// ex: split("a::b:c:::d", ':') -> ["a", "", "b", "c", "", "", "d"]
kvector<kstring> tokenize(kstring::const_iterator begin,
    kstring::const_iterator end,
    char delim = ' ');

kvector<kstring> split(const kstring& str, char delim = ' ');
kvector<kstring> split(kstring_view sv, char delim = ' ');

template <typename Iter>
kstring join(Iter begin,
    Iter end,
    char delim = ' ')
{
    kstring result;

    while (begin != end) {
        result += *begin;
        result += delim;
        ++begin;
    }

    if (!result.empty()) {
        result.pop_back();
    }

    return result;
}

template <typename T>
kstring join(const kvector<T>& v, char delim = ' ')
{
    return join(v.begin(), v.end(), delim);
}

template <typename T>
kstring join(const klist<T>& list, char delim = ' ')
{
    kstring result;

    for (std::size_t i = 0; i < list.size(); i++) {
        const T& item = list[i];
        char buffer[128];

        result += fmt::to_string(item, buffer);
        result.push_back(delim);
    }

    if (!result.empty()) {
        result.pop_back();
    }

    return result;
}

template <std::integral T>
T max(T a, T b)
{
    return a > b ? a : b;
}

template <std::integral T>
T min(T a, T b)
{
    return a < b ? a : b;
}

template <std::signed_integral T>
T abs(T t)
{
    return t < 0 ? -t : t;
}

auto abs(std::integral auto t)
{
    return t;
}

auto sqr(std::integral auto t)
{
    return t * t;
}

}

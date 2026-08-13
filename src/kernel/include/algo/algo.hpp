#pragma once

#include <concepts>
#include <containers/klist.hpp>
#include <containers/kstring.hpp>
#include <containers/kvector.hpp>
#include <fmt/fmt.hpp>

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

inline constexpr auto max(std::integral auto a, std::integral auto b)
{
    return a > b ? a : b;
}

inline constexpr auto min(std::integral auto a, std::integral auto b)
{
    return a < b ? a : b;
}

inline constexpr auto abs(std::signed_integral auto t)
{
    return t < 0 ? -t : t;
}

inline constexpr auto abs(std::unsigned_integral auto t)
{
    return t;
}

inline constexpr auto sqr(std::integral auto t)
{
    return t * t;
}

inline constexpr void swap(auto* a, auto* b)
{
    auto temp = *a;
    *a = *b;
    *b = temp;
}

}

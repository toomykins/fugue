// std::from_chars for double, with a strtod fallback where the library lacks it (Apple libc++ before LLVM 20).
#pragma once
#include <cerrno>
#include <charconv>
#include <clocale>
#include <cstdlib>
#include <string>
#include <system_error>
#include <version>

namespace fugue::render {

inline std::from_chars_result parseDouble(const char* first, const char* last, double& value) {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    return std::from_chars(first, last, value);
#else
    std::string s(first, last);
    const char point = *std::localeconv()->decimal_point;  // strtod follows the C locale's decimal point
    for (char& c : s)
        if (c == '.') c = point;
    errno = 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    std::from_chars_result r{first + (end - s.c_str()), std::errc()};
    if (end == s.c_str())
        r = {first, std::errc::invalid_argument};
    else if (errno == ERANGE)
        r.ec = std::errc::result_out_of_range;
    else
        value = v;
    return r;
#endif
}

}  // namespace fugue::render

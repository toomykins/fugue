// BYOND text-macro markers. No Qt.
#pragma once
#include <string>
#include <string_view>

namespace fugue::net {

// The text without BYOND's text-macro markers (`\improper` etc. are a 0xFF byte and a selector byte).
inline std::string plainText(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if ((unsigned char)s[i] == 0xff) {
            i++;
            continue;
        }
        o.push_back(s[i]);
    }
    return o;
}

}  // namespace fugue::net

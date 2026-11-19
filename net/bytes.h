// Small byte helpers shared by net/ and proto/. No Qt.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fugue {

using Bytes = std::vector<uint8_t>;
using ByteSpan = std::span<const uint8_t>;

inline std::string hexEncode(ByteSpan b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}

// Lowercase or uppercase hex, no separators. nullopt on odd length or a non-hex digit.
inline std::optional<Bytes> hexDecode(std::string_view s) {
    if (s.size() % 2) return std::nullopt;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Bytes out(s.size() / 2);
    for (size_t i = 0; i < out.size(); i++) {
        int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out[i] = uint8_t(hi << 4 | lo);
    }
    return out;
}

// hexDecode that throws std::invalid_argument on bad input, for callers with no recovery.
// Never dereference hexDecode()'s optional unchecked: that is undefined behaviour on nullopt.
inline Bytes hexBytes(std::string_view s) {
    auto b = hexDecode(s);
    if (!b) throw std::invalid_argument("bad hex");
    return std::move(*b);
}

// Wire scalars. Frame headers and the c2s seq are BIG-endian; payload scalars are LITTLE-endian.
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void putBe16(Bytes& o, uint16_t v) {
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v));
}
inline void putBe32(Bytes& o, uint32_t v) {
    putBe16(o, uint16_t(v >> 16));
    putBe16(o, uint16_t(v));
}
inline void putLe16(Bytes& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
inline void putLe32(Bytes& o, uint32_t v) {
    putLe16(o, uint16_t(v));
    putLe16(o, uint16_t(v >> 16));
}

}  // namespace fugue

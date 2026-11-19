// Builds a Windows bitmap font file (.FON) byte by byte, for tests of render/gdifon.h: version-3 FNT strikes in RT_FONT
// resources of a minimal NE executable. The glyphs are whatever `ink` says, so no real font's bitmaps are involved.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fontfile {

inline void put16(std::string& b, size_t o, unsigned v) {
    b[o] = char(v & 0xff);
    b[o + 1] = char((v >> 8) & 0xff);
}
inline void put32(std::string& b, size_t o, uint32_t v) {
    put16(b, o, v & 0xffff);
    put16(b, o + 2, v >> 16);
}

struct Strike {
    unsigned pt = 7, dpi = 96, h = 11, ascent = 9, il = 2;
    unsigned first = 32, last = 255, defaultChar = 128;  // defaultChar is absolute
    std::vector<unsigned> widths;                        // first..last
    std::function<bool(unsigned c, unsigned x, unsigned y)> ink = [](unsigned, unsigned, unsigned) { return true; };
};

inline std::string fnt(const Strike& s) {
    const size_t n = s.last - s.first + 1;
    std::string f(0x94 + n * 6, '\0');
    put16(f, 0, 0x300);
    put16(f, 0x44, s.pt);
    put16(f, 0x46, s.dpi);
    put16(f, 0x48, s.dpi);
    put16(f, 0x4a, s.ascent);
    put16(f, 0x4c, s.il);
    put16(f, 0x58, s.h);
    f[0x5f] = char(s.first);
    f[0x60] = char(s.last);
    f[0x61] = char(s.defaultChar - s.first);
    for (size_t i = 0; i < n; i++) {
        const unsigned c = s.first + unsigned(i), w = s.widths[i];
        put16(f, 0x94 + i * 6, w);
        put32(f, 0x94 + i * 6 + 2, uint32_t(f.size()));
        // Column-major: each 8-pixel-wide column is h bytes, one per row, the high bit leftmost.
        std::string bits(((w + 7) / 8) * s.h, '\0');
        for (unsigned y = 0; y < s.h; y++)
            for (unsigned x = 0; x < w; x++)
                if (s.ink(c, x, y))
                    bits[(x / 8) * s.h + y] = char(uint8_t(bits[(x / 8) * s.h + y]) | (0x80u >> (x % 8)));
        f += bits;
    }
    f.resize((f.size() + 15) / 16 * 16, '\0');
    put32(f, 2, uint32_t(f.size()));
    return f;
}

// An NE file holding `strikes` as RT_FONT resources, 16-byte aligned.
inline std::string fon(const std::vector<Strike>& strikes) {
    std::string b(0x100, '\0');
    b[0] = 'M';
    b[1] = 'Z';
    put16(b, 0x3c, 0x40);
    b[0x40] = 'N';
    b[0x41] = 'E';
    put16(b, 0x40 + 0x24, 0x40);  // resource table at 0x80
    put16(b, 0x80, 4);            // alignment shift
    put16(b, 0x82, 0x8008);
    put16(b, 0x84, unsigned(strikes.size()));
    std::string body;
    for (size_t i = 0; i < strikes.size(); i++) {
        const std::string f = fnt(strikes[i]);
        put16(b, 0x8a + i * 12, unsigned((b.size() + body.size()) >> 4));
        put16(b, 0x8c + i * 12, unsigned(f.size() >> 4));
        body += f;
    }
    return b + body;
}

}  // namespace fontfile

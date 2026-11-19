#include "render/pngwrite.h"

#include <array>
#include <algorithm>
#include <cstring>

namespace fugue::render {

namespace {

const std::array<uint32_t, 256>& crcTable() {
    static const std::array<uint32_t, 256> t = [] {
        std::array<uint32_t, 256> a{};
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            a[n] = c;
        }
        return a;
    }();
    return t;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
    for (size_t i = 0; i < n; i++) c = crcTable()[(c ^ p[i]) & 0xff] ^ (c >> 8);
    return c;
}

void be32(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(uint8_t(v >> 24));
    o.push_back(uint8_t(v >> 16));
    o.push_back(uint8_t(v >> 8));
    o.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
    be32(o, uint32_t(data.size()));
    size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    uint32_t c = crc32(&o[start], o.size() - start) ^ 0xffffffffu;
    be32(o, c);
}

}  // namespace

std::vector<uint8_t> encodePng(const RgbaImage& img) {
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(img.w));
    be32(ihdr, uint32_t(img.h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);

    std::vector<uint8_t> raw;
    raw.reserve(size_t(img.h) * (size_t(img.w) * 4 + 1));
    for (int y = 0; y < img.h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), img.px.begin() + size_t(y) * size_t(img.w) * 4,
                   img.px.begin() + (size_t(y) + 1) * size_t(img.w) * 4);
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    size_t pos = 0;
    do {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    be32(z, (b << 16) | a);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    return out;
}

}  // namespace fugue::render

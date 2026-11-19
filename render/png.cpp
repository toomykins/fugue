// Spec: PNG 1.2 sections 4 (chunks), 7 (filtering), 11 (critical/ancillary chunks). Where the spec leaves behaviour open
// (out-of-range palette index, tRNS longer than PLTE, tRNS on grayscale or truecolour) the choices are: an
// out-of-range index is opaque black, and tRNS on a palette image applies per entry (extending the palette
// with opaque black first).
#include "render/png.h"

#include <array>
#include <cstring>

#include "render/inflate.h"

namespace fugue::render {
namespace {

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

int paeth(int a, int b, int c) {
    int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

}  // namespace

PngStatus decodePng(const uint8_t* d, size_t n, RgbaImage& out, std::string& why) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (n < 8 || std::memcmp(d, sig, 8) != 0) {
        why = "not a PNG";
        return PngStatus::Malformed;
    }
    size_t p = 8;
    bool haveIhdr = false, haveIend = false, havePlte = false, haveTrns = false;
    uint32_t w = 0, h = 0;
    int depth = 0, ctype = 0, interlace = 0;
    std::array<std::array<uint8_t, 4>, 256> pal;  // RGBA: default opaque black
    for (auto& e : pal) e = {0, 0, 0, 255};
    size_t npal = 0;
    uint8_t trnsRaw[6] = {};
    bool useKey = false;
    std::vector<uint8_t> idat;
    bool seenIdat = false, idatEnded = false;
    while (p < n && !haveIend) {
        if (p + 8 > n) {
            why = "truncated chunk header";
            return PngStatus::Malformed;
        }
        uint32_t len = be32(d + p);
        const uint8_t* type = d + p + 4;
        if (len > 0x7fffffff || p + 12 + size_t(len) > n) {
            why = "truncated chunk";
            return PngStatus::Malformed;
        }
        const uint8_t* data = d + p + 8;
        if (crc32Ieee(d + p + 4, 4 + size_t(len)) != be32(data + len)) {
            why = "invalid checksum";
            return PngStatus::Malformed;
        }
        auto is = [&](const char* t) { return std::memcmp(type, t, 4) == 0; };
        if (!haveIhdr) {
            if (!is("IHDR") || len != 13) {
                why = "missing/bad IHDR";
                return PngStatus::Malformed;
            }
            w = be32(data);
            h = be32(data + 4);
            depth = data[8];
            ctype = data[9];
            if (data[10] != 0 || data[11] != 0) {
                why = "compression/filter method";
                return PngStatus::Unsupported;
            }
            interlace = data[12];
            if (interlace > 1) {
                why = "invalid interlace method";
                return PngStatus::Malformed;
            }
            if (w == 0 || h == 0 || w > 0x7fffffff || h > 0x7fffffff) {
                why = "non-positive dimension";
                return PngStatus::Malformed;
            }
            if (uint64_t(w) * h > (1ull << 28)) {
                why = "dimension overflow";
                return PngStatus::Unsupported;
            }
            bool okc = false;
            switch (ctype) {
                case 0: okc = depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16; break;
                case 2:
                case 4:
                case 6: okc = depth == 8 || depth == 16; break;
                case 3: okc = depth == 1 || depth == 2 || depth == 4 || depth == 8; break;
            }
            if (!okc) {
                why = "bad bit depth / colour type";
                return PngStatus::Malformed;
            }
            haveIhdr = true;
        } else if (is("PLTE")) {
            size_t np = len / 3;
            if (len % 3 || np == 0 || np > 256 || np > (size_t(1) << depth)) {
                why = "bad PLTE length";
                return PngStatus::Malformed;
            }
            if (ctype == 3) {
                for (size_t i = 0; i < np; i++) pal[i] = {data[3 * i], data[3 * i + 1], data[3 * i + 2], 255};
                npal = np;
                havePlte = true;
            } else if (ctype == 0 || ctype == 4) {
                why = "PLTE, colour type mismatch";
                return PngStatus::Malformed;
            }
        } else if (is("tRNS")) {
            if (ctype == 3) {
                if (len > 256 || !havePlte) {
                    why = "bad tRNS";
                    return PngStatus::Malformed;
                }
                for (size_t i = 0; i < len; i++) pal[i][3] = data[i];
                if (len > npal) npal = len;
                haveTrns = true;
            } else if (ctype == 0 && depth <= 8) {
                if (len != 2) {
                    why = "bad tRNS length";
                    return PngStatus::Malformed;
                }
                std::memcpy(trnsRaw, data, 2);
                useKey = true;
            } else if (ctype == 2 && depth == 8) {
                if (len != 6) {
                    why = "bad tRNS length";
                    return PngStatus::Malformed;
                }
                std::memcpy(trnsRaw, data, 6);
                useKey = true;
            } else if (ctype == 0 || ctype == 2) {
                why = "16-bit tRNS";
                return PngStatus::Unsupported;
            } else {
                why = "tRNS, colour type mismatch";
                return PngStatus::Malformed;
            }
        } else if (is("IDAT")) {
            if (idatEnded) {
                why = "IDAT after other chunk";
                return PngStatus::Malformed;
            }
            idat.insert(idat.end(), data, data + len);
            seenIdat = true;
        } else if (is("IEND")) {
            haveIend = true;
        }
        if (seenIdat && !is("IDAT")) idatEnded = true;
        p += 12 + size_t(len);
    }
    (void)haveTrns;
    if (!haveIhdr) {
        why = "missing IHDR";
        return PngStatus::Malformed;
    }
    if (!haveIend) {
        why = "unexpected EOF before IEND";
        return PngStatus::Malformed;
    }
    if (!seenIdat) {
        why = "missing IDAT";
        return PngStatus::Malformed;
    }
    if (ctype == 3 && !havePlte) {
        why = "missing PLTE";
        return PngStatus::Malformed;
    }
    if (depth == 16) {
        why = "16-bit PNG";
        return PngStatus::Unsupported;
    }
    if (interlace) {
        why = "Adam7 interlace";
        return PngStatus::Unsupported;
    }

    int bpp;  // bits per pixel
    switch (ctype) {
        case 0: bpp = depth; break;
        case 2: bpp = 24; break;
        case 3: bpp = depth; break;
        case 4: bpp = 16; break;
        default: bpp = 32; break;
    }
    size_t bytesPerPixel = size_t(bpp + 7) / 8;
    size_t rowBytes = (uint64_t(bpp) * w + 7) / 8;
    size_t expect = (rowBytes + 1) * h;
    std::vector<uint8_t> raw;
    raw.reserve(expect);
    size_t used = 0;
    ZStatus zs = zlibInflate(idat.data(), idat.size(), raw, expect, &used);
    if (zs == ZStatus::Limit) {
        why = "too much pixel data";
        return PngStatus::Malformed;
    }
    if (zs != ZStatus::Ok) {
        why = "bad zlib stream";
        return PngStatus::Malformed;
    }
    if (raw.size() < expect) {
        why = "not enough pixel data";
        return PngStatus::Malformed;
    }
    if (raw.size() > expect || used != idat.size()) {
        why = "too much pixel data";
        return PngStatus::Malformed;
    }

    // Unfilter in place.
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = raw.data() + size_t(y) * (rowBytes + 1);
        uint8_t ft = row[0];
        uint8_t* cur = row + 1;
        const uint8_t* prev = y ? row - (rowBytes + 1) + 1 : nullptr;
        for (size_t i = 0; i < rowBytes; i++) {
            int a = i >= bytesPerPixel ? cur[i - bytesPerPixel] : 0;
            int b = prev ? prev[i] : 0;
            int c = (prev && i >= bytesPerPixel) ? prev[i - bytesPerPixel] : 0;
            switch (ft) {
                case 0: break;
                case 1: cur[i] = uint8_t(cur[i] + a); break;
                case 2: cur[i] = uint8_t(cur[i] + b); break;
                case 3: cur[i] = uint8_t(cur[i] + (a + b) / 2); break;
                case 4: cur[i] = uint8_t(cur[i] + paeth(a, b, c)); break;
                default: why = "bad filter type"; return PngStatus::Malformed;
            }
        }
    }

    out.w = int(w);
    out.h = int(h);
    out.px.assign(size_t(w) * h * 4, 0);
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* c = raw.data() + size_t(y) * (rowBytes + 1) + 1;
        uint8_t* o = out.px.data() + size_t(y) * w * 4;
        for (uint32_t x = 0; x < w; x++, o += 4) {
            switch (ctype) {
                case 0: {
                    unsigned v;
                    if (depth == 8)
                        v = c[x];
                    else {
                        int per = 8 / depth;
                        unsigned bits = (c[x / per] >> (8 - depth * (1 + int(x % per)))) & ((1u << depth) - 1);
                        v = bits * (255u / ((1u << depth) - 1));  // 1: *0xff, 2: *0x55, 4: *0x11
                    }
                    o[0] = o[1] = o[2] = uint8_t(v);
                    o[3] = (useKey && v == trnsRaw[1] * (depth == 8 ? 1u : 255u / ((1u << depth) - 1))) ? 0 : 255;
                    break;
                }
                case 2: {
                    o[0] = c[3 * x];
                    o[1] = c[3 * x + 1];
                    o[2] = c[3 * x + 2];
                    o[3] = (useKey && o[0] == trnsRaw[1] && o[1] == trnsRaw[3] && o[2] == trnsRaw[5]) ? 0 : 255;
                    break;
                }
                case 3: {
                    unsigned idx;
                    if (depth == 8)
                        idx = c[x];
                    else {
                        int per = 8 / depth;
                        idx = (c[x / per] >> (8 - depth * (1 + int(x % per)))) & ((1u << depth) - 1);
                    }
                    std::memcpy(o, pal[idx].data(), 4);
                    break;
                }
                case 4:
                    o[0] = o[1] = o[2] = c[2 * x];
                    o[3] = c[2 * x + 1];
                    break;
                default: std::memcpy(o, c + 4 * x, 4); break;
            }
        }
    }
    return PngStatus::Ok;
}

}  // namespace fugue::render

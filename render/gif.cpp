// Spec: GIF89a (see gif.h).
#include "render/gif.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace fugue::assets {
namespace {

struct Rd {
    const uint8_t* d;
    size_t n, p = 0;
    bool ok = true;
    int u8() {
        if (p >= n) {
            ok = false;
            return 0;
        }
        return d[p++];
    }
    int u16() {
        int a = u8();
        int b = u8();
        return a | (b << 8);
    }
    bool skip(size_t k) {
        if (p + k > n) {
            ok = false;
            return false;
        }
        p += k;
        return true;
    }
};

using Pal = std::vector<std::array<uint8_t, 4>>;

bool readTable(Rd& r, int fields, Pal& pal) {
    size_t cnt = size_t(1) << ((fields & 7) + 1);
    if (r.p + cnt * 3 > r.n) return false;
    pal.clear();
    for (size_t i = 0; i < cnt; i++) {
        pal.push_back({r.d[r.p], r.d[r.p + 1], r.d[r.p + 2], 255});
        r.p += 3;
    }
    return true;
}

// Reads sub-blocks to the zero terminator; appends payload to `out` if non-null.
bool subBlocks(Rd& r, std::vector<uint8_t>* out) {
    for (;;) {
        int len = r.u8();
        if (!r.ok) return false;
        if (len == 0) return true;
        if (r.p + size_t(len) > r.n) return false;
        if (out) out->insert(out->end(), r.d + r.p, r.d + r.p + len);
        r.p += size_t(len);
    }
}

// GIF LZW (LSB-first codes, no early change). Returns false unless exactly npix indices are produced.
bool lzw(const std::vector<uint8_t>& in, int litWidth, size_t npix, std::vector<uint8_t>& out) {
    const int clear = 1 << litWidth, eoi = clear + 1;
    struct Ent {
        int16_t prefix;
        uint8_t ch, first;
    };
    std::vector<Ent> tab(4096);
    for (int i = 0; i < clear; i++) tab[size_t(i)] = {-1, uint8_t(i), uint8_t(i)};
    int next = eoi + 1, width = litWidth + 1, prev = -1;
    uint32_t acc = 0;
    int nb = 0;
    size_t ip = 0;
    out.clear();
    out.reserve(npix);
    std::vector<uint8_t> tmp;
    for (;;) {
        while (nb < width) {
            if (ip >= in.size()) return false;  // stream ended without an end-of-information code
            acc |= uint32_t(in[ip++]) << nb;
            nb += 8;
        }
        int code = int(acc & ((1u << width) - 1));
        acc >>= width;
        nb -= width;
        if (code == clear) {
            next = eoi + 1;
            width = litWidth + 1;
            prev = -1;
            continue;
        }
        if (code == eoi) break;
        if (prev < 0) {
            if (code >= clear) return false;  // the first code after a clear must be a literal
        } else {
            if (code > next) return false;
            if (next < 4096) {
                uint8_t fc = code == next ? tab[size_t(prev)].first : tab[size_t(code)].first;
                tab[size_t(next)] = {int16_t(prev), fc, tab[size_t(prev)].first};
                next++;
                if (next == (1 << width) && width < 12) width++;
            } else if (code == next) {
                return false;
            }
        }
        tmp.clear();
        for (int c = code; c >= 0; c = tab[size_t(c)].prefix) tmp.push_back(tab[size_t(c)].ch);
        for (size_t i = tmp.size(); i-- > 0;) out.push_back(tmp[i]);
        if (out.size() > npix) return false;
        prev = code;
    }
    return out.size() == npix;
}

// One decoded image block: its rectangle, palette indices (de-interlaced) and the graphic control that preceded it.
struct RawFrame {
    int left = 0, top = 0, w = 0, h = 0;
    const Pal* pal = nullptr;  // transparent index already applied
    const std::vector<uint8_t>* idx = nullptr;
    bool hasT = false;
    int disposal = 0;  // GCE bits 2-4
    int delayCs = 0;   // GCE delay, 1/100 s
};

struct Screen {
    int w = 0, h = 0, bgIndex = 0;
    bool haveGlobal = false;
    Pal global;
    int loopExt = -1;  // NETSCAPE2.0 loop count, -1 = no such extension
};

// Walks the whole stream; `onFrame` sees every image block in order. A bad block anywhere rejects the file.
// false + why on any malformed stream.
template <class F>
bool walk(const uint8_t* d, size_t n, Screen& sc, std::string& why, F&& onFrame) {
    Rd r{d, n};
    if (n > 128 * 1024 * 1024) {
        why = "input size cap";
        return false;
    }
    uint64_t decodedPixels = 0;
    unsigned frameCount = 0;
    if (n < 6 || !(std::string((const char*)d, 6) == "GIF87a" || std::string((const char*)d, 6) == "GIF89a")) {
        why = "bad header";
        return false;
    }
    r.p = 6;
    sc.w = r.u16();
    sc.h = r.u16();
    int gfields = r.u8();
    sc.bgIndex = r.u8();
    r.u8();
    if (!r.ok) {
        why = "truncated header";
        return false;
    }
    if (uint64_t(sc.w) * sc.h > 16 * 1024 * 1024 || sc.w > 8192 || sc.h > 8192) {
        why = "screen size cap";
        return false;
    }
    sc.haveGlobal = (gfields & 0x80) != 0;
    if (sc.haveGlobal && !readTable(r, gfields, sc.global)) {
        why = "truncated colour table";
        return false;
    }
    bool hasT = false;
    int tIndex = 0, disposal = 0, delay = 0;
    bool any = false;
    for (;;) {
        if (r.p >= n) {
            why = "missing trailer";
            return false;
        }
        int c = r.u8();
        if (c == 0x3b) break;
        if (c == 0x21) {
            int label = r.u8();
            if (label == 0xf9) {
                int sz = r.u8();
                if (sz != 4) {
                    why = "bad graphic control size";
                    return false;
                }
                int flags = r.u8();
                delay = r.u16();
                int ti = r.u8();
                int term = r.u8();
                if (!r.ok || term != 0) {
                    why = "bad graphic control";
                    return false;
                }
                disposal = (flags >> 2) & 7;
                if (flags & 1) {
                    hasT = true;
                    tIndex = ti;
                }
            } else if (label == 0xff) {
                // application extension: NETSCAPE2.0 sub-block [1][loop lo][loop hi] is the loop count
                std::vector<uint8_t> app;
                int idLen = r.u8();
                if (!r.ok || r.p + size_t(idLen) > n) {
                    why = "truncated extension";
                    return false;
                }
                bool netscape = idLen == 11 && std::memcmp(d + r.p, "NETSCAPE2.0", 11) == 0;
                r.p += size_t(idLen);
                if (!subBlocks(r, &app)) {
                    why = "truncated extension";
                    return false;
                }
                if (netscape && app.size() >= 3 && app[0] == 1) sc.loopExt = app[1] | (app[2] << 8);
            } else if (!subBlocks(r, nullptr)) {
                why = "truncated extension";
                return false;
            }
            continue;
        }
        if (c != 0x2c) {
            why = "unknown block type";
            return false;
        }
        int left = r.u16();
        int top = r.u16();
        int w = r.u16(), h = r.u16(), ifields = r.u8();
        if (!r.ok) {
            why = "truncated image descriptor";
            return false;
        }
        Pal pal;
        bool local = (ifields & 0x80) != 0;
        if (local) {
            if (!readTable(r, ifields, pal)) {
                why = "truncated colour table";
                return false;
            }
        } else if (!sc.haveGlobal) {
            why = "no colour table";
            return false;
        } else
            pal = sc.global;
        if (hasT) {
            if (size_t(tIndex) < pal.size())
                pal[size_t(tIndex)] = {0, 0, 0, 0};
            else {
                size_t old = pal.size();
                pal.resize(size_t(tIndex) + 1);
                for (size_t i = old; i < pal.size(); i++) pal[i] = {0, 0, 0, 0};
            }
        }
        int lit = r.u8();
        if (!r.ok || lit < 2 || lit > 8) {
            why = "bad LZW literal width";
            return false;
        }
        std::vector<uint8_t> blob;
        if (!subBlocks(r, &blob)) {
            why = "truncated image data";
            return false;
        }
        size_t npix = size_t(w) * size_t(h);
        decodedPixels += npix;
        // The frames' own pixels are bounded like a composited animation (render/dmi.cpp): 64 million.
        if (!npix || w > 8192 || h > 8192 || decodedPixels > 64 * 1024 * 1024 || ++frameCount > 65536) {
            why = "frame work cap";
            return false;
        }
        if (disposal > 3) {
            why = "unsupported disposal";
            return false;
        }
        std::vector<uint8_t> idx;
        if (!lzw(blob, lit, npix, idx)) {
            why = "bad LZW data";
            return false;
        }
        for (uint8_t v : idx)
            if (size_t(v) >= pal.size()) {
                why = "invalid pixel value";
                return false;
            }
        if (ifields & 0x40) {  // interlace: rows arrive as passes 0:8, 4:8, 2:4, 1:2
            std::vector<uint8_t> t(idx.size());
            size_t src = 0;
            static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
            for (int ps = 0; ps < 4; ps++)
                for (int y = start[ps]; y < h; y += step[ps], src++)
                    for (int x = 0; x < w; x++) t[size_t(y) * size_t(w) + size_t(x)] = idx[src * size_t(w) + size_t(x)];
            idx.swap(t);
        }
        any = true;
        RawFrame f{left, top, w, h, &pal, &idx, hasT, disposal, delay};
        if (!onFrame(f)) {
            why = "frame rejected";
            return false;
        }
        hasT = false;
        disposal = 0;
        delay = 0;
    }
    if (!any) {
        why = "no frames";
        return false;
    }
    return true;
}

}  // namespace

bool decodeGifFirstFrame(const uint8_t* d, size_t n, RgbaImage& out, std::string& why) {
    Screen sc;
    bool first = true;
    return walk(d, n, sc, why, [&](const RawFrame& f) {
        if (first) {
            first = false;
            size_t npix = size_t(f.w) * size_t(f.h);
            out.w = f.w;
            out.h = f.h;
            out.px.resize(npix * 4);
            for (size_t i = 0; i < npix; i++)
                for (int k = 0; k < 4; k++) out.px[i * 4 + size_t(k)] = (*f.pal)[(*f.idx)[i]][size_t(k)];
        }
        return true;
    });
}

bool decodeGifAnimation(const uint8_t* d, size_t n, GifAnimation& out, std::string& why, size_t maxBytes) {
    Screen sc;
    out = GifAnimation{};
    std::vector<uint8_t> canvas, saved;
    bool sized = false;
    bool tooBig = false;
    bool ok = walk(d, n, sc, why, [&](const RawFrame& f) {
        if (!sized) {
            sized = true;
            // The icon is the LOGICAL SCREEN, not the first frame: a 16x16 first frame on a 32x32 screen is a 32x32 icon,
            // transparent outside the frame. A zero screen falls back to the first frame's own extent.
            out.w = sc.w > 0 ? sc.w : f.left + f.w;
            out.h = sc.h > 0 ? sc.h : f.top + f.h;
            if (out.w <= 0 || out.h <= 0 || out.w > 8192 || out.h > 8192) return false;
            if (uint64_t(out.w) * out.h > maxBytes / 4) {
                tooBig = true;
                return false;
            }
            canvas.assign(size_t(out.w) * size_t(out.h) * 4, 0);  // the canvas starts transparent
        }
        const size_t frameBytes = canvas.size();
        if ((size_t(out.frames) + 1) * frameBytes > maxBytes) {
            tooBig = true;
            return false;
        }
        if (f.disposal == 3) saved = canvas;
        // Draw: a transparent pixel leaves the canvas as it is; clipped to the screen.
        for (int y = 0; y < f.h; y++) {
            int cy = f.top + y;
            if (cy < 0 || cy >= out.h) continue;
            for (int x = 0; x < f.w; x++) {
                int cx = f.left + x;
                if (cx < 0 || cx >= out.w) continue;
                const auto& c = (*f.pal)[(*f.idx)[size_t(y) * size_t(f.w) + size_t(x)]];
                if (c[3] == 0) continue;
                std::memcpy(&canvas[(size_t(cy) * size_t(out.w) + size_t(cx)) * 4], c.data(), 4);
            }
        }
        out.strip.px.insert(out.strip.px.end(), canvas.begin(), canvas.end());
        out.frames++;
        // BYOND's icon delay is in deciseconds, with the fraction kept and a floor of 1 tick.
        out.delays.push_back(std::max(1.0, double(f.delayCs) / 10.0));
        // disposal, applied before the next frame draws
        if (f.disposal == 2) {
            // Restore-to-background clears the frame's rectangle to transparent when that frame has a transparent index
            // (whatever its value); otherwise to the global table's background colour, opaque.
            std::array<uint8_t, 4> bg{0, 0, 0, 0};
            if (!f.hasT && sc.haveGlobal && size_t(sc.bgIndex) < sc.global.size()) bg = sc.global[size_t(sc.bgIndex)];
            for (int y = std::max(0, f.top); y < std::min(out.h, f.top + f.h); y++)
                for (int x = std::max(0, f.left); x < std::min(out.w, f.left + f.w); x++)
                    std::memcpy(&canvas[(size_t(y) * size_t(out.w) + size_t(x)) * 4], bg.data(), 4);
        } else if (f.disposal == 3 && !saved.empty()) {
            canvas.swap(saved);  // restore to previous
        }
        return true;
    });
    if (tooBig) {
        why = "animation exceeds the size cap";
        return false;
    }
    if (!ok) return false;
    out.strip.w = out.w;
    out.strip.h = out.h * out.frames;
    // Loop: no NETSCAPE2.0 extension plays once (loop 1); its count n is n+1 plays; 0 is forever.
    out.loop = sc.loopExt < 0 ? 1 : sc.loopExt == 0 ? 0 : sc.loopExt + 1;
    return true;
}

}  // namespace fugue::assets

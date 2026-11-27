#include "render/gdifon.h"

#include <algorithm>

namespace fugue::render {
namespace {

struct Reader {
    std::string_view d;
    bool ok = true;
    unsigned u8(size_t o) {
        if (o >= d.size()) {
            ok = false;
            return 0;
        }
        return uint8_t(d[o]);
    }
    unsigned u16(size_t o) { return u8(o) | u8(o + 1) << 8; }
    uint32_t u32(size_t o) { return u16(o) | uint32_t(u16(o + 2)) << 16; }
};

// One FNT resource at `f` (`len` bytes); false for a vector font, a strike for another resolution than 96 or 120 dpi
// (Windows has a file of each for its bitmap faces) or a malformed one.
bool parseStrike(Reader& r, size_t f, size_t len, FonStrike& s) {
    const unsigned ver = r.u16(f);
    if ((ver != 0x200 && ver != 0x300) || (r.u16(f + 0x42) & 1)) return false;
    const unsigned dpi = r.u16(f + 0x46);
    if ((dpi != 96 && dpi != 120) || r.u16(f + 0x48) != dpi) return false;
    s.pt = int(r.u16(f + 0x44));
    s.ascent = int(r.u16(f + 0x4a));
    s.il = int(r.u16(f + 0x4c));
    s.h = int(r.u16(f + 0x58));
    s.first = int(r.u8(f + 0x5f));
    s.last = int(r.u8(f + 0x60));
    s.defaultChar = s.first + int(r.u8(f + 0x61));
    if (!r.ok || s.h <= 0 || s.h > 255 || s.last < s.first) return false;
    const size_t table = f + (ver == 0x200 ? 0x76 : 0x94), step = ver == 0x200 ? 4 : 6;
    for (int c = s.first; c <= s.last; c++) {
        const size_t e = table + size_t(c - s.first) * step;
        FonStrike::Glyph g;
        g.w = int(r.u16(e));
        const size_t off = ver == 0x200 ? r.u16(e + 2) : r.u32(e + 2);
        const size_t cols = (size_t(g.w) + 7) / 8;
        if (!r.ok || g.w > 255 || off + cols * size_t(s.h) > len) return false;
        g.bits.assign(size_t(g.w) * size_t(s.h), 0);
        // Column-major: each 8-pixel-wide column is h bytes, one per row, the high bit leftmost.
        for (int y = 0; y < s.h; y++)
            for (int x = 0; x < g.w; x++)
                if (r.u8(f + off + size_t(x / 8) * size_t(s.h) + size_t(y)) & (0x80u >> (x % 8)))
                    g.bits[size_t(y) * size_t(g.w) + size_t(x)] = 1;
        s.glyphs.push_back(std::move(g));
    }
    return r.ok;
}

}  // namespace

std::vector<FonStrike> parseFon(std::string_view file) {
    Reader r{file};
    std::vector<FonStrike> out;
    if (r.u16(0) != 0x5a4d) return out;
    const size_t ne = r.u16(0x3c);
    if (r.u16(ne) != 0x454e) return out;
    const size_t rt = ne + r.u16(ne + 0x24);
    const unsigned shift = r.u16(rt);
    if (!r.ok || shift > 16) return out;
    for (size_t p = rt + 2; r.ok;) {
        const unsigned type = r.u16(p);
        if (type == 0) break;
        const unsigned n = r.u16(p + 2);
        p += 8;
        for (unsigned i = 0; i < n && r.ok; i++, p += 12) {
            if (type != 0x8008) continue;  // RT_FONT
            const size_t f = size_t(r.u16(p)) << shift, len = size_t(r.u16(p + 2)) << shift;
            FonStrike s;
            if (f + len <= file.size() && parseStrike(r, f, len, s)) out.push_back(std::move(s));
            r.ok = true;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const FonStrike& a, const FonStrike& b) { return a.h < b.h; });
    return out;
}

bool fonGlyph(const std::vector<FonStrike>& strikes, const DsFace& face, char32_t cp, GlyphMask& out) {
    if (strikes.empty()) return false;
    // A byte the strike lacks is its default char. Every advance must be the one the layout placed the glyph with, or
    // glyphs would overlap or leave gaps. Bytes 0x7f to 0x9f are left out: Windows' own files have no glyphs for most
    // of them and Wine's have, so a host's file may differ from the tables there; such a byte is checked by itself.
    const FonStrike* use = nullptr;
    auto glyphOf = [&](int c) {
        if (c < std::max(use->first, 32) || c > use->last) c = use->defaultChar;
        return c >= use->first && c <= use->last ? &use->glyphs[size_t(c - use->first)] : nullptr;
    };
    auto adv = [&](int c) {
        const FonStrike::Glyph* g = glyphOf(c);
        return g ? g->w : -1;
    };
    // The strike the layout measured, by its cell and its character height. Several can be of one height: Windows'
    // smalle.fon has two 7pt strikes, and the 18pt one of sserife.fon is as tall as the 14pt one of sseriff.fon.
    const int charHeight = dsBitmapCharHeight(face.family, face.em);
    for (const FonStrike& c : strikes) {
        if (use || c.h != face.height || c.ascent != face.ascent || c.h - c.il != charHeight) continue;
        use = &c;
        bool same = (use->defaultChar >= 0x7f && use->defaultChar <= 0x9f) || adv(-1) == dsBitmapAdvance(face, -1);
        for (int b = 32; b <= 255 && same; b++)
            if ((b < 0x7f || b > 0x9f) && adv(b) != dsBitmapAdvance(face, b)) same = false;
        if (!same) use = nullptr;
    }
    if (!use) return false;
    const FonStrike* s = use;
    const int byte = cp1252Byte(cp);
    if ((byte < 32 || (byte >= 0x7f && byte <= 0x9f)) && adv(byte) != dsBitmapAdvance(face, byte)) return false;
    const FonStrike::Glyph* gp = glyphOf(byte);
    if (!gp || gp->w <= 0) return false;
    const FonStrike::Glyph& g = *gp;
    // Bold overstrikes 1 px to the right. Italic is GDI's slant of the bitmap: the cell's top two rows move right by
    // half its height less one, every two rows below them by a pixel less, the rows under the baseline too (GDI's own
    // picture of every strike; stock 516.1689 on Windows 11).
    const int bold = face.bold ? 1 : 0;
    const auto slant = [&](int y) { return face.italic ? (s->h - 1) / 2 - y / 2 : 0; };
    out.w = g.w + bold + slant(0);
    out.h = s->h;
    out.left = 0;
    out.top = 0;
    out.a.assign(size_t(out.w) * size_t(out.h), 0);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < g.w; x++) {
            if (!g.bits[size_t(y) * size_t(g.w) + size_t(x)]) continue;
            for (int b = 0; b <= bold; b++) out.a[size_t(y) * size_t(out.w) + size_t(x + b + slant(y))] = 255;
        }
    return true;
}

}  // namespace fugue::render

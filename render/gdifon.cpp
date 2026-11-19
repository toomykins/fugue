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

// One FNT resource at `f` (`len` bytes); false for a vector font, a non-96-dpi strike or a malformed one.
bool parseStrike(Reader& r, size_t f, size_t len, FonStrike& s) {
    const unsigned ver = r.u16(f);
    if ((ver != 0x200 && ver != 0x300) || (r.u16(f + 0x42) & 1)) return false;
    if (r.u16(f + 0x46) != 96 || r.u16(f + 0x48) != 96) return false;
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
    // GDI font mapping: the largest strike whose character height fits the request, else the smallest; then integer scaling.
    const FonStrike* s = &strikes.front();
    for (const FonStrike& c : strikes)
        if (c.h - c.il <= face.em) s = &c;
    const int k = std::max(1, (face.em + 2 * s->il) / s->h);
    if (s->h * k != face.height || s->ascent * k != face.ascent) return false;
    // A byte the strike lacks is its default char. Every advance must be the one the layout placed the glyph with, or
    // glyphs would overlap or leave gaps.
    auto glyphOf = [&](int c) {
        if (c < std::max(s->first, 32) || c > s->last) c = s->defaultChar;
        return c >= s->first && c <= s->last ? &s->glyphs[size_t(c - s->first)] : nullptr;
    };
    auto adv = [&](int c) {
        const FonStrike::Glyph* g = glyphOf(c);
        return g ? g->w : -1;
    };
    if (adv(-1) != dsBitmapAdvance(face, -1)) return false;
    for (int c = 32; c <= 255; c++)
        if (adv(c) != dsBitmapAdvance(face, c)) return false;
    const FonStrike::Glyph* gp = glyphOf(cp1252Byte(cp));
    if (!gp || gp->w <= 0) return false;
    const FonStrike::Glyph& g = *gp;
    // The strike is drawn unscaled at the line top even when scaled metrics place it; bold overstrikes 1 px to the
    // right, italic draws 1 px to the left.
    const int bold = face.bold ? 1 : 0;
    out.w = g.w + bold;
    out.h = s->h;
    out.left = face.italic ? -1 : 0;
    out.top = 0;
    out.a.assign(size_t(out.w) * size_t(out.h), 0);
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < g.w; x++) {
            if (!g.bits[size_t(y) * size_t(g.w) + size_t(x)]) continue;
            for (int b = 0; b <= bold; b++) out.a[size_t(y) * size_t(out.w) + size_t(x + b)] = 255;
        }
    return true;
}

}  // namespace fugue::render

#include "render/font.h"

#include <vector>

namespace fugue::render {
namespace {
#include "render/gofont_adv.inc"

struct Tables {
    std::vector<int> start;  // cumulative index of each range's first rune in a row
    GoFace faces[kGoFontNumFaces][kGoFontMaxSize - kGoFontMinSize + 1];
    Tables() {
        int n = 0;
        for (auto& rg : kGoFontRanges) {
            start.push_back(n);
            n += int(rg[1] - rg[0]) + 1;
        }
        for (int f = 0; f < kGoFontNumFaces; f++)
            for (int s = 0; s <= kGoFontMaxSize - kGoFontMinSize; s++) {
                int row = f * (kGoFontMaxSize - kGoFontMinSize + 1) + s;
                GoFace& g = faces[f][s];
                g.ascent = kGoFontMeta[row][0];
                g.descent = kGoFontMeta[row][1];
                g.height = g.ascent + g.descent;
                g.space = kGoFontMeta[row][2];
                g.row = row;
            }
    }
};
const Tables& tables() {
    static const Tables t;
    return t;
}
}  // namespace

DecodedRune decodeRune(std::string_view s, size_t i) {
    const DecodedRune bad{0xFFFD, 1};
    size_t n = s.size() - i;
    if (n < 1) return {0xFFFD, 0};
    auto b = [&](size_t k) { return static_cast<unsigned char>(s[i + k]); };
    unsigned char c = b(0);
    if (c < 0x80) return {c, 1};
    if (c < 0xC2 || c > 0xF4) return bad;
    auto cont = [&](size_t k, unsigned lo = 0x80, unsigned hi = 0xBF) { return b(k) >= lo && b(k) <= hi; };
    if (c < 0xE0) {
        if (n < 2 || !cont(1)) return bad;
        return {char32_t(((c & 0x1F) << 6) | (b(1) & 0x3F)), 2};
    }
    if (c < 0xF0) {
        unsigned lo = c == 0xE0 ? 0xA0 : 0x80, hi = c == 0xED ? 0x9F : 0xBF;
        if (n < 3 || !cont(1, lo, hi) || !cont(2)) return bad;
        return {char32_t(((c & 0x0F) << 12) | ((b(1) & 0x3F) << 6) | (b(2) & 0x3F)), 3};
    }
    unsigned lo = c == 0xF0 ? 0x90 : 0x80, hi = c == 0xF4 ? 0x8F : 0xBF;
    if (n < 4 || !cont(1, lo, hi) || !cont(2) || !cont(3)) return bad;
    return {char32_t(((c & 0x07) << 18) | ((b(1) & 0x3F) << 12) | ((b(2) & 0x3F) << 6) | (b(3) & 0x3F)), 4};
}

int GoFace::advance(char32_t r) const {
    const Tables& t = tables();
    // binary search the range containing r
    size_t lo = 0, hi = t.start.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (kGoFontRanges[mid][1] < r)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= t.start.size() || r < kGoFontRanges[lo][0]) return space;
    size_t idx = size_t(row) * kGoFontNumRunes + size_t(t.start[lo]) + (r - kGoFontRanges[lo][0]);
    return kGoFontAdv[idx] - '#';
}

int GoFace::width(std::string_view s) const {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        DecodedRune d = decodeRune(s, i);
        w += advance(d.r);
        i += size_t(d.size);
    }
    return w;
}

std::string_view GoFace::clip(std::string_view s, int maxW) const {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        DecodedRune d = decodeRune(s, i);
        w += advance(d.r);
        if (w > maxW) return s.substr(0, i);
        i += size_t(d.size);
    }
    return s;
}

const GoFace& goFace(int size, bool bold, bool italic, bool mono) {
    if (size == 0) size = 13;
    if (size < kFontMinSize) size = kFontMinSize;
    if (size > kFontMaxSize) size = kFontMaxSize;
    int f = mono ? 4 : (bold ? 1 : 0) | (italic ? 2 : 0);
    return tables().faces[f][size - kFontMinSize];
}

}  // namespace fugue::render

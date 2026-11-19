// font: the client's rasterized-face metrics (ascent, descent, per-rune advance) as a lookup table.
//
// Advances come from a generated table of the Go fonts at each size (render/gofont_adv.inc; advance = the 26.6
// fixed-point advance, rounded). Runes outside the table advance like U+0020. These are not DreamSeeker's metrics; see
// dsmaptext.h for those.
#pragma once
#include <cstddef>
#include <string_view>

namespace fugue::render {

// UTF-8 decode: an invalid byte yields (0xFFFD, 1).
struct DecodedRune {
    char32_t r;
    int size;
};
DecodedRune decodeRune(std::string_view s, size_t i);

struct GoFace {
    int ascent = 0, descent = 0, height = 0;
    int space = 0;  // advance of U+0020: what an uncovered rune advances by
    int row = 0;    // internal table row
    int advance(char32_t r) const;
    // Sum of advances over s, decoded rune by rune.
    int width(std::string_view s) const;
    // The longest prefix (cut at a rune start) whose running width never exceeds maxW.
    std::string_view clip(std::string_view s, int maxW) const;
};

// Face selection for the maptext styles: size is clamped to kFontMinSize..kFontMaxSize (0 = 13, the UI size).
// mono wins over bold/italic (Mono ships regular only).
const GoFace& goFace(int size, bool bold, bool italic, bool mono);

constexpr int kFontMinSize = 6;
constexpr int kFontMaxSize = 64;

}  // namespace fugue::render

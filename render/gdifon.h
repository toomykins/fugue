// gdifon: glyph bitmaps read from a Windows bitmap font file (.FON; NE executable of FNT raster strikes) for drawing
// maptext in DreamSeeker's bitmap faces. No font file is shipped; the host supplies one (shell/ttmetrics.h).
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "render/maptextpaint.h"

namespace fugue::render {

struct FonStrike {
    int pt = 0, h = 0, ascent = 0, il = 0;     // dfPoints, dfPixHeight, dfAscent, dfInternalLeading
    int first = 0, last = 0, defaultChar = 0;  // byte range; defaultChar is absolute
    struct Glyph {
        int w = 0;
        std::vector<uint8_t> bits;
    };  // h rows of w pixels, 1 = ink
    std::vector<Glyph> glyphs;  // first..last
};

// The file's raster strikes for 96 or 120 dpi, by increasing height; empty when `file` is not a readable .FON.
std::vector<FonStrike> parseFon(std::string_view file);

// The glyph DreamSeeker draws for `face`, from the strike the layout measured it with (dsmaptext's choice of GDI's),
// with bitmap bold's 1 px overstrike. false when no strike given has that cell, character height and every advance,
// so glyphs match the positions.
bool fonGlyph(const std::vector<FonStrike>& strikes, const DsFace& face, char32_t cp, GlyphMask& out);

}  // namespace fugue::render

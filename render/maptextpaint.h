// maptextpaint: draws atom.maptext from the render/dsmaptext.h layout, the same one the C2S::MeasureText reply
// uses, so a box the server sizes from that reply holds the lines drawn.
//
// A runechat is a client.image whose whole visible content is its maptext.
//
// PLACEMENT (matches stock DreamSeeker): the box is
// maptext_width x maptext_height, anchored at the atom's bottom-left like a sprite and moved +maptext_x right,
// +maptext_y up; the text block's LAST line sits flush with the box's bottom, the block grows upward and is clipped at
// the box's top. Text wraps at the box width. Lines stack exactly as dsMeasureText adds heights. A left-aligned line
// the box cannot hold whole is not drawn at all; a centred or right-aligned one is clipped.
// PAINT: colour defaults to the sheet's `body` colour (skin/liveskin.h folds in the map's text-color), else white.
// `text-align` is per line (center = floor((box - w) / 2), right = box - w), w being the line's advances plus its last
// glyph's overhang. Bitmap italic never inks the box's first column. `-dm-text-outline` and `text-shadow` are stock's
// outline and drop-shadow filters run on the drawn text; they reach past the box.
//
// Glyphs come from a rasterizer the host installs (setGlyphRasterizer; the app's is shell/ttmetrics.h, on Qt's font stack),
// the same split as dsmaptext's TrueType metrics, so no Qt Gui dependency reaches render/. DreamSeeker's faces are GDI
// bitmap fonts ('Small Fonts', 'MS Sans Serif') that this client does not ship; the rasterizer draws them from the
// host's font file when it has one (render/gdifon.h), else from a stand-in face at the measured em. Positions and line
// breaks follow DreamSeeker's layout. Without a rasterizer
// nothing is drawn (and nothing fails).
#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "render/dsmaptext.h"
#include "render/ir.h"

namespace fugue::render {

// One glyph's coverage: w x h alpha bytes whose top-left sits at (pen x + left, face top + top), the face top being
// baseline - DsFace::ascent.
struct GlyphMask {
    int w = 0, h = 0;
    int left = 0, top = 0;
    std::vector<uint8_t> a;
};
using GlyphRasterizer = std::function<bool(const DsFace& face, char32_t cp, GlyphMask& out)>;
// Set once at start-up (or by a test); clears the glyph and box caches.
void setGlyphRasterizer(GlyphRasterizer r);

// The maptext box as a straight-alpha RGBA sprite, or nullptr when nothing visible would be drawn (no text, no glyphs,
// no rasterizer, or an empty box). `mapStyle` is the default map control's `style` (the same sheet MeasureText merges
// in). The sprite is the box with `pad` px round it, as far as an outline or shadow reaches past the box (0 without).
SpritePtr paintMaptext(std::string_view markup, std::string_view mapStyle, int boxW, int boxH, int* pad = nullptr);

}  // namespace fugue::render

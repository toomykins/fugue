// dsmaptext: client.MeasureText() answered the way stock DreamSeeker 516 answers it (S2C::MeasureText /
// C2S::MeasureText).
//
// What DreamSeeker does:
//   * Text is laid out with GDI fonts: CreateFont(lfHeight = -em), then the face's per-character advance widths
//     (GetTextExtentPoint: a plain sum, no kerning) and GetTextMetrics' tmHeight/tmAscent. A CSS size N (px is treated
//     as pt) is rounded to integer pt, em = round(N*4/3), except that pt 9 measures as em 13 (it is drawn at em 12).
//   * Faces: no font-family (or `sans-serif`) gives "MS Sans Serif" 10pt, a bitmap font (sserife.fon: 8/10/12pt).
//     "Small Fonts" has one 7pt strike, so under the Wine profile every size up to 13pt measures alike. `monospace`, <tt>, <code>
//     and <pre> give the "Courier" bitmap. Any other family is that TrueType face; an unknown one falls back to Arial.
//     Under the Wine profile a bitmap face is integer-scaled above its largest strike (k = (em + 2*il) / tmHeight). On
//     Windows it never is: a size is drawn from the nearest strike as GDI's mapper weighs them, and far above the
//     largest one from Arial.
//   * Lines: tmHeight for the last line, and floor(line-height * tmAscent) between lines. line-height defaults to 1.2
//     and accepts only a number or a percentage; `normal` or a length stacks every line at pitch 0. Bold on a bitmap
//     face adds 1 px per character; italic adds nothing.
//   * Width: with a width argument (> 0) the reply's width is that argument; with 0, it is the widest line (a trailing
//     run of n spaces counts n-1 of them). Wrapping is greedy at spaces, tabs and &nbsp;, and after '-' (not '/'). A
//     word wider than the whole width is cut by character.
//   * The style sheet is the default map control's `style` followed by the call's own `style`: `.class` rules apply in
//     stylesheet order, and `body` styles the base. The first declaration of a property
//     wins; `font:` sets family, size, weight and style together, defaulting what it omits.
//   * "" gives DreamSeeker's "cannot measure" (the server resumes with null); whitespace or tags only gives height 0.
//
// Two profiles, because Windows and wine ship different bitmap fonts. The Windows profile (the default) has the strikes
// of Windows' own files, those digitised for 120 dpi included (render/gdifont_windows.inc); the Wine profile has only
// Wine's (Small Fonts 7pt, MS Sans Serif 8, 10 and 12pt, Courier 10pt).
//
// Not modelled: lines that mix font sizes, <IMG> (measures 0), <h1>-<h6>, text-indent, padding and letter-spacing.
// TrueType faces need setTrueTypeResolver(); without one they measure as MS Sans Serif.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "render/maptext.h"  // MeasureTextAnswer, MeasureTextRequest

namespace fugue::render {

enum class GdiProfile { Windows, Wine };

struct DsMeasure {
    bool ok = false;  // false = DreamSeeker's "cannot measure" (the reply's 0xffff/0xffff, DM null)
    int width = 0, height = 0;
};

// The DreamSeeker answer for MeasureText(text, style, width) given the default map control's `style` param.
DsMeasure dsMeasureText(std::string_view text, std::string_view mapStyle, std::string_view style, int width,
                        GdiProfile profile = GdiProfile::Windows);
// Reduced to the wire's u16 pair: not ok, or an overflow of either u16, is 0xffff/0xffff.
MeasureTextAnswer dsMeasureTextAnswer(std::string_view text, std::string_view mapStyle, std::string_view style,
                                      int width, GdiProfile profile = GdiProfile::Windows);

// TrueType faces (Arial, Verdana, ...) come from the host's font stack. A resolver fills GDI's numbers for `family` at
// lfHeight -emPx: tmAscent/tmDescent (GDI reads them from the font's VDMX table when it has one) and the per-character
// advance. false = the family is not available (the measurer then asks for "Arial").
// The advance function is only ever called while the measurer holds its lock. Set once at start-up (shell/ttmetrics.h).
struct TrueTypeFace {
    int ascent = 0, descent = 0;
    std::function<int(char32_t)> advance;
    std::function<int(char32_t)> overhang;  // ink right of the advance (GDI's -C when C < 0, else 0); may be empty
};
using TrueTypeResolver =
    std::function<bool(const std::string& family, int emPx, bool bold, bool italic, TrueTypeFace& out)>;
void setTrueTypeResolver(TrueTypeResolver r);

// Unicode -> cp1252 byte; -1 = not in the code page.
int cp1252Byte(char32_t cp);

// THE SAME LAYOUT, for DRAWING maptext (render/maptextpaint.h; one layout drives both
// the drawing and the MeasureText reply, so a runechat box the server sized from our reply holds the lines we
// draw). Lines stack from the block's top (y 0) as the measured height adds up; the block's height is the measure,
// except that pt 9 is laid out at its drawn em 12.
// Colour, text-align, valign, underline, text-shadow and -dm-text-outline are read here (they never change a measurement) by applyProps.
struct DsFace {
    std::string family;  // "ms sans serif" / "small fonts" / "courier" (a bitmap face) or the TrueType family used
    int em = 0;          // the GDI request, px (CreateFont(-em))
    bool bold = false, italic = false;
    bool bitmap = false;
    int ascent = 0, height = 0;  // GDI tmAscent / tmHeight, px
};
struct DsPaint {
    uint32_t color = 0xffffffffu;  // 0xRRGGBBAA
    bool outline = false;
    uint32_t outlineColor = 0x000000ffu;
    int outlineWidth = 1;  // px
    bool underline = false;
    bool shadow = false;  // text-shadow: an offset copy of the text and its outline, under them
    int shadowX = 0, shadowY = 0, shadowBlur = 0;
    uint32_t shadowColor = 0x000000ffu;
    int outlineFlags = 0;  // the outline filter's: 1 sharp, 2 square
};
// An underlined cell (spaces included): `w` px from pen position `x`, in the cell's face and paint.
struct DsUnderline {
    int x = 0, w = 0, face = 0, paint = 0;
};
struct DsGlyph {
    char32_t cp = 0;
    int x = 0;
    int face = 0;
    int paint = 0;
    bool joined = false;  // follows a glyph of its own run of linked glyphs (dsmaptext.cpp): GDI drew them as one
};  // spaces and tabs only advance: not listed
// align 0 left 1 center 2 right; `overhang` px (last glyph's ink past its advance) count toward a centred/right line's width.
struct DsLine {
    int top = 0, ascent = 0, height = 0, width = 0, overhang = 0, align = 0;
    std::vector<DsGlyph> glyphs;
    std::vector<DsUnderline> underlines;
};
struct DsLayout {
    bool ok = false;  // false: nothing to draw ("" or whitespace/tags only)
    int width = 0, height = 0;
    int valign = 2;  // where the block sits in its box (its first glyph's valign): 0 top, 1 middle, 2 bottom
    std::vector<DsLine> lines;
    std::vector<DsFace> faces;
    std::vector<DsPaint> paints;
};
DsLayout dsLayoutMaptext(std::string_view text, std::string_view mapStyle, int width,
                         GdiProfile profile = GdiProfile::Windows);

// The unscaled advance of cp1252 byte `b` (b < 0: the default char) in `face`'s strike; -1 for TrueType or out of range.
int dsBitmapAdvance(const DsFace& face, int b, GdiProfile profile = GdiProfile::Windows);

// The character height (tmHeight - tmInternalLeading) of the strike GDI picks for a bitmap family at CreateFont(-em); 0 otherwise.
int dsBitmapCharHeight(std::string_view family, int em, GdiProfile profile = GdiProfile::Windows);
// That bitmap face at CreateFont(-em), and its pen advance for `cp` (strike width, scaled, plus bold's 1 px).
DsFace dsBitmapFace(std::string_view family, int em, bool bold, GdiProfile profile = GdiProfile::Windows);
int dsBitmapAdvancePx(const DsFace& face, char32_t cp, GdiProfile profile = GdiProfile::Windows);

// The C2S::MeasureText reply body [w u16][h u16][id u32] for a decoded request, laid out against the default map's
// style.
std::string dsMeasureTextReplyBody(const MeasureTextRequest& req, std::string_view mapStyle,
                                   GdiProfile profile = GdiProfile::Windows);

}  // namespace fugue::render

// ttmetrics: the TrueType half of render/dsmaptext.h (MeasureText), on Qt's font stack.
//
// Stock DreamSeeker measures a TrueType family with GDI: CreateFont(lfHeight = -em), per-character advances, and
// tmAscent and tmDescent. The advances match the hinted advances QFontMetrics::horizontalAdvance returns for the same
// font file at pixel size em, and for the Microsoft core fonts GDI's C width matches QFontMetrics::rightBearing (the
// overhang an aligned line counts; metric-compatible stand-ins differ for a few glyphs). tmAscent and tmDescent come from the font's VDMX table for that ppem, which Qt does not
// use, so they are read from the table here, falling back to Qt's ascent and descent when there is none.
// A family the host lacks returns false, and dsmaptext then asks for Arial, as stock does when a family is missing.
// Liberation Sans, Serif and Mono stand in for Arial, Times New Roman and Courier New. They have the same advances
// but no VDMX table, so on a Liberation-only host the MeasureText width matches stock and the height may be a pixel
// short.
#pragma once
#include <cstdint>

class QByteArray;

namespace fugue::shell {

// A font file among the world's resources (a .ttf or .otf): stock loads those for the session, and maptext can name
// their families (stock 516.1689: tgstation's lobby text is its own 'Grand9K Pixel'). false when `file` is no font or
// was added before. Any thread.
bool addResourceFont(const QByteArray& file);
// Grows with every font added: text drawn before may have used a stand-in face.
uint64_t resourceFontGeneration();

// Installs the resolver into render::setTrueTypeResolver. Needs a QGuiApplication; call once after constructing it.
void installTrueTypeMetrics();

// Installs render::setGlyphRasterizer, which supplies the glyph shapes for atom.maptext (render/maptextpaint.h).
// Positions and line breaks come from dsmaptext's layout, not from Qt. TrueType families are drawn antialiased.
// Stock bitmap faces ('Small Fonts', 'MS Sans Serif', 'Courier') are not shipped here. They are drawn from the host's
// own .FON file for the face (smalle.fon, sserife.fon, coure.fon), looked for in $FUGUE_FONT_DIR, ~/.fugue/fonts,
// Windows' font folder and Wine's font folders, when it has the strike the layout used with the same advance widths.
// Otherwise they are drawn without antialiasing in the host's face of that name if present, else its sans-serif or
// monospace face.
void installMaptextGlyphs();

}  // namespace fugue::shell

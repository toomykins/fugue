// Pixel operations of the compositor: per-layer colour (tint, alpha, colour matrix) and the blend modes.
// Straight-alpha RGBA8 throughout; float64 arithmetic evaluated in a fixed operand order (render/CMakeLists.txt
// compiles this module with -ffp-contract=off).
//
// blendModeOfWire maps the wire atom.blend_mode byte to a BlendMode.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace fugue::render {

// ColorMatrix: 5 rows x 4 columns, row-major; rows 0-3 are input R,G,B,A, row 4 is the constant offset.
// Values are UNIT-SCALED (1.0 == a full channel); the executor multiplies them by 255.
using ColorMatrix = std::array<double, 20>;
using ColorMatrixPtr = std::shared_ptr<const ColorMatrix>;

// BlendMode. The WIRE blend byte of atom.blend_mode shares these numbers for 0..5 (BLEND_DEFAULT 0,
// OVERLAY 1, ADD 2, SUBTRACT 3, MULTIPLY 4, INSET_OVERLAY 5); it is NOT dm's /icon.Blend IconBlendMode (ADD 0,
// SUBTRACT 1, MULTIPLY 2, OVERLAY 3, ...), which is a different enumeration entirely.
enum BlendMode : uint8_t {
    kBlendDefault = 0,
    kBlendOver = 1,
    kBlendAdd = 2,
    kBlendSubtract = 3,
    kBlendMultiply = 4,
    kBlendInset = 5
};
// An unknown wire value falls back to the default.
inline uint8_t blendModeOfWire(int b) { return (b >= 1 && b <= 5) ? uint8_t(b) : uint8_t(0); }

struct Rgba8 {
    uint8_t r, g, b, a;
};

// clamp8: uint8(v+0.5) for 0 < v < 255.
uint8_t clamp8(double v);
// overPx: straight-alpha over, float64 (oa <= 0 -> the zero pixel).
Rgba8 overPx(Rgba8 s, Rgba8 d);

// The per-layer colour state resolved before a layer is blended: tint, alpha, then the colour matrix. `cm` may be null.
struct LayerFx {
    bool hasTint = false;
    uint8_t tr = 255, tg = 255, tb = 255;
    int alpha = 255;
    const ColorMatrix* cm = nullptr;
};

// blendLayer: composites `src` (sw x sh, resolved through `fx` per pixel) into `dst` (dw x dh) at (offX, offY)
// under `mode`, restricted to destination rows [y0, y1) (y1 < 0 = all). Includes the two exact shortcuts for the
// default mode and the "transparent source leaves a channelwise destination unchanged" rule.
void blendLayer(uint8_t* dst, int dw, int dh, const uint8_t* src, int sw, int sh, int offX, int offY, const LayerFx& fx,
                int mode, int y0 = 0, int y1 = -1, bool planeSurface = false);
// planeSurface: the source is a finished plane buffer (premultiplied), so
// BLEND_MULTIPLY uses blendMulPlane; an object sprite drawn into a plane (the default) is straight - blendSubMul.

// Applies a colour matrix over npix pixels in place.
void applyColorMatrixInPlace(uint8_t* px, size_t npix, const ColorMatrix& m);
// Colours the pixels with r, g, b and alpha = clamp(alpha, 0, 255); a master's "#rrggbb" has no alpha of its own.
void applyColorInPlace(uint8_t* px, size_t npix, uint8_t r, uint8_t g, uint8_t b, int alpha);

// Test-only hooks; 0 is the shipped behaviour.
void setPixopsMutation(int m);
int pixopsMutation();

}  // namespace fugue::render

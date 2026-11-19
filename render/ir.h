// Draw-list IR and its CPU executor. The assembler (scene.cpp) resolves every world-dependent decision into a
// DrawList; the executor is a pure function of the list.
//
// Order is data: planes ascend, layers inside a plane are in draw order (the assembler owns the sort), and HUD
// passes are in client.screen arrival order.
//
//   DrawList
//     outW x outH       the map control's pixel size
//     planes[]          ascending plane id. A PlanePass with a master composites its layers onto a fresh transparent
//                       surface, which is then blended over the frame; one without a master has no surface of its own
//                       and blends its layers straight onto the frame (masterless_test.cpp). One surface per mastered
//                       plane matters: flattening per plane rounds differently from compositing every layer onto the frame.
//                       A PLANE_MASTER turns the finished surface into an off-screen slate or a filtered, coloured,
//                       blended plane (group -> filters -> colour -> blend), in two passes: pass 1 fills the
//                       render-target slates, pass 2 builds the scene.
//     hud[]             HudPass, after all planes. Each composites onto a transparent canvas that holds the whole
//                       object (its screen_loc cell joined with every layer's rectangle), then blits it at every dest.
//                       The map assembler does not emit it: screen objects are placed on their planes.
//     groundBlack       the final flatten onto the opaque black ground, so alpha is 255 everywhere.
//
//   Layer: a borrowed immutable straight-alpha sprite (or a RELAY naming a slate), a top-left (x,y) that may be
//   negative or run off the edge (the executor clips), a tint, an alpha, an optional colour matrix and a blend mode.
//
// Rounding (see executor.cpp):
//   1. tint/alpha: clamp8(float64(c) * (mul/255.0)), with clamp8(v) = uint8(v + 0.5) for 0 < v < 255.
//   2. layer over surface: float64 straight-alpha over, with the sa==0 / sa==255 shortcuts of blendLayer (pixops.cpp).
//   3. blit Over: 16-bit premultiplied arithmetic (HUD tiles, and the final black flatten).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/pixops.h"  // ColorMatrix, BlendMode
#include "render/png.h"     // RgbaImage

namespace fugue::render {

// A sprite: an immutable w x h straight-alpha RGBA8 cell, shared between every layer that draws it.
struct Sprite {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w*h*4
};
using SpritePtr = std::shared_ptr<const Sprite>;

struct DrawLayer {
    SpritePtr sprite;
    int x = 0, y = 0;  // top-left in the pass surface, px, +y down
    bool hasTint = false;
    uint8_t tintR = 255, tintG = 255, tintB = 255;
    int alpha = 255;  // 0..255 opacity; 0 draws nothing
    // Applied per source pixel, in this order: tint+alpha, then the colour matrix; then the layer is combined
    // with the surface under `blend` (a BlendMode; 0/1 = straight alpha-over).
    uint8_t blend = 0;
    ColorMatrixPtr cmat;
    // A RELAY (atom.render_source) has no sprite: its pixels are the render-target slate of this name, resolved when
    // the plane is composited. The slate's bottom edge sits at relayBottomY. An unknown name draws nothing.
    std::string relay;
    int relayBottomY = 0;
};

// Filters of a plane master, in the order the master lists them. Blur, outline, drop_shadow, an RGB colour matrix and
// an alpha mask by render_source are built; every other type is counted in PlaneMasterSpec::unappliedFilters and not
// drawn.
struct FilterSpec {
    enum Kind : uint8_t { Blur, Outline, DropShadow, ColorMatrix, AlphaMask } kind = Blur;
    double size = 0;
    double offset = 0;  // drop_shadow: grows the silhouette before the blur
    uint8_t r = 0, g = 0, b = 0, alpha = 255;
    uint16_t flags = 0;   // outline: OUTLINE_SHARP | OUTLINE_SQUARE; alpha mask: MASK_INVERSE | MASK_SWAP
    int x = 0, y = 0;     // drop_shadow / alpha mask offset, IMAGE space (+y down)
    ColorMatrixPtr cmat;  // ColorMatrix
    std::string source;   // AlphaMask: the render_target slate used as the mask
};

// A PLANE_MASTER: its own icon never draws; it names how its plane's finished surface composites.
//   group (the plane's layers + the master's overlays) -> filters -> colour -> blend     (the stock order)
// colour is the colour MATRIX when there is one (the scalar is then a legacy sentinel and is ignored), else the
// scalar tint+alpha.
struct PlaneMasterSpec {
    bool hasColor = false;  // scalar "#rrggbb" + alpha
    uint8_t r = 255, g = 255, b = 255;
    int alpha = 255;
    ColorMatrixPtr cmat;
    uint8_t blend = 0;         // how the finished surface composites onto the scene
    std::string renderTarget;  // "" = none; "*name" fills the slate and is NOT drawn on the map
    std::vector<FilterSpec> filters;
    int unappliedFilters = 0;
    // PlaneMaster.Matrix / HasMatrix: the master's `transform` (DM matrix(a,b,c,d,e,f) cells), applied AFTER
    // filters and colour: the linear part about the viewport centre, bilinear, then translate (+c, -f) px.
    bool hasXform = false;
    std::array<float, 6> xf{1, 0, 0, 0, 1, 0};
};

struct PlanePass {
    double plane = 0;                               // diagnostic: the resolved plane id this pass represents
    std::vector<DrawLayer> layers;                  // draw order, first = bottom (relays and master overlays included)
    std::shared_ptr<const PlaneMasterSpec> master;  // null = no master: the plane draws straight onto the frame
};

struct HudPass {
    int tileW = 0, tileH = 0;       // the canvas size
    std::vector<DrawLayer> layers;  // draw order, coordinates inside the canvas
    struct Dest {
        int x, y;
    };  // top-left of the canvas in the frame
    std::vector<Dest> dests;
};

struct HitScene;  // render/hittest.h

struct DrawList {
    int outW = 0, outH = 0;
    std::vector<PlanePass> planes;
    std::vector<HudPass> hud;
    bool groundBlack = true;
    // OPTIONAL sidecar for the mouse hit test (render/hittest.h). The executor never reads it, so output is identical
    // with or without it.
    std::shared_ptr<const HitScene> hit;
};

// What the plane pipeline did with its render targets and filters, for tests and diagnostics.
struct ExecStats {
    int slates = 0;            // planes whose master declared a render_target (pass 1)
    int offScreenPlanes = 0;   // of those, the "*" ones that are not drawn on the map
    int relays = 0;            // relay layers that resolved to a slate
    int unresolvedRelays = 0;  // relay layers whose slate did not exist when they were resolved
    int unresolvedMasks = 0;   // alpha-mask filters whose slate did not exist (the mask is then empty)
    int unappliedFilters = 0;
};

// Executes a list into straight-alpha RGBA8 (w*h*4, no padding).
RgbaImage execute(const DrawList& dl, ExecStats* stats = nullptr);
// True when execute() would produce the same pixels for both lists: every field it reads is compared (sprites and
// colour matrices by value, since the assembler builds fresh ones every frame). `hit` is not compared. A field added
// to the IR must be added here, or a change to it alone would leave a stale frame on screen.
bool sameDrawList(const DrawList& a, const DrawList& b);
// Compares a plane's own inputs by value, with the same immutable-pointer fast paths as sameDrawList.
// Does not resolve relay names: callers caching pixels must also compare resolved source dependencies and dimensions.
bool samePlanePass(const PlanePass& a, const PlanePass& b);

// Test-only hooks; 0 is the shipped behaviour.
void setExecutorMutation(int m);
void setPipelineMutation(int m);

// ---- building blocks, exposed for the unit tests ------------------------------------------------
// blendLayer for the default blend: composites the layer (tint/alpha applied per pixel) into
// `dst` (dw x dh) at the layer's (x,y).
void blendLayerOver(uint8_t* dst, int dw, int dh, const DrawLayer& l);
// blitOver: src (sw x sh), anchored at sp, into rect r of dst.
void blitOver(uint8_t* dst, int dw, int dh, int rx0, int ry0, int rx1, int ry1, const uint8_t* src, int sw, int sh,
              int spx, int spy);

}  // namespace fugue::render

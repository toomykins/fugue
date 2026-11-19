// The mouse hit test: viewport pixel -> the ATOM under the cursor, and that atom's mouse-command naming (over-ref, loc,
// icon-x/y, screen-loc).
//
// The hit test reads the same placed layers the compositor draws. scene.cpp's Assembler records, in draw order, every
// sprite it placed together with the atom that owns it (HitScene::world), and the HUD passes with their screen_loc
// destinations. Nothing here re-derives a position, plane or layer, so the hit test cannot drift from the pixels.
// Collection is additive: the DrawList the executor consumes is byte-identical with or without it.
//
// Rules, as stock DreamSeeker 516 applies them:
//   mouse_opacity 0   the atom is skipped entirely, overlays included (an overlay's own value is ignored).
//   mouse_opacity 1   (default) a target only where the source sprite pixel's alpha is not 0. atom.alpha, colour and
//                     tint are not part of the test.
//   mouse_opacity 2   a target over the sprite's whole rectangle, transparent pixels included.
//   draw order        topmost drawn wins: planes ascending, then the draw key (drawKey, per layer), then creation order
//                     (the order of HitScene::world). An overlay answers for its parent atom.
//   HUD               client.screen layers are hit-tested in the order they are drawn, each overlay by its own plane and
//                     key, then client.screen order (later on top). A screen layer's key sits above every map layer of
//                     its plane and band, whatever the layers, and below every map layer of a higher plane or band. So a
//                     mouse_opacity 2 HUD
//                     item takes its whole rectangle over the slot background beneath it, and over the map beneath its
//                     plane. The full-screen click catcher (mouse_opacity 2 on plane -99) answers only where no map layer
//                     of plane >= -99 covers the pixel. An item at layer 2 over its background at layer 1 answers at a pixel
//                     the background paints, whichever joined client.screen first; a background at the higher layer still
//                     answers where it paints and the item's rectangle where it does not; at equal layers the later one
//                     is on top; plane beats layer. A HUD atom's location is NULL.
//   nothing covers    no hit, so no mouse frame at all.
//   icon-x/icon-y     relative to the named atom's own icon frame (HitFrame), 1-based, with icon-y counting up from the
//                     bottom row. Through an atom.transform the cursor is mapped back into the untransformed icon. Over an
//                     overlay it is still the atom's frame and is not clamped.
//   vis-x/vis-y       the cursor within the bounding box of the transformed icon (centred on the icon's centre plus the
//                     translation, half extents floored), sent only when it differs from icon-x/icon-y: for a scale, rotate,
//                     flip or skew, never for a pure translation, a pixel offset or an overlay, and never when either half
//                     is the 0x8000 "unset" value. Sub-pixel rule (hittest.cpp frameCoords): the cursor is the pixel's
//                     top-left corner and every coordinate is floored in +y-down image space. So Turn(90) gives
//                     vis-x = icon-y + 1, vis-y = 33 - icon-x, and its leftmost box column (icon-y 0) is not the atom: the
//                     turf beneath answers.
//   vis_contents      a child is named itself, in its own frame (a child at pixel_x 16 has icon-x 23), at its holder's cell (a
//                     map atom shown in another atom's vis_contents is located at the holder, not its own loc). It is drawn
//                     and named through the holder's transform about its own centre.
//   location          the named atom's own cell, not the cursor's.
//   plane master      a PLANE_MASTER at mouse_opacity 0 hides everything drawn on its plane (per placed layer: an
//                     overlay on that plane drops out, while its atom's own icon on another plane still answers). The
//                     layers never reach HitScene::world (scene.cpp MasterRec::hideFromMouse). A master at
//                     mouse_opacity 2 (takes every click off its plane) is not modelled.
//   maptext           never takes a click by its ink. An obj's own maptext, or a client.image's, answers with the turf
//                     beneath at mouse_opacity 1. At the atom's mouse_opacity 2 its whole maptext box is part of its
//                     rectangle. A maptext link (<a href>) is not modelled.
//   client.images     an image's pixels answer for its parent (HitAtom = the parent's: its name, location and
//                     mouse_opacity; the image's own mouse_opacity and alpha are ignored), named in the image's own icon
//                     frame. An override image replaces the parent's shape (scene.cpp imagesOn).
//   render_source     a relay of an atom's render_target is such an image: it answers for its parent where the relayed
//                     pixels paint. With PASS_MOUSE on the relay they answer for the SOURCE atom instead (a client.screen
//                     source: located nowhere), named in the source's icon frame at the relay, where their alpha is not 0
//                     whatever the relay's or the source's mouse_opacity. vis-x/vis-y then count from a client.screen
//                     source's screen_loc, its pixel_x/pixel_y left out (HitFrame::visLeft/visBottom), and are sent when
//                     they differ from icon-x/icon-y.
//                     PASS_MOUSE without render_source changes nothing.
//
// The draw key (drawKey), as stock DreamSeeker 516 sorts its map icon list: plane, then a sub-layer byte, then a float
// layer, then the order the icons were listed. A MAP layer's layer is first lowered by its view cell's tie-break,
// (32*y + x + 33) / 2^18 for the 1-based cell x,y from the view's bottom-left, in float32: at the same layer a lower row
// draws over a higher one and a cell over the one east of it, and a layer just at a threshold can round below it: 5000
// always does, 20000 and 25000 from the 7th row up (where the tie-break passes half a float step). Then, from
// the float: at 20000 or more, 20000 off and the byte -64 (BACKGROUND_LAYER); then at 5000 or more, 5000 off and +16
// (EFFECTS_LAYER, also within the background). A client.screen layer has no tie-break and +1, so it sits above every
// map layer of its band and below the next band. TOPDOWN_LAYER is no band on a topdown map; a negative layer is regular.
// Not modelled (the scene stats count these): a plane master's mouse_opacity 2,
// its rule for client.screen objects on its plane, slate/relay folding, maptext links (<a href>),
// animated appearances' mid-tween positions, and an image on a parent at mouse_opacity 2 (whether the image's rectangle
// joins the parent's).
// A HUD object is hit over everything it draws: a sprite larger than a tile, its pixel offsets and its attached client.images
// are not clipped to the screen_loc cell. icon-x/icon-y count from the atom's own icon, so an overlay drawn 24 px east of it
// answers icon-x 25..56.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "render/ir.h"
#include "render/xform.h"

namespace fugue::render {

constexpr int kMouseOpacityNone = 0, kMouseOpacityIcon = 1, kMouseOpacityBBox = 2;

// See "The draw key" above. `cell` is mapCell(x, y) for a map layer, -1 for a screen layer or none.
struct DrawKey {
    int band = 0;
    float layer = 0;
    bool operator<(const DrawKey& o) const { return band != o.band ? band < o.band : layer < o.layer; }
    bool operator==(const DrawKey& o) const { return band == o.band && layer == o.layer; }
    bool operator!=(const DrawKey& o) const { return !(*this == o); }
};
inline int mapCell(int x, int y) { return 32 * y + x + 33; }
inline DrawKey drawKey(double layer, bool screen, int cell) {
    DrawKey k;
    k.band = screen ? 1 : 0;
    k.layer = float(layer);
    if (cell >= 0) k.layer = float(double(k.layer) - std::ldexp(double(cell), -18));
    if (k.layer >= 5000) {
        if (k.layer >= 20000) {
            k.layer = float(double(k.layer) - 20000);
            k.band -= 64;
        }
        if (k.layer >= 5000) {
            k.layer = float(double(k.layer) - 5000);
            k.band += 16;
        }
    }
    return k;
}
// Atom kinds, numbered like the wire's ref kinds (proto::enc::RefKind) minus null.
constexpr uint8_t kAtomTurf = 0, kAtomObj = 1, kAtomMob = 2;

struct HitAtom {
    std::string name;            // atom.name off its S2C::Appearance, text-macro markers already stripped (scene.cpp)
    int mouseOpacity = 1;        // decoded: ((flags >> 12) + 1) & 3
    uint8_t kind = kAtomTurf;    // kAtomTurf = no movable: the caller names the turf under the cursor
    uint32_t ref = 0;            // obj/mob ref (registry bit stripped)
    int worldX = 0, worldY = 0;  // the cell it is drawn on (1-based); 0,0 for a screen object
    // A turf on another z than the default map's (a secondary map's vis_contents): named [07][turfId], not by x,y.
    bool byId = false;
    uint32_t turfId = 0;  // (x-1) + (y-1)*maxx + (z-1)*maxx*maxy
    bool screen = false;
    uint32_t appid = 0;  // the appearance it is drawn from (the right-click menu's icon: render::appearanceIcon)
    int dir = -1;        // the facing it is drawn with; -1 = the appearance's own
    bool wheel =
        false;  // appearance flag 0x100000: the atom declares MouseWheel or MouseMove (stock sends it wheel events)
    bool hover = false;  // appearance flag 0x800: the atom declares a Mouse proc of its own (Entered/Exited/Move/Wheel)
    bool mouseMove = false;  // appearance flag 0x80000: the atom declares MouseMove
};

// An atom's ICON FRAME in map-image pixels: its untransformed icon rectangle where the atom is anchored (pixel offsets
// applied), and the atom.transform it is drawn through (image space, render/xform.h). Every layer of one atom, overlays and
// underlays included, carries the frame of that atom's own icon, because stock DreamSeeker reports a click in that frame.
// w == 0: no frame (a client.screen layer; the sprite rectangle is used).
struct HitFrame {
    int x = 0, y = 0, w = 0, h = 0;
    bool hasXf = false;
    Mat m;
    // A PASS_MOUSE relay of a client.screen source: the source's screen_loc bottom-left in map-image pixels (vis-x/vis-y).
    bool hasVisOrigin = false;
    int visLeft = 0, visBottom = 0;
};

struct HitLayer {
    SpritePtr sprite;
    int x = 0, y = 0;      // top-left in the map image (HUD: inside the pass canvas)
    int atom = -1;         // index into HitScene::atoms
    HitFrame frame;        // map layers only
    bool own = false;      // the atom's OWN icon (not one of its overlays/underlays): see hittest.cpp covers()
    double plane = 0;      // the (resolved) plane it is drawn on: HUD and map layers meet by it (hitAt)
    bool maptext = false;  // a maptext box: answers only under mouse_opacity 2, never by its ink (see "maptext" above)
    bool footprint =
        false;    // a transparent stand-in for an atom's own icon that draws nothing: hitStack only, never hitAt
    DrawKey key;  // its draw key (drawKey): orders it against the other layers of its plane
    bool passMouse = false;  // drawn through a PASS_MOUSE relay: answers by its alpha, not by the atom's mouse_opacity
};

struct HitHud {
    int atom = -1;
    double plane = 0, layer = 0;   // the screen atom's resolved plane and layer
    int tileW = 0, tileH = 0;      // the hit canvas: the cell joined with every layer
    int originX = 0, originY = 0;  // the atom's own icon's bottom-left: icon-x/icon-y count from it
    std::vector<HitLayer> layers;  // draw order, canvas coordinates, each on its own resolved plane
    struct Dest {
        int x, y;
    };
    std::vector<Dest> dests;  // canvas top-left; screen_loc range form repeats it
};

// Everything a pointer question needs, immutable once built (share it across threads via shared_ptr).
struct HitScene {
    int tileW = 32, tileH = 32;
    int cols = 0, rows = 0;        // viewport extent in tiles
    int originX = 0, originY = 0;  // 1-based world coords of the viewport's BOTTOM-LEFT tile
    std::vector<HitAtom> atoms;
    std::vector<HitLayer> world;  // every placed map layer, in DRAW order (last = topmost)
    std::vector<HitHud> hud;      // client.screen passes, client.screen order (hitAt sorts by plane/layer)
    // A secondary map control (render/scene.h assembleSecondaryMap): its screen objects, their vis_contents turfs and
    // what stands on them are all world layers, a turf is named by its own cell, and screen cells count from the box's
    // own corner (screenX0/screenY0 cells past screen_loc 1,1).
    bool secondary = false;
    int screenX0 = 0, screenY0 = 0;
    bool anyHover = false;  // some atom carries HitAtom::hover
    bool clientHover =
        false;  // the world declares a client hover proc (MouseSubs; set by the session, not the assembler)
    int pixW() const { return cols * tileW; }
    int pixH() const { return rows * tileH; }
};

struct Hit {
    std::string name;
    uint8_t kind = kAtomTurf;
    uint32_t ref = 0;
    int worldX = 0, worldY = 0;
    bool byId = false;  // HitAtom::byId / turfId
    uint32_t turfId = 0;
    bool screen = false;
    // The WINNING SPRITE's rectangle in map-image pixels (top-left origin).
    int spriteX = 0, spriteY = 0, spriteW = 0, spriteH = 0;
    int atom = -1;       // index into HitScene::atoms when known (hitStack sets it), else -1
    uint32_t appid = 0;  // HitAtom::appid / dir of the named atom (map AND screen atoms)
    int dir = -1;
    bool wheel = false;                     // HitAtom::wheel
    bool hover = false, mouseMove = false;  // HitAtom::hover / mouseMove
    HitFrame frame;                         // the winning layer's frame (HitLayer::frame)
};

// DreamSeeker's right-click STACK: every atom with a layer (its icon, an overlay, a client.image) whose rectangle
// holds the pixel, transparent pixels included at any mouse_opacity but 0, listed once in draw order topmost first,
// at its topmost such layer: by plane and draw key (a screen layer above every map layer of its plane and band, so a
// screen object on a lower plane, or at BACKGROUND_LAYER, lists after the floor), then client.screen order or map
// draw order. An atom whose own icon draws nothing (no icon, no such state) still holds its own icon frame, whatever
// its overlays draw; an unnamed one is left out. On a secondary map the stack is instead the cell of the atom under
// the cursor.
std::vector<Hit> hitStack(const HitScene& s, int px, int py);

// The atom drawn at map-image pixel (px,py), or false when no sprite answers there.
bool hitAt(const HitScene& s, int px, int py, Hit& out);

// What a mouse command at that pixel names. overKind: 1 obj, 2 mob, 3 turf, 7 turf by id (the wire's RefKind numbering).
struct Naming {
    uint8_t overKind = 3;
    uint32_t overRef = 0;      // obj/mob, or the turf id (7)
    int overX = 0, overY = 0;  // turf, 1-based world
    bool locNull = false;
    int locX = 0, locY = 0;          // turf, 1-based world
    int iconX = 1, iconY = 1;        // within the NAMED ATOM's icon, before its transform (unclamped, on the wire too)
    int visX = 1, visY = 1;          // within the transformed icon's bounding box (DreamSeeker's vis-x/vis-y)
    bool hasVis = false;             // vis != icon: the only case DreamSeeker sends vis-x/vis-y
    int screenX = 1, screenY = 1;    // 1-based viewport cell
    int screenPX = 1, screenPY = 1;  // pixel within that cell, 1-based, lower-left origin
    Hit hit;
};
// false: pixel outside the image, unusable geometry, or no sprite covers it (=> send nothing).
bool nameMouse(const HitScene& s, int px, int py, Naming& out);

// Test-only hook; 0 is the shipped behaviour.
void setHitMutation(int m);

}  // namespace fugue::render

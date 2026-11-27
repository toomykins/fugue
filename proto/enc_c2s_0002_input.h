// C2S::Command: the unified input command (sub-typed by body[0]), and C2S::Topic. ENCODERS only.
//
// Byte layouts follow stock DreamSeeker 516. Subs: 0 move, 2 verb, 3-6 mouse, 7 extended mouse, 8 hover.
// Bodies are PLAINTEXT; net::Session body-ciphers them with K_login.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "net/bytes.h"

namespace fugue::proto::enc {

// Command mouse sub-types
constexpr uint8_t kMouseClick = 0x03, kMouseDblClick = 0x04, kMouseUp = 0x05, kMouseDown = 0x06, kMouseExt = 0x07,
                  kMouseHover = 0x08;
constexpr uint16_t kMouseEventDrop = 0x0092, kMouseEventDrag = 0x0093, kMouseEventWheel = 0x010a;
constexpr uint8_t kVerbSrcMob = 0x02, kVerbSrcClient = 0x08;

enum RefKind : uint8_t {
    RefNull = 0,
    RefObj = 1,
    RefMob = 2,
    RefTurf = 3,
    RefTurfId = 7
};  // RefTurfId: [7][turf id u32] (encoded like obj/mob)

struct AtomRef {
    uint8_t kind = RefNull;
    uint32_t ref = 0x0000ffff;  // RefObj/RefMob/RefTurfId/RefNull payload
    uint16_t x = 0, y = 0;      // RefTurf: WIRE coordinates (DM coordinate minus 1)
};
AtomRef nullRef();
AtomRef objRef(uint32_t ref);
AtomRef mobRef(uint32_t ref);
AtomRef turfRef(int worldX, int worldY);  // 1-based DM coordinates, clamped to >= 1
Bytes encodeAtomRef(const AtomRef&);      // 5 bytes: [kind][u32 LE] or [3][x u16][y u16]

struct Mods {
    bool ctrl = false, shift = false, alt = false;
};

struct ClickCmd {
    uint8_t sub = kMouseClick;
    AtomRef over, loc = nullRef();
    std::string control = "mapwindow.map";
    // A secondary map control: loc is this string instead (the control's short or full id), and screen-loc is prefixed
    // with screenLocMap (mapIdParam). Both empty on the default map.
    std::string locStr, screenLocMap;
    int iconX = 16, iconY = 16;  // in the named atom's icon frame, sent unclamped (can be < 1 or > 32 over an overlay)
    // vis-x/vis-y: the cursor in a TRANSFORMED atom's bounding box in image coordinates. Sent, unclamped, right after
    // icon-y only when hasVis - stock DreamSeeker's rule (vis pair != icon pair).
    int visX = 0, visY = 0;
    bool hasVis = false;
    int screenX = 1, screenY = 1;    // 1-based viewport tile column/row
    int screenPX = 0, screenPY = 0;  // 0 = use icon px
    std::string button = "left";
    Mods mods;
    int screenTileSize = 32;  // world icon size; bounds the screen-loc pixel components
};
std::string clickParams(const ClickCmd&);
// Finalize a canonical narrow mouse body (subtypes 3..8) using this session's
// negotiated verb-slot width. Worker only; refs/arguments and event IDs are unchanged.
Bytes mouseWireBody(Bytes body, bool wideId);
Bytes encodeMouseCmd(
    const ClickCmd&);  // [sub 00 ff ff][over][03 00][loc][02 ctrl\0][02 params\0] (loc: [02 locStr\0] when set)
// The same shape naming nothing: over and loc null atoms (loc [02 locStr\0] when set), control a null atom when empty.
Bytes encodeMouseNone(uint8_t sub, const std::string& locStr, const std::string& control, const std::string& params);
// A map id as stock writes it into a params value: [A-Za-z0-9_.-] kept, every other byte %xx (lower-case hex), so
// camera_[0x3000000]_map becomes camera_%5b0x3000000%5d_map.
std::string mapIdParam(const std::string& id);

// The hover report (sub 0x08), one shape for all three hover procs: the server runs MouseExited on the atom the previous
// report named and MouseEntered on this one when they differ, and MouseMove when they are the same.
// params: hoverParams (clickParams without the button; "right=1;" or "middle=1;" before the modifiers when c.button is
// "right" or "middle", a report made as a verb menu closes with that button held), or "screen-loc=X:PX,Y:PY" alone over the letterbox (over and
// loc null). locStr: on a secondary map, loc is that string (the control's short id), and screen-loc carries it.
std::string hoverParams(const ClickCmd&);
Bytes encodeHover(const AtomRef& over, const AtomRef& loc, const std::string& control, const std::string& params,
                  const std::string& locStr = {});
// The pointer left the map control: [08 00 ff ff][00 null][03][00 null][00 null][02 ""] - loc AND control are null atoms.
Bytes encodeHoverLeave();
ClickCmd turfClick(int worldX, int worldY, int screenCol, int screenRow, int iconX, int iconY, Mods = {});

// A click on an INFO-control (stat panel) row, not the map shape. Stock DreamSeeker 516 sends:
//   [sub 03 click | 04 dblclick][00 ff ff][over 5B][03 = three values][02 panel\0][02 control\0][02 params\0]
// i.e. Click(location = the stat PANEL'S NAME, control = the INFO control's id, params). The map click's `03 00` after
// `over` is the same value count followed by a tag-0 (atom) location. A turf row's `over` is [07][the turf's id] (its
// \ref low bits; the head the StatPanel row carried), not the map's [03][x][y]. params has NO screen-loc and NO
// icon-x/icon-y unless the row DRAWS an icon (see infoClickParams); a click on the row's label, or past its text, sends
// nothing.
Bytes encodeInfoClick(uint8_t sub, const AtomRef& over, const std::string& panel, const std::string& control,
                      const std::string& params);
// "left=1;button=left", with modifiers between the two in this order: "left=1;ctrl=1;shift=1;button=left",
// "left=1;alt=1;button=left", "middle=1;button=middle". When the row shows an icon (icon = true), stock prefixes
// "icon-x=X;icon-y=Y;" with the modifiers unmoved. X/Y count in the row's icon CELL (world icon size), not the icon's own
// pixels: x+1 from the cell's left edge, y+1 from its bottom edge, unclamped. A row with nothing drawn in its cell
// (no icon, no overlay) sends neither.
std::string infoClickParams(const std::string& button, const Mods&, bool icon = false, int iconX = 0, int iconY = 0);
// A hover report over the INFO control is the click's shape with sub 0x08 (encodeInfoClick(kMouseHover, ..)), as stock
// DreamSeeker 516.1689 sends it (tests/data/hover-world panel.json): over a row naming an atom, that atom, the
// selected tab's name and the control, params "icon-x=X;icon-y=Y" for a row that draws an icon (counted as for a
// click) and "" for one that does not; over the panel's empty space (a row's label, past its text, a text row, below
// the rows) a null atom with the same location and control and params "". Leaving the panel is the map's leave
// (encodeHoverLeave). Modifiers held: appended as on the map ("ctrl=1", "shift=1", "alt=1"), not measured here.
std::string infoHoverParams(const Mods&, bool icon, int iconX, int iconY);

// Which mouse events the world subscribed to (MouseSubs FormatFlags word, decoded by net/input.h).
struct MouseHooks {
    bool down = false, up = false, click = false, dblClick = false, drag = false, drop = false;
};
MouseHooks allMouseHooks();
std::vector<Bytes> pressFrames(ClickCmd, const MouseHooks&);    // MouseDown iff subscribed
std::vector<Bytes> releaseFrames(ClickCmd, const MouseHooks&);  // MouseUp then Click, each iff subscribed
std::vector<Bytes> clickSequence(ClickCmd, const MouseHooks&);  // Down, Up, Click

struct DragCmd {
    bool drop = false;
    AtomRef src, srcLoc = nullRef(), over = nullRef(), overLoc = nullRef();
    std::string srcLocStr, overLocStr;  // non-empty: that location is this string (a secondary map's id)
    std::string srcControl = "mapwindow.map", overControl = "mapwindow.map", params;
    bool overLocBlank = false;  // over_location is the string "" (a drag over no map, or the default map's letterbox)
    bool overControlNull = false;  // over_control is a null atom (a drop over no map)
};
Bytes encodeDragCmd(const DragCmd&);  // sub 0x07, event 0x93 (drag) / 0x92 (drop), 7 values

// MouseWheel(object, delta_x, delta_y, location, control, params), as stock DreamSeeker 516 sends one per wheel notch:
//   [07 00][event 0a 01][00 ff ff 00 00][06][00 over][01 delta_x f32][01 delta_y f32][02 location\0][02 control\0][02 params\0]
// over is null (00 ffff0000) when nothing is under the pointer. location is the map control's own id (no window
// prefix), control its full id. params: icon-x/icon-y (only when an atom is named), the held button
// ("left=1;button=left"), the modifiers, then screen-loc prefixed with the map id ("screen-loc=map:8:16,7:16").
struct WheelCmd {
    AtomRef over = nullRef();
    float dx = 0, dy = 0;
    std::string location = "map", control = "mapwindow.map", params;
};
Bytes encodeWheelCmd(const WheelCmd&);

Bytes encodeMove(uint8_t dir);  // sub 0: 00 00 dir 00 00 ff ff 00 00 00

struct VerbArg {
    uint8_t tag = 0x02;  // 0 ref, 1 number (float32 LE), 2 string
    uint8_t kind = RefObj;
    uint32_t ref = 0;
    float num = 0;
    std::string str;
};
// Wide wire slots are verified for IDs <=65535; callers must not truncate larger dictionary IDs.
Bytes encodeVerb(uint16_t verbId, uint8_t srcKind, uint32_t srcRef, const std::vector<VerbArg>& args,
                 bool wideId = false);
// Movement verb call: args {dir} or {dir, state} (state only when the verb declares 2 args), src-kind client, src-ref 0
// (what stock DreamSeeker sends for a /client/verb; the srcRef parameter is ignored).
Bytes encodeMoveVerb(uint16_t verbId, uint32_t srcRef, int dir, bool down, bool twoArgs, bool wideId = false);

Bytes encodeTopic(const std::string& href);  // strips "byond://", ensures leading '?', NUL-terminated

}  // namespace fugue::proto::enc

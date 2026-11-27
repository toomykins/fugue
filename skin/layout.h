// skin/layout.h - a parsed skin laid out as control rectangles for one client rect.
//
// Stock DreamSeeker 516: an omitted `splitter` is 50; fractional file values are retained; `is-vert` defaults false.
//   A lone pane fills the CHILD without a divider, including an omitted opposite side.
//   With two panes the divider comes out of the box BEFORE the fraction is applied. kSplitW is a fixed logical width, not native DPI parity (144-DPI stock measured a 5px divider).
//   The split is a percentage preserved across resizes and clamped to 0..100 (tg's 2 and 97.5 are honoured,
//   not pulled to 10 and 90); a pane window hosted in a splitter slot always fills the slot exactly (its MAIN anchors
//   are never consulted); a control whose two anchors track the same edge keeps its designed extent on that axis.
// The anchor formula is stock's (MEASURED): corner' = corner_design + trunc(actual * pct / 100) - trunc(design * pct / 100).
// Where stock's behaviour is not pinned down, the layout follows these rules: `size = 0x0` meaning "the rest
//   of the design box"; an anchor of `none` on ONE corner meaning "keep the size"; the 8-deep and seen-once limits;
//   and the empty-slot rule (a slot with no window is filled by the first window whose own on-show assigns it; browser
//   panes sort last). Stock does not fill a slot until it shows that pane, so LiveSkin turns the rule off (layoutSkin
//   inferSlots).
// Quirk, matched on purpose: an empty result rect is the zero rect (0,0,0,0), not an empty rect at the control's corner.
//   A divergence here would move an off-pane control.
// SkinWindow places BUTTON widgets using full geometry and clips paint/input to rect without resizing the button.
// Live property overlays: the layoutSkin() overload taking a PropStore (skin/props.h) lets a winset override, per
//   control, is-visible / is-disabled / pos / size (applied in the window's design coordinates so the anchors still
//   compose), and a CHILD's left / right. An empty side leaves the other pane the whole box, with no splitter.
//   LiveSkin turns a placed control's pos / size winset into a GeometryOverrides entry first (designGeometry).
//   The overload without a PropStore lays out the file's own state, including optional slot inference.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "skin/dmf.h"
#include "skin/props.h"

namespace fugue::skin {

constexpr int kDefaultSplitter = 50;  // stock's default for an unstated splitter
constexpr int kSplitW = 4;            // divider width

// Stock stores CHILD percentages as float, including fractional DMF values.
// Missing/malformed/nonfinite file values use the default; layout clamps separately.
float fileSplitterPercent(const Elem& e);

struct Rect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int dx() const { return x1 - x0; }
    int dy() const { return y1 - y0; }
    bool empty() const { return x0 >= x1 || y0 >= y1; }
    bool operator==(const Rect& o) const { return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1; }
    Rect intersect(const Rect& s) const;  // empty -> {0,0,0,0}
};

struct Control {
    std::string window, name, type;
    Rect rect;  // what is visible: full clipped to the box its window shows through
    Rect full;  // the anchor rectangle before that clip; winget `size`, and the box a CHILD splits (stock measures a
                // control that overhangs its pane at its whole size)
    Rect box;   // the box its WINDOW was laid out in (the main client, or the slot hosting its pane): winget `pos`
                // is full's corner relative to this (liveskin.cpp wingetLocked)
    bool visible = true, disabled = false, isDefault = false, offPane = false;
    int depth = 0;
    std::string top;   // the floating window it lives in (Tree::floats), "" = the main window
    std::string side;  // "left" / "right" / ""
    const Elem* elem = nullptr;
    uint64_t runtimeIdentity = 0;  // zero for file controls
};

struct DesignGeometry;
using GeometryOverrides = std::map<std::string, DesignGeometry>;
// A runtime control (winset `parent=`) laid out into the tree. `geometry` holds its pos / size winsets as LiveSkin read
// them (keyed by the lower-case "parent.name", DesignGeometry::refW / refH the box they were read in); without an entry
// its pos / size props are read in the parent window's design coordinates.
void layoutRuntime(struct Tree&, const File&, const PropStore&, const std::string& name, const std::string& parent,
                   const std::string& type, uint64_t identity, const GeometryOverrides* geometry = nullptr);

struct Splitter {
    std::string path;
    Rect rect;
    bool vert = true;
    double frac = 0.5;
    Rect box;
    std::string top;  // as Control::top
};

// A top-level skin window other than the main one: not a pane, so it is its own window on the screen (tg's tgui_say,
// preferences_window). Laid out in its own client box from (0,0); hidden until a winset of is-visible shows it.
struct Floating {
    std::string window;  // block name
    Rect box;            // its client area: a winset `size`, else the .dmf's
    bool visible = false;
};

struct Slot {
    std::string path;
    Rect rect;
    std::vector<std::string> candidates;
    std::string chosen;
};

struct Tree {
    Rect client;
    int below = 0;  // the main window's status strip under `client` (layoutSkin `below`)
    std::vector<Control> controls;
    std::vector<Splitter> splitters;
    std::vector<Slot> slotList;
    std::vector<Floating> floats;
    const Floating* floating(const std::string& window) const;  // case-insensitive; nullptr = not a floating window
    // ByName: "elem" (first match) or "window.elem". nullptr when absent.
    const Control* byName(const std::string& id) const;
};

// The window a skin opens: the MAIN elem with is-default=true, else window "mainwindow", else the first
// window, else nullptr.
const Block* findMainWindow(const File& f);

// A control's place and extent as a runtime winset of its `pos` or `size` left it, in its window's design coordinates:
// the rect that, put through the anchor formula in the box the control had when the winset arrived, gives the rect the
// winset asked for. Unlike a .dmf `size`, a zero or negative extent here is that extent, not "fill".
struct DesignGeometry {
    long long x = 0, y = 0, w = 0, h = 0;
    int refW = 0, refH = 0;  // the design extent of the box the rect is in (a runtime control's: layoutRuntime)
};
// GeometryOverrides (above) is keyed by the lower-case "window.elem".

// Stock DreamSeeker 516 reads a winset `size` or `pos` of a placed control as where it is to be NOW, in the box it is
// laid out in now, and its anchors move it from there when that box changes size later (516.1689,
// tests/data/winset-size-world: a 0,0 -> 100,100 map set to 160x160 in a 288x288 window is 96x96 once the window is
// 224x224 again). x, y are the box-relative corner and w, h the extent asked for (LiveSkin has already read a zero
// `size` as "to the box's far edge"). Returns the design-coordinate rect that lays `c` out there (DesignGeometry).
DesignGeometry designGeometry(const File& f, const Control& c, long long x, long long y, long long w, long long h);
// The same for a box of design extent dw x dh, the anchors read from `anchors` (nullptr = none).
DesignGeometry designGeometry(const Elem* anchors, const Rect& box, int dw, int dh, long long x, long long y,
                              long long w, long long h);
// A runtime control's pos / size / anchor1 / anchor2 props as an Elem, as layoutRuntime reads them.
Elem runtimeElem(const PropStore& props, const std::string& name);
// The box a runtime control of window `parent` is laid out in: the main window's client area, else the box the window
// is shown in (empty when it is shown nowhere).
Rect runtimeParentBox(const Tree& tree, const File& file, const std::string& parent);

// splits: "window.elem" -> fraction (before clamping), the live splitter-drag overrides.
Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits = {});
// Same, against a live property overlay (nullptr = the file's own state). inferSlots=false turns the empty-slot rule
// OFF: a side no window is stated or set for stays empty and the other side takes the whole box (the clearing form).
// That is what stock DreamSeeker does until it shows a pane whose on-show claims the side. LiveSkin lays out with
// inferSlots=false and runs a pane's on-show when the GUI shows it (liveskin.h setPaneShown). `geometry`, when given,
// places the controls it names and beats their `pos` / `size` props.
// `below` is the height of the main window's status strip under `client`: its controls are clipped to the client area
// and the strip together, as stock DreamSeeker's are (a control can cover the strip).
Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits, const PropStore* props,
                bool inferSlots = true, const GeometryOverrides* geometry = nullptr, int below = 0);

// Where a control a winset created at runtime (`parent=<window>`) sits, in the coordinates `box` is in: `box` is the box
// window `window` was laid out in, and pos / size / anchor1 / anchor2 are the control's winset values ("" = unset), in
// the window's design coordinates, composed by the same anchor formula as a .dmf control. An unset or 0 size fills the
// rest of the design box; an anchor of "none" or "" is no anchor. pos and size may be fractional and are divided by
// `scale` first (a page's device pixels to the layout's, as for a runtime map), then rounded.
Rect runtimeRect(const File& f, const std::string& window, const Rect& box, const std::string& pos,
                 const std::string& size, const std::string& anchor1, const std::string& anchor2, double scale = 1);

// A window that is neither the main window nor a pane: a top-level window of its own (Tree::floats).
bool isFloatingWindow(const File& f, const Block& b);

// The MAIN window's `on-size` verbatim ("" when none).
std::string onSize(const File& f);

}  // namespace fugue::skin

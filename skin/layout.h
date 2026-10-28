// skin/layout.h - a parsed skin laid out as control rectangles for one client rect.
//
// Stock DreamSeeker 516: an omitted `splitter` is 50; the divider is 4 px and comes out of the box BEFORE the fraction
//   is applied; the split is a percentage preserved across resizes and clamped to 0..100 (tg's 2 and 97.5 are honoured,
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
// Live property overlays: the layoutSkin() overload taking a PropStore (skin/props.h) lets a winset override, per
//   control, is-visible / is-disabled / pos / size (applied in the window's design coordinates so the anchors still
//   compose), and a CHILD's left / right. An empty live side hides its control and the other side takes the whole box,
//   with no splitter (the clearing form). The overload without a PropStore lays out the file's own state.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "skin/dmf.h"
#include "skin/props.h"

namespace fugue::skin {

constexpr int kDefaultSplitter = 50;  // stock's default for an unstated splitter
constexpr int kSplitW = 4;            // divider width

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
    Rect rect;         // what is visible: full clipped to the box its window shows through
    Rect full;         // the anchor rectangle before that clip; winget `size`, and the box a CHILD splits (stock measures a
                       // control that overhangs its pane at its whole size)
    Rect box;          // the box its WINDOW was laid out in (the main client, or the slot hosting its pane): winget `pos`
                       // is full's corner relative to this (liveskin.cpp wingetLocked)
    bool visible = true, disabled = false, isDefault = false, offPane = false;
    int depth = 0;
    std::string side;  // "left" / "right" / ""
    const Elem* elem = nullptr;
};

struct Splitter {
    std::string path;
    Rect rect;
    bool vert = true;
    double frac = 0.5;
    Rect box;
};

struct Slot {
    std::string path;
    Rect rect;
    std::vector<std::string> candidates;
    std::string chosen;
};

struct Tree {
    Rect client;
    std::vector<Control> controls;
    std::vector<Splitter> splitters;
    std::vector<Slot> slotList;
    // ByName: "elem" (first match) or "window.elem". nullptr when absent.
    const Control* byName(const std::string& id) const;
};

// The window a skin opens: the MAIN elem with is-default=true, else window "mainwindow", else the first
// window, else nullptr.
const Block* findMainWindow(const File& f);

// splits: "window.elem" -> fraction (before clamping), the live splitter-drag overrides.
Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits = {});
// Same, against a live property overlay (nullptr = the file's own state). inferSlots=false turns the empty-slot rule
// OFF: a side no window is stated or set for stays empty and the other side takes the whole box (the clearing form).
// That is what stock DreamSeeker does until it shows a pane whose on-show claims the side. LiveSkin lays out with
// inferSlots=false and runs a pane's on-show when the GUI shows it (liveskin.h setPaneShown).
Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits, const PropStore* props,
                bool inferSlots = true);

// The MAIN window's `on-size` verbatim ("" when none).
std::string onSize(const File& f);

}  // namespace fugue::skin

#include "skin/layout.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace fugue::skin {

Rect Rect::intersect(const Rect& s) const {
    Rect r = *this;
    if (r.x0 < s.x0) r.x0 = s.x0;
    if (r.y0 < s.y0) r.y0 = s.y0;
    if (r.x1 > s.x1) r.x1 = s.x1;
    if (r.y1 > s.y1) r.y1 = s.y1;
    if (r.empty()) return Rect{};
    return r;
}

namespace {

Rect makeRect(int x0, int y0, int x1, int y1) {  // canonicalising the corners
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    return Rect{x0, y0, x1, y1};
}

double clampFrac(double f) { return f < 0 ? 0 : (f > 1 ? 1 : f); }

int roundInt(double v) { return int(std::round(v)); }  // half away from zero

// An edge anchored at pct% sits at its design coordinate, shifted by the whole-pixel share of the pane's extent that
// the anchor moved: trunc(actual * pct / 100) - trunc(design * pct / 100), each term truncated on its own (MEASURED on
// stock: tg's button strip, 676 designed, 498 shown).
int anchorAxis(int64_t design, int64_t pct, bool ok, int designExtent, int actualExtent) {
    if (!ok || pct < 0) return int(design);
    return int(design + int64_t(actualExtent) * pct / 100 - int64_t(designExtent) * pct / 100);
}

// The window's design size = its MAIN `size`; a window with no usable MAIN size is laid out 1:1.
void designSize(const Block& b, const Rect& box, int& dw, int& dh) {
    if (const Elem* m = b.main()) {
        int64_t w, h;
        if (m->size("size", w, h) && w > 0 && h > 0) { dw = int(w); dh = int(h); return; }
    }
    dw = box.dx(); dh = box.dy();
}

struct GeomOverride {
    long long px = 0, py = 0, sw = 0, sh = 0;
    bool hasPos = false, hasSz = false;
};

Rect anchorRect(const Elem& e, const GeomOverride& ov, int dw, int dh, const Rect& box) {
    int64_t px, py, sw, sh;
    if (!e.point("pos", px, py)) { px = 0; py = 0; }
    if (!e.size("size", sw, sh)) { sw = dw - px; sh = dh - py; }
    if (ov.hasPos) { px = ov.px; py = ov.py; }
    if (ov.hasSz) { sw = ov.sw; sh = ov.sh; }
    // `size = 0x0` is BYOND's "fill the rest of the design box".
    if (sw <= 0) sw = dw - px;
    if (sh <= 0) sh = dh - py;
    int64_t a1x = 0, a1y = 0, a2x = 0, a2y = 0;
    bool ok1 = e.point("anchor1", a1x, a1y);
    bool ok2 = e.point("anchor2", a2x, a2y);
    int x1 = box.x0 + anchorAxis(px, a1x, ok1, dw, box.dx());
    int y1 = box.y0 + anchorAxis(py, a1y, ok1, dh, box.dy());
    int x2 = box.x0 + anchorAxis(px + sw, a2x, ok2, dw, box.dx());
    int y2 = box.y0 + anchorAxis(py + sh, a2y, ok2, dh, box.dy());
    // An anchor of `none` on ONE corner means "keep the size" (the corner pins, the far side follows).
    if (ok1 && !ok2) { x2 = x1 + int(sw); y2 = y1 + int(sh); }
    else if (!ok1 && ok2) { x1 = x2 - int(sw); y1 = y2 - int(sh); }
    // An inverted extent is a zero one (stock answers 82x0 for a button its strip has shrunk past), not a flipped rect.
    return Rect{x1, y1, std::max(x1, x2), std::max(y1, y2)};
}

struct Ctx {
    const File& f;
    Tree& t;
    const std::map<std::string, double>& splits;
    const PropStore* props;
    bool infer;   // the empty-slot rule (layout.h layoutSkin inferSlots)
    std::set<std::string> seen;
};

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Every `<needle><name>` value in a winset command string (name ends at a quote, ; : ? space tab &).
std::vector<std::string> assignedWindows(const std::string& cmd, const std::string& needle) {
    std::vector<std::string> out;
    size_t i = 0;
    for (;;) {
        size_t j = cmd.find(needle, i);
        if (j == std::string::npos) return out;
        i = j + needle.size();
        size_t k = i;
        while (k < cmd.size() && std::string("\";:? \t&").find(cmd[k]) == std::string::npos) k++;
        if (k > i) out.push_back(cmd.substr(i, k - i));
    }
}

// The first visible pane-filling control (anchored 0,0 -> 100,100), else the first visible non-BUTTON.
const Elem* principalControl(const Block* b) {
    if (!b) return nullptr;
    auto ctl = b->controls();
    for (const Elem* e : ctl) {
        if (!e->boolv("is-visible", true)) continue;
        int64_t x1, y1, x2, y2;
        bool ok1 = e->point("anchor1", x1, y1), ok2 = e->point("anchor2", x2, y2);
        if (ok1 && ok2 && x1 == 0 && y1 == 0 && x2 == 100 && y2 == 100) return e;
    }
    for (const Elem* e : ctl)
        if (e->boolv("is-visible", true) && e->type() != "BUTTON") return e;
    return nullptr;
}

bool isBrowserWindow(const File& f, const std::string& name) {
    const Elem* e = principalControl(f.window(name));
    return e && e->type() == "BROWSER";
}

// Windows whose OWN on-show assigns this splitter slot to themselves, file order, browser panes last.
std::vector<std::string> slotClaimants(const File& f, const std::string& path, const std::string& side) {
    size_t dot = path.find('.');
    std::string child = dot == std::string::npos ? "" : path.substr(dot + 1);
    std::string needle = child + "." + side + "=";
    std::vector<std::string> out;
    for (const Block& b : f.blocks) {
        if (b.kind != "window") continue;
        for (const Elem& e : b.elems) {
            for (const char* key : {"on-show", "on-hide"}) {
                const std::string* v = e.get(key);
                if (!v) continue;
                for (const std::string& name : assignedWindows(*v, needle))
                    if (name == b.name && std::string(key) == "on-show" && !contains(out, name)) out.push_back(name);
            }
        }
    }
    std::stable_partition(out.begin(), out.end(), [&](const std::string& n) { return !isBrowserWindow(f, n); });
    return out;
}

void layoutWindow(Ctx& c, const Block& b, const Rect& box, const Rect& clip, int depth, const std::string& side);

void fillSlot(Ctx& c, const std::string& path, const std::string& side, std::string window, const Rect& box, const Rect& clip,
              int depth) {
    std::vector<std::string> cands = slotClaimants(c.f, path, side);
    if (window.empty()) {
        Slot sl{path + "." + side, box, cands, cands.empty() || !c.infer ? "" : cands[0]};
        c.t.slotList.push_back(sl);
        if (sl.chosen.empty()) return;
        window = sl.chosen;
    } else if (!cands.empty()) {
        c.t.slotList.push_back(Slot{path + "." + side, box, cands, window});
    }
    const Block* blk = c.f.window(window);
    if (!blk) return;
    layoutWindow(c, *blk, box, clip, depth, side);
}

void layoutChild(Ctx& c, const Block& b, const Elem& e, const Rect& box, const Rect& clip, int depth) {
    std::string path = b.name + "." + e.name;
    bool vert = e.boolv("is-vert", true);
    std::string left = e.str("left", ""), right = e.str("right", "");
    // Live left/right (a winset) override the file; `live` distinguishes "never stated" from "set, maybe to empty".
    bool leftLive = false, rightLive = false;
    if (c.props) {
        std::string v;
        if (c.props->controlProp(b.name, e.name, "left", v)) { left = v; leftLive = true; }
        if (c.props->controlProp(b.name, e.name, "right", v)) { right = v; rightLive = true; }
    }
    // A side that names no window (tg's `left=null`) is hidden as an emptied one is.
    if (!left.empty() && !c.f.window(left)) left.clear();
    if (!right.empty() && !c.f.window(right)) right.clear();
    // The clearing form: an emptied side is hidden and the other side fills the whole box, undivided. A side the skin
    // leaves empty is emptied the same way unless the empty-slot rule would fill it from an on-show claimant (tg's
    // output_selector_screen names only a left window and measures at the box's full size).
    if (left.empty() && (!c.infer || slotClaimants(c.f, path, "left").empty())) leftLive = true;
    if (right.empty() && (!c.infer || slotClaimants(c.f, path, "right").empty())) rightLive = true;
    if (leftLive && left.empty() && !right.empty()) {
        c.t.slotList.push_back(Slot{path + ".left", Rect{}, slotClaimants(c.f, path, "left"), ""});
        fillSlot(c, path, "right", right, box, clip, depth + 1);
        return;
    }
    if (rightLive && right.empty() && !left.empty()) {
        c.t.slotList.push_back(Slot{path + ".right", Rect{}, slotClaimants(c.f, path, "right"), ""});
        fillSlot(c, path, "left", left, box, clip, depth + 1);
        return;
    }
    double frac = e.floatv("splitter", kDefaultSplitter) / 100;
    auto ov = c.splits.find(path);
    if (ov != c.splits.end()) frac = ov->second;
    frac = clampFrac(frac);
    int sw = e.boolv("show-splitter", true) ? kSplitW : 0;

    Rect a, sp, b2;
    if (vert) {
        int avail = std::max(0, box.dx() - sw);  // the divider comes off the box first
        int cut = box.x0 + roundInt(double(avail) * frac);
        a = makeRect(box.x0, box.y0, cut, box.y1);
        sp = makeRect(cut, box.y0, cut + sw, box.y1);
        b2 = makeRect(cut + sw, box.y0, box.x1, box.y1);
    } else {
        int avail = std::max(0, box.dy() - sw);
        int cut = box.y0 + roundInt(double(avail) * frac);
        a = makeRect(box.x0, box.y0, box.x1, cut);
        sp = makeRect(box.x0, cut, box.x1, cut + sw);
        b2 = makeRect(box.x0, cut + sw, box.x1, box.y1);
    }
    if (sw > 0) c.t.splitters.push_back(Splitter{path, sp.intersect(clip), vert, frac, box});
    fillSlot(c, path, "left", left, a, a.intersect(clip), depth + 1);
    fillSlot(c, path, "right", right, b2, b2.intersect(clip), depth + 1);
}

void layoutWindow(Ctx& c, const Block& b, const Rect& box, const Rect& clip, int depth, const std::string& side) {
    if (depth > 8) return;
    if (c.seen.count(b.name)) return;
    c.seen.insert(b.name);
    int dw, dh;
    designSize(b, box, dw, dh);
    for (const Elem* e : b.controls()) {
        Control ctl;
        ctl.window = b.name; ctl.name = e->name; ctl.type = e->type(); ctl.elem = e;
        ctl.visible = e->boolv("is-visible", true);
        ctl.disabled = e->boolv("is-disabled", false);
        GeomOverride ov;
        if (c.props) {
            bool v;
            if (c.props->controlBool(b.name, e->name, "is-visible", v)) ctl.visible = v;
            if (c.props->controlBool(b.name, e->name, "is-disabled", v)) ctl.disabled = v;
            ov.hasPos = c.props->controlPoint(b.name, e->name, "pos", ov.px, ov.py);
            ov.hasSz = c.props->controlSize(b.name, e->name, "size", ov.sw, ov.sh);
        }
        ctl.isDefault = e->boolv("is-default", false);
        ctl.depth = depth; ctl.side = side;
        ctl.full = anchorRect(*e, ov, dw, dh, box);
        ctl.rect = ctl.full.intersect(clip);  // empty -> the zero rect (see layout.h, Quirk)
        ctl.box = box;
        ctl.offPane = ctl.visible && ctl.rect.empty();
        Rect full = ctl.full, vis = ctl.rect;
        bool child = ctl.type == "CHILD" && ctl.visible;
        c.t.controls.push_back(std::move(ctl));
        if (child) layoutChild(c, b, *e, full, vis, depth);
    }
}

}  // namespace

const Block* findMainWindow(const File& f) {
    for (const Block& b : f.blocks) {
        if (b.kind != "window") continue;
        const Elem* m = b.main();
        if (m && m->boolv("is-default", false)) return &b;
    }
    if (const Block* b = f.window("mainwindow")) return b;
    for (const Block& b : f.blocks) if (b.kind == "window") return &b;
    return nullptr;
}

Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits) {
    return layoutSkin(f, client, splits, nullptr);
}

Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits, const PropStore* props, bool inferSlots) {
    Tree t;
    t.client = client;
    const Block* mw = findMainWindow(f);
    if (!mw) return t;
    Ctx c{f, t, splits, props, inferSlots, {}};
    layoutWindow(c, *mw, client, client, 0, "");
    return t;
}

std::string onSize(const File& f) {
    const Block* mw = findMainWindow(f);
    if (!mw || !mw->main()) return "";
    return mw->main()->str("on-size", "");
}

const Control* Tree::byName(const std::string& id) const {
    size_t dot = id.find('.');
    bool qualified = dot != std::string::npos;
    std::string win = qualified ? id.substr(0, dot) : "", shortName = qualified ? id.substr(dot + 1) : id;
    for (const Control& c : controls) {
        if (qualified) {
            if (c.window == win && c.name == shortName) return &c;
            continue;
        }
        if (c.name == id) return &c;
    }
    return nullptr;
}

}  // namespace fugue::skin

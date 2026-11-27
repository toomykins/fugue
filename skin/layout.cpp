#include "skin/layout.h"
#include "skin/winset.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

namespace fugue::skin {

float fileSplitterPercent(const Elem& e) {
    const std::string value = trimSpace(e.str("splitter", ""));
    char* end = nullptr;
    const float pct = std::strtof(value.c_str(), &end);
    return !value.empty() && end == value.c_str() + value.size() && std::isfinite(pct) ? pct : float(kDefaultSplitter);
}

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
        if (m->size("size", w, h) && w > 0 && h > 0) {
            dw = int(w);
            dh = int(h);
            return;
        }
    }
    dw = box.dx();
    dh = box.dy();
}

struct GeomOverride {
    long long px = 0, py = 0, sw = 0, sh = 0;
    bool hasPos = false, hasSz = false;
    bool exact = false;  // a DesignGeometry: the extent is used as it is, never read as "fill"
};

Rect anchorRect(const Elem& e, const GeomOverride& ov, int dw, int dh, const Rect& box) {
    int64_t px, py, sw, sh;
    if (!e.point("pos", px, py)) {
        px = 0;
        py = 0;
    }
    if (!e.size("size", sw, sh)) {
        sw = dw - px;
        sh = dh - py;
    }
    if (ov.hasPos) {
        px = ov.px;
        py = ov.py;
    }
    if (ov.hasSz) {
        sw = ov.sw;
        sh = ov.sh;
    }
    // `size = 0x0` is BYOND's "fill the rest of the design box".
    if (!ov.exact) {
        if (sw <= 0) sw = dw - px;
        if (sh <= 0) sh = dh - py;
    }
    int64_t a1x = 0, a1y = 0, a2x = 0, a2y = 0;
    bool ok1 = e.point("anchor1", a1x, a1y);
    bool ok2 = e.point("anchor2", a2x, a2y);
    int x1 = box.x0 + anchorAxis(px, a1x, ok1, dw, box.dx());
    int y1 = box.y0 + anchorAxis(py, a1y, ok1, dh, box.dy());
    int x2 = box.x0 + anchorAxis(px + sw, a2x, ok2, dw, box.dx());
    int y2 = box.y0 + anchorAxis(py + sh, a2y, ok2, dh, box.dy());
    // An anchor of `none` on ONE corner means "keep the size" (the corner pins, the far side follows).
    if (ok1 && !ok2) {
        x2 = x1 + int(sw);
        y2 = y1 + int(sh);
    } else if (!ok1 && ok2) {
        x1 = x2 - int(sw);
        y1 = y2 - int(sh);
    }
    // An inverted extent is a zero one (stock answers 82x0 for a button its strip has shrunk past), not a flipped rect.
    return Rect{x1, y1, std::max(x1, x2), std::max(y1, y2)};
}

bool pairOf(const std::string& v, const char* sep, int64_t& a, int64_t& b) {
    long long x, y;
    if (!parsePair(v, sep, x, y)) return false;
    a = x;
    b = y;
    return true;
}

// "A,B" / "AxB" where A and B may be fractional (tgui and vg's tooltip.js send device pixels), divided by scale.
bool scaledPair(const std::string& v, char sep, double scale, int64_t& a, int64_t& b) {
    const char* s = v.c_str();
    char* end = nullptr;
    double x = std::strtod(s, &end);
    if (end == s || *end != sep) return false;
    const char* t = end + 1;
    double y = std::strtod(t, &end);
    if (end == t || *end) return false;
    if (scale <= 0) scale = 1;
    a = std::llround(x / scale);
    b = std::llround(y / scale);
    return true;
}

}  // namespace

Rect runtimeRect(const File& f, const std::string& window, const Rect& box, const std::string& pos,
                 const std::string& size, const std::string& anchor1, const std::string& anchor2, double scale) {
    int dw = box.dx(), dh = box.dy();
    if (const Block* b = f.window(window)) designSize(*b, box, dw, dh);
    int64_t px = 0, py = 0, sw = 0, sh = 0;
    if (!scaledPair(pos, ',', scale, px, py)) px = py = 0;
    if (!scaledPair(size, 'x', scale, sw, sh)) sw = sh = 0;
    if (sw <= 0) sw = dw - px;
    if (sh <= 0) sh = dh - py;
    int64_t a1x = 0, a1y = 0, a2x = 0, a2y = 0;
    bool ok1 = pairOf(anchor1, ",", a1x, a1y), ok2 = pairOf(anchor2, ",", a2x, a2y);
    int x1 = box.x0 + anchorAxis(px, a1x, ok1, dw, box.dx());
    int y1 = box.y0 + anchorAxis(py, a1y, ok1, dh, box.dy());
    int x2 = box.x0 + anchorAxis(px + sw, a2x, ok2, dw, box.dx());
    int y2 = box.y0 + anchorAxis(py + sh, a2y, ok2, dh, box.dy());
    if (ok1 != ok2) {  // one anchor: that corner pins and the control keeps its size (anchorRect)
        if (ok1) {
            x2 = x1 + int(sw);
            y2 = y1 + int(sh);
        } else {
            x1 = x2 - int(sw);
            y1 = y2 - int(sh);
        }
    }
    return Rect{x1, y1, std::max(x1, x2), std::max(y1, y2)};
}

DesignGeometry designGeometry(const Elem* anchors, const Rect& box, int dw, int dh, long long x, long long y,
                              long long w, long long h) {
    int64_t a1x = 0, a1y = 0, a2x = 0, a2y = 0;
    bool ok1 = anchors && anchors->point("anchor1", a1x, a1y);
    bool ok2 = anchors && anchors->point("anchor2", a2x, a2y);
    // anchorAxis undone: the design coordinate that the anchor formula moves to `actual` in this box.
    auto design = [](long long actual, int64_t pct, bool ok, int designExtent, int actualExtent) {
        if (!ok || pct < 0) return actual;
        return actual - int64_t(actualExtent) * pct / 100 + int64_t(designExtent) * pct / 100;
    };
    long long x1 = design(x, a1x, ok1, dw, box.dx()), y1 = design(y, a1y, ok1, dh, box.dy());
    long long x2 = design(x + w, a2x, ok2, dw, box.dx()), y2 = design(y + h, a2y, ok2, dh, box.dy());
    DesignGeometry g{x, y, w, h, dw, dh};
    if (ok1 && ok2) g = {x1, y1, x2 - x1, y2 - y1, dw, dh};
    else if (ok1) g = {x1, y1, w, h, dw, dh};  // that corner pins and the extent is kept (anchorRect)
    else if (ok2) g = {x2 - w, y2 - h, w, h, dw, dh};
    return g;
}

DesignGeometry designGeometry(const File& f, const Control& c, long long x, long long y, long long w, long long h) {
    int dw = c.box.dx(), dh = c.box.dy();
    if (const Block* b = f.window(c.window)) designSize(*b, c.box, dw, dh);
    return designGeometry(c.elem, c.box, dw, dh, x, y, w, h);
}

Elem runtimeElem(const PropStore& props, const std::string& name) {
    Elem e;
    e.name = name;
    for (const char* key : {"pos", "size", "anchor1", "anchor2"}) {
        std::string v;
        if (props.get(name, key, v)) {
            e.idx[key] = int(e.rows.size());
            e.rows.push_back({key, v});
        }
    }
    return e;
}

Rect runtimeParentBox(const Tree& tree, const File& file, const std::string& parent) {
    const Block* block = file.window(parent);
    if (block && block == findMainWindow(file)) return tree.client;
    for (const Control& c : tree.controls)
        if (c.window == parent) return c.box;
    return {};
}

namespace {

struct Ctx {
    const File& f;
    Tree& t;
    const std::map<std::string, double>& splits;
    const PropStore* props;
    bool infer;  // the empty-slot rule (layout.h layoutSkin inferSlots)
    std::set<std::string> seen;
    std::string top;  // the floating window being laid out ("" = the main window)
    const GeometryOverrides* geometry = nullptr;
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

void fillSlot(Ctx& c, const std::string& path, const std::string& side, std::string window, const Rect& box,
              const Rect& clip, int depth) {
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
    bool vert = e.boolv("is-vert", false);
    std::string left = e.str("left", ""), right = e.str("right", "");
    // Live left/right (a winset) override the file; `live` distinguishes "never stated" from "set, maybe to empty".
    bool leftLive = false, rightLive = false;
    if (c.props) {
        std::string v;
        if (c.props->controlProp(b.name, e.name, "left", v)) {
            left = v;
            leftLive = true;
        }
        if (c.props->controlProp(b.name, e.name, "right", v)) {
            right = v;
            rightLive = true;
        }
    }
    // A side that names no window (tg's `left=null`) is hidden as an explicitly emptied one is.
    if (!left.empty() && !c.f.window(left)) {
        left.clear();
        leftLive = true;
    }
    if (!right.empty() && !c.f.window(right)) {
        right.clear();
        rightLive = true;
    }
    // Stock uses the whole CHILD for a single pane, whether the other side is
    // omitted in the file or explicitly cleared. Offline preview can still infer
    // an unstated pane from its on-show; an explicit live clear overrides inference.
    const bool leftEmpty = left.empty() && (leftLive || !c.infer || slotClaimants(c.f, path, "left").empty());
    const bool rightEmpty = right.empty() && (rightLive || !c.infer || slotClaimants(c.f, path, "right").empty());
    if (leftEmpty && !rightEmpty) {
        c.t.slotList.push_back(Slot{path + ".left", Rect{}, slotClaimants(c.f, path, "left"), ""});
        fillSlot(c, path, "right", right, box, clip, depth + 1);
        return;
    }
    if (rightEmpty && !leftEmpty) {
        c.t.slotList.push_back(Slot{path + ".right", Rect{}, slotClaimants(c.f, path, "right"), ""});
        fillSlot(c, path, "left", left, box, clip, depth + 1);
        return;
    }
    double frac = double(fileSplitterPercent(e)) / 100;
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
    if (sw > 0) c.t.splitters.push_back(Splitter{path, sp.intersect(clip), vert, frac, box, c.top});
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
        ctl.window = b.name;
        ctl.name = e->name;
        ctl.type = e->type();
        ctl.elem = e;
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
        if (c.geometry) {
            auto g = c.geometry->find(lowerAscii(b.name + "." + e->name));
            if (g != c.geometry->end()) {
                ov.px = g->second.x;
                ov.py = g->second.y;
                ov.sw = g->second.w;
                ov.sh = g->second.h;
                ov.hasPos = ov.hasSz = ov.exact = true;
            }
        }
        ctl.isDefault = e->boolv("is-default", false);
        ctl.depth = depth;
        ctl.side = side;
        ctl.top = c.top;
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
    for (const Block& b : f.blocks)
        if (b.kind == "window") return &b;
    return nullptr;
}

Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits) {
    return layoutSkin(f, client, splits, nullptr);
}

Tree layoutSkin(const File& f, Rect client, const std::map<std::string, double>& splits, const PropStore* props,
                bool inferSlots, const GeometryOverrides* geometry, int below) {
    Tree t;
    t.client = client;
    t.below = std::max(0, below);
    const Block* mw = findMainWindow(f);
    if (!mw) return t;
    Ctx c{f, t, splits, props, inferSlots, {}, {}, geometry};
    layoutWindow(c, *mw, client, Rect{client.x0, client.y0, client.x1, client.y1 + t.below}, 0, "");
    for (const Block& b : f.blocks) {
        if (b.kind != "window" || &b == mw || !isFloatingWindow(f, b) || c.seen.count(b.name)) continue;
        const Elem* m = b.main();
        int64_t dw = 0, dh = 0;
        if (!m->size("size", dw, dh) || dw < 0 || dh < 0) dw = dh = 0;
        long long sw, sh;
        if (props && props->controlSize("", b.name, "size", sw, sh) && sw >= 0 && sh >= 0) {
            dw = sw;
            dh = sh;
        }
        bool vis = m->boolv("is-visible", true);
        if (props) props->controlBool("", b.name, "is-visible", vis);
        Rect box{0, 0, int(dw), int(dh)};
        t.floats.push_back(Floating{b.name, box, vis});
        c.top = b.name;
        layoutWindow(c, b, box, box, 0, "");
    }
    return t;
}

void layoutRuntime(Tree& tree, const File& file, const PropStore& props, const std::string& name,
                   const std::string& parent, const std::string& type, uint64_t identity,
                   const GeometryOverrides* geometry) {
    // Runtime controls are siblings in their parent window, after file controls.
    // They retain their own identity through visibility/geometry changes.
    if (type != "BROWSER" && type != "MAP") return;
    const auto* block = file.window(parent);
    if (!block && type != "MAP") return;
    const bool inMain = block && block == findMainWindow(file);
    Rect box = runtimeParentBox(tree, file, parent);
    Elem e = runtimeElem(props, name);
    int dw = 0, dh = 0;
    GeomOverride ov;
    if (block) {
        designSize(*block, box, dw, dh);
        // A pos / size winset, read in the box the control had then (runtime geometry, liveskin.cpp).
        if (geometry) {
            auto g = geometry->find(lowerAscii(parent + "." + name));
            if (g != geometry->end()) {
                ov.px = g->second.x;
                ov.py = g->second.y;
                ov.sw = g->second.w;
                ov.sh = g->second.h;
                ov.hasPos = ov.hasSz = ov.exact = true;
                dw = g->second.refW;
                dh = g->second.refH;
            }
        }
    } else {
        // Popup MAP geometry is browser-client-relative; Qt's actual parent
        // supplies clipping. No invented main-window pane or default-map view.
        int64_t x = 0, y = 0, width = 0, height = 0;
        e.point("pos", x, y);
        e.size("size", width, height);
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384 || std::abs(x) > 16384 || std::abs(y) > 16384)
            return;
        box = {0, 0, int(std::max<int64_t>(0, x) + width), int(std::max<int64_t>(0, y) + height)};
        dw = box.dx();
        dh = box.dy();
    }
    Control c;
    c.window = parent;
    c.name = name;
    c.type = type;
    c.box = box;
    c.runtimeIdentity = identity;
    c.full = anchorRect(e, ov, dw, dh, box);
    // A control of the main window shows over its status strip as a .dmf one does (layoutSkin `below`).
    c.rect = c.full.intersect(inMain ? Rect{box.x0, box.y0, box.x1, box.y1 + tree.below} : box);
    c.offPane = box.empty() || c.rect.empty();
    props.controlBool(parent, name, "is-visible", c.visible);
    props.controlBool(parent, name, "is-disabled", c.disabled);
    tree.controls.push_back(std::move(c));
}
bool isFloatingWindow(const File& f, const Block& b) {
    const Elem* m = b.main();
    if (!m || findMainWindow(f) == &b || m->boolv("is-pane", false)) return false;
    for (const Block& o : f.blocks)  // a window a CHILD names is hosted there, is-pane or not
        for (const Elem& e : o.elems)
            if (e.type() == "CHILD" && (e.str("left", "") == b.name || e.str("right", "") == b.name)) return false;
    return true;
}

const Floating* Tree::floating(const std::string& window) const {
    for (const Floating& fl : floats)
        if (lowerAscii(fl.window) == lowerAscii(window)) return &fl;
    return nullptr;
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

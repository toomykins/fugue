#include "render/hittest.h"

#include <cmath>
#include <algorithm>

namespace fugue::render {
namespace {

int g_mutation = 0;
int effOpacity(int mo) {
    if (g_mutation == 1) return kMouseOpacityIcon;
    if (g_mutation == 2) return mo == kMouseOpacityNone ? mo : kMouseOpacityBBox;
    return mo;
}

// A cursor pixel in an atom's icon frame: icon-x/icon-y (the untransformed icon, 1-based, y up) and vis-x/vis-y
// (the transformed icon's bounding box). See hittest.h.
struct FrameCoords {
    int iconX, iconY, visX, visY;
};
FrameCoords frameCoords(const HitFrame& f, int px, int py) {
    FrameCoords c{};
    // Image space is +y DOWN; the frame's centre C, the transform q = C + L(p - C) + t (render/xform.cpp applyTransform's
    // own mapping, re-centred on C). The cursor is the pixel's top-left corner in image space, and every coordinate is
    // floored in that space: icon-y and vis-y are then counted up from the bottom (h - row), not floored after the flip.
    // The transformed bounding box's half extents are floored to whole pixels about the centre. Hence Turn(90) gives
    // vis-x = icon-y + 1, vis-y = 33 - icon-x: the corner x lands on an icon row boundary, which the y-down floor assigns
    // to the row below. kEps absorbs float32 noise in the wire matrix (cos 90 = -4.4e-8) at those exact boundaries.
    constexpr double kEps = 1e-4;
    auto fl = [](double v) { return int(std::floor(v + kEps)); };
    const double qx = px, qy = py;
    const double cx = f.x + f.w / 2.0, cy = f.y + f.h / 2.0;
    double ix = qx, iy = qy, left = f.x, top = f.y, boxH = f.h;
    if (f.hasXf) {
        const Mat& m = f.m;
        double det = m.a * m.e - m.b * m.d;
        if (det != 0) {
            double dx = qx - cx - m.c, dy = qy - cy - m.f;
            ix = cx + (m.e * dx - m.b * dy) / det;
            iy = cy + (-m.d * dx + m.a * dy) / det;
        }
        // the transformed icon's bounding box: the linear part's extent, centred on C + t, half extents floored
        double hw = std::floor((std::fabs(m.a) * f.w + std::fabs(m.b) * f.h) / 2 + kEps);
        double hh = std::floor((std::fabs(m.d) * f.w + std::fabs(m.e) * f.h) / 2 + kEps);
        left = cx + m.c - hw;
        top = cy + m.f - hh;
        boxH = 2 * hh;
    }
    c.iconX = fl(ix - f.x) + 1;
    c.iconY = f.h - fl(iy - f.y);
    c.visX = fl(qx - left) + 1;
    c.visY = int(boxH) - fl(qy - top);
    if (g_mutation == 4 && f.hasXf) {
        // test-only hook: the alternative convention (pixel left edge + vertical centre, floored after the y flip,
        // unfloored box).
        const Mat& m = f.m;
        double det = m.a * m.e - m.b * m.d, dx = qx - cx - m.c, dy = qy + 0.5 - cy - m.f;
        double iy4 = det != 0 ? cy + (-m.d * dx + m.a * dy) / det : qy + 0.5;
        double hh = (std::fabs(m.d) * f.w + std::fabs(m.e) * f.h) / 2,
               hw = (std::fabs(m.a) * f.w + std::fabs(m.b) * f.h) / 2;
        c.iconY = int(std::floor(f.y + f.h - iy4)) + 1;
        c.visX = int(std::floor(qx - (cx + m.c - hw))) + 1;
        c.visY = int(std::floor(cy + m.f + hh - (qy + 0.5))) + 1;
    }
    return c;
}

// covers: with whole=false the SOURCE sprite's own alpha decides (any non-zero counts).
bool covers(const HitLayer& l, int px, int py, bool whole) {
    if (!l.sprite || l.sprite->w <= 0 || l.sprite->h <= 0) return false;
    int ix = px - l.x, iy = py - l.y;
    if (ix < 0 || iy < 0 || ix >= l.sprite->w || iy >= l.sprite->h) return false;
    if (whole) return true;
    if (l.maptext) return false;  // maptext never answers by its ink (see hittest.h)
    if (l.own && l.frame.hasXf && l.frame.w > 0 && g_mutation != 3) {
        // A transformed atom's OWN icon answers only where the cursor maps back INTO the icon. Under Turn(90) the
        // leftmost bounding-box column (vis-x 1, icon-y 0) names the turf beneath, though the resampled sprite paints it.
        FrameCoords c = frameCoords(l.frame, px, py);
        if (c.iconX < 1 || c.iconY < 1 || c.iconX > l.frame.w || c.iconY > l.frame.h) return false;
    }
    return l.sprite->px[(size_t(iy) * size_t(l.sprite->w) + size_t(ix)) * 4 + 3] != 0;
}

void fill(Hit& h, const HitAtom& a, int sx, int sy, const Sprite& sp) {
    h.name = a.name;
    h.kind = a.kind;
    h.ref = a.ref;
    h.worldX = a.worldX;
    h.worldY = a.worldY;
    h.screen = a.screen;
    h.byId = a.byId;
    h.turfId = a.turfId;
    h.appid = a.appid;
    h.dir = a.dir;
    h.wheel = a.wheel;
    h.hover = a.hover;
    h.mouseMove = a.mouseMove;
    h.spriteX = sx;
    h.spriteY = sy;
    h.spriteW = sp.w;
    h.spriteH = sp.h;
}

// Does layer `l` (plane, key) draw over the layer drawn before it at (plane, key)? A later layer wins a tie.
bool drawsOver(double plane, const DrawKey& key, double bestPlane, const DrawKey& bestKey) {
    return plane != bestPlane ? plane > bestPlane : !(key < bestKey);
}

// Visits client.screen entries in `ord` order, a later entry (and a later dest of one entry) beating an earlier one:
// keyed, the topmost layer by plane and draw key; otherwise the last one that covers. allowBBox=false withholds
// mouse_opacity 2's widening.
bool hudScan(const HitScene& s, const std::vector<size_t>& ord, int px, int py, bool allowBBox, bool keyed, Hit& out,
             double& plane, DrawKey& key) {
    bool found = false;
    for (size_t i : ord) {
        const HitHud& hu = s.hud[i];
        if (hu.atom < 0 || size_t(hu.atom) >= s.atoms.size()) continue;
        const HitAtom& a = s.atoms[size_t(hu.atom)];
        if (effOpacity(a.mouseOpacity) == kMouseOpacityNone) continue;
        bool whole = allowBBox && effOpacity(a.mouseOpacity) >= kMouseOpacityBBox;
        for (const auto& d : hu.dests) {
            int lx = px - d.x, ly = py - d.y;
            if (lx < 0 || ly < 0 || lx >= hu.tileW || ly >= hu.tileH) continue;
            for (const HitLayer& l : hu.layers) {
                if (l.footprint || !covers(l, lx, ly, whole)) continue;
                if (found && keyed && !drawsOver(l.plane, l.key, plane, key)) continue;
                fill(out, a, d.x + hu.originX, d.y + hu.originY - l.sprite->h, *l.sprite);
                plane = l.plane;
                key = l.key;
                found = true;
            }
        }
    }
    return found;
}

// client.screen order only. hitAt itself goes by the draw key; a test-only hook uses this rule.
bool hudPass(const HitScene& s, int px, int py, bool allowBBox, Hit& out) {
    std::vector<size_t> ord(s.hud.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    double plane = 0;
    DrawKey key;
    return hudScan(s, ord, px, py, allowBBox, /*keyed=*/false, out, plane, key);
}

}  // namespace

bool hitAt(const HitScene& s, int px, int py, Hit& out) {
    if (s.cols <= 0 || s.rows <= 0 || px < 0 || py < 0 || px >= s.pixW() || py >= s.pixH()) return false;
    Hit h;
    double hudPlane = 0;
    DrawKey hudKey;
    bool hud = false;
    if (g_mutation == 5) {
        // test-only hook: a HUD atom wins outright where it paints, in client.screen order.
        if (hudPass(s, px, py, /*allowBBox=*/false, h)) {
            out = h;
            return true;
        }
    } else {
        // The topmost client.screen layer, each atom covering by its own mouse_opacity (2 = its whole rectangle).
        std::vector<size_t> ord(s.hud.size());
        for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
        hud = hudScan(s, ord, px, py, /*allowBBox=*/true, /*keyed=*/true, h, hudPlane, hudKey);
    }
    const HitLayer* best = nullptr;
    for (const HitLayer& l : s.world) {
        if (l.atom < 0 || size_t(l.atom) >= s.atoms.size()) continue;
        const HitAtom& a = s.atoms[size_t(l.atom)];
        const int mo = l.passMouse ? kMouseOpacityIcon : effOpacity(a.mouseOpacity);
        if (l.footprint || mo == kMouseOpacityNone) continue;
        if (covers(l, px, py, mo >= kMouseOpacityBBox)) best = &l;
    }
    if (hud && (!best || drawsOver(hudPlane, hudKey, best->plane, best->key))) {
        out = h;
        return true;
    }
    if (best) {
        fill(out, s.atoms[size_t(best->atom)], best->x, best->y, *best->sprite);
        out.frame = best->frame;
        return true;
    }
    // test-only hook: when nothing on the map answers, a mouse_opacity-2 HUD atom takes the click.
    Hit c;
    if (g_mutation == 5 && hudPass(s, px, py, /*allowBBox=*/true, c)) {
        out = c;
        return true;
    }
    return false;
}

std::vector<Hit> hitStack(const HitScene& s, int px, int py) {
    std::vector<Hit> out;
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0 || px < 0 || py < 0 || px >= s.pixW() ||
        py >= s.pixH())
        return out;
    std::vector<bool> seen(s.atoms.size(), false);
    auto listable = [&](int atom) {
        if (atom < 0 || size_t(atom) >= s.atoms.size() || seen[size_t(atom)]) return false;
        const HitAtom& a = s.atoms[size_t(atom)];
        return !a.name.empty() && effOpacity(a.mouseOpacity) != kMouseOpacityNone;
    };
    if (s.secondary) {
        // No grid: the stack is the cell of the atom under the pointer (a screen object stands alone).
        Hit top;
        if (!hitAt(s, px, py, top)) return out;
        if (top.screen || top.worldX <= 0) {
            out.push_back(top);
            return out;
        }
        for (auto it = s.world.rbegin(); it != s.world.rend(); ++it) {  // topmost first
            const HitLayer& l = *it;
            if (!listable(l.atom) || !l.sprite) continue;
            const HitAtom& a = s.atoms[size_t(l.atom)];
            if (a.screen || a.worldX != top.worldX || a.worldY != top.worldY) continue;
            seen[size_t(l.atom)] = true;
            Hit h;
            fill(h, a, l.x, l.y, *l.sprite);
            h.atom = l.atom;
            out.push_back(h);
        }
        return out;
    }
    // Every layer whose rectangle holds the pixel, transparent pixels included, in draw order topmost first: by plane and
    // draw key, then client.screen order or map draw order. A HUD atom enters at its topmost such layer.
    struct Cand {
        double plane;
        DrawKey key;
        size_t seq;
        bool screen;
        const HitLayer* l;
        int atom, x, y;
    };  // x,y: map layer / HUD dest
    std::vector<Cand> cs;
    for (size_t i = 0; i < s.hud.size(); i++) {
        const HitHud& hu = s.hud[i];
        for (const auto& d : hu.dests) {
            const HitLayer* top = nullptr;
            for (const HitLayer& l : hu.layers)
                if (covers(l, px - d.x, py - d.y, /*whole=*/true) &&
                    (!top || drawsOver(l.plane, l.key, top->plane, top->key)))
                    top = &l;
            if (top) cs.push_back({top->plane, top->key, i, true, top, hu.atom, d.x, d.y});
        }
    }
    for (size_t i = 0; i < s.world.size(); i++) {
        const HitLayer& l = s.world[i];
        if (covers(l, px, py, /*whole=*/true)) cs.push_back({l.plane, l.key, i, false, &l, l.atom, l.x, l.y});
    }
    std::stable_sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) {
        if (a.plane != b.plane) return a.plane > b.plane;
        if (a.key != b.key) return b.key < a.key;
        if (a.screen != b.screen) return a.screen;
        return a.seq > b.seq;
    });
    for (const Cand& c : cs) {
        if (!listable(c.atom)) continue;
        seen[size_t(c.atom)] = true;
        const HitAtom& a = s.atoms[size_t(c.atom)];
        Hit h;
        if (c.screen) {
            const HitHud& hu = s.hud[c.seq];
            fill(h, a, c.x + hu.originX, c.y + hu.originY - c.l->sprite->h, *c.l->sprite);
        } else {
            fill(h, a, c.x, c.y, *c.l->sprite);
        }
        h.atom = c.atom;
        out.push_back(h);
    }
    return out;
}

void setHitMutation(int m) { g_mutation = m; }

bool nameMouse(const HitScene& s, int px, int py, Naming& n) {
    if (s.cols <= 0 || s.rows <= 0 || s.tileW <= 0 || s.tileH <= 0) return false;
    if (px < 0 || py < 0 || px >= s.pixW() || py >= s.pixH()) return false;
    // The y flip is taken from the bottom pixel row (pixH-1).
    int fromBottom = s.pixH() - 1 - py;
    int tx = px / s.tileW, ty = fromBottom / s.tileH;
    if (tx >= s.cols || ty >= s.rows) return false;
    Hit h;
    if (!hitAt(s, px, py, h)) return false;

    n = Naming{};
    n.screenX = tx + 1 + s.screenX0;
    n.screenY = ty + 1 + s.screenY0;
    n.screenPX = px % s.tileW + 1;
    n.screenPY = fromBottom % s.tileH + 1;
    n.iconX = n.screenPX;
    n.iconY = n.screenPY;
    n.overKind = 3;
    n.overX = n.locX = std::max(1, s.originX + tx);
    n.overY = n.locY = std::max(1, s.originY + ty);
    if (h.kind == kAtomTurf) {
        // no movable answered: the turf under the cursor is the atom (on a secondary map, the vis_contents turf drawn there)
        if (s.secondary && h.worldX > 0 && h.worldY > 0) {
            n.overX = n.locX = h.worldX;
            n.overY = n.locY = h.worldY;
        }
        if (s.secondary && h.byId) {
            n.overKind = 7;
            n.overRef = h.turfId;
        }
    } else if (h.screen) {
        n.overKind = h.kind == kAtomMob ? 2 : 1;
        n.overRef = h.ref;
        n.locNull = true;  // a client.screen atom has no turf
    } else {
        n.overKind = h.kind == kAtomMob ? 2 : 1;
        n.overRef = h.ref;
        if (h.worldX > 0 && h.worldY > 0) {
            n.locX = h.worldX;
            n.locY = h.worldY;
        }  // the atom's OWN cell
    }
    if (h.frame.w > 0 && h.frame.h > 0 && g_mutation != 3) {
        FrameCoords c = frameCoords(h.frame, px, py);
        n.iconX = c.iconX;
        n.iconY = c.iconY;
        n.visX = c.visX;
        n.visY = c.visY;
        n.hasVis = n.visX != n.iconX || n.visY != n.iconY;
    } else if (h.spriteW > 0 && h.spriteH > 0) {
        n.iconX = px - h.spriteX + 1;
        n.iconY = (h.spriteY + h.spriteH - 1) - py + 1;
    }
    if (h.frame.hasVisOrigin) {
        n.visX = px - h.frame.visLeft + 1;
        n.visY = h.frame.visBottom - py;
        n.hasVis = n.visX != n.iconX || n.visY != n.iconY;
    }
    if (!n.hasVis) {
        n.visX = n.iconX;
        n.visY = n.iconY;
    }
    n.hit = std::move(h);
    return true;
}

}  // namespace fugue::render

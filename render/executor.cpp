// CPU executor for the draw-list IR (render/ir.h). Straight-alpha RGBA8 throughout.
//
// Rules (float64 arithmetic, fixed operand order):
//   composite: blendLayer (pixops.cpp): tint and alpha, optional colour matrix, then the blend mode. The sa==0 and
//        sa==255 shortcuts are part of the contract: the sa==0 one zeroes RGB of a transparent destination pixel.
//   blit:    premultiplied 16-bit source-over, with a copy shortcut for fully opaque sources.
//   plane:   a plane with a master gets a fresh transparent surface, blended into the frame in ascending plane order with
//        the master's blend mode; a plane without one draws its layers straight onto the frame.
//        A HUD tile is composited onto a tile-sized transparent canvas, then blitted over.
//   map control: the final flatten over opaque black.
// Build note: render/CMakeLists.txt compiles this module with -ffp-contract=off. A fused multiply-add would change the
// last bit of the float64 expressions below.
#include <algorithm>
#include <cstring>
#include <map>

#include "render/filters.h"
#include "render/ir.h"
#include "render/pixops.h"
#include "render/xform.h"

namespace fugue::render {

namespace {

int g_pipeMutation = 0;

LayerFx fxOf(const DrawLayer& l) {
    LayerFx f;
    f.hasTint = l.hasTint;
    f.tr = l.tintR;
    f.tg = l.tintG;
    f.tb = l.tintB;
    f.alpha = l.alpha;
    f.cm = l.cmat.get();
    return f;
}

// One plane-pipeline surface (outW x outH, straight alpha).
using Surface = std::vector<uint8_t>;

}  // namespace

void setExecutorMutation(int m) { setPixopsMutation(m); }
void setPipelineMutation(int m) {
    g_pipeMutation = m;
    if (m == 10 || m == 13 || m == 16) setPixopsMutation(m);
}

void blendLayerOver(uint8_t* dst, int dw, int dh, const DrawLayer& l) {
    if (!l.sprite) return;
    blendLayer(dst, dw, dh, l.sprite->px.data(), l.sprite->w, l.sprite->h, l.x, l.y, fxOf(l), l.blend);
}

// Source-over blit of src (sp = its anchor) into the rect [rx0,rx1) x [ry0,ry1) of dst. Both clip intersections are taken
// against the original rect origin, and the source anchor is then shifted by how far that origin moved.
void blitOver(uint8_t* dst, int dw, int dh, int rx0, int ry0, int rx1, int ry1, const uint8_t* src, int sw, int sh,
              int spx, int spy) {
    const int ox = rx0, oy = ry0;
    rx0 = std::max(rx0, 0);
    ry0 = std::max(ry0, 0);
    rx1 = std::min(rx1, dw);
    ry1 = std::min(ry1, dh);
    int ax = ox - spx, ay = oy - spy;  // source bounds shifted by (orig - sp)
    rx0 = std::max(rx0, ax);
    ry0 = std::max(ry0, ay);
    rx1 = std::min(rx1, sw + ax);
    ry1 = std::min(ry1, sh + ay);
    if (rx0 >= rx1 || ry0 >= ry1) return;
    spx += rx0 - ox;
    spy += ry0 - oy;

    const uint32_t m = 0xffff;
    int w = rx1 - rx0;
    for (int y = 0; y < ry1 - ry0; y++) {
        uint8_t* dp = &dst[(size_t(ry0 + y) * size_t(dw) + size_t(rx0)) * 4];
        const uint8_t* sp = &src[(size_t(spy + y) * size_t(sw) + size_t(spx)) * 4];
        for (int i = 0; i < 4 * w; i += 4) {
            uint32_t sa8 = sp[i + 3];
            if (sa8 == 255) {  // an opaque source pixel is an exact copy
                dp[i] = sp[i];
                dp[i + 1] = sp[i + 1];
                dp[i + 2] = sp[i + 2];
                dp[i + 3] = 255;
                continue;
            }
            uint32_t sr = sp[i];
            sr |= sr << 8;
            sr = sr * sa8 / 0xff;
            uint32_t sg = sp[i + 1];
            sg |= sg << 8;
            sg = sg * sa8 / 0xff;
            uint32_t sb = sp[i + 2];
            sb |= sb << 8;
            sb = sb * sa8 / 0xff;
            uint32_t sa = sa8 | sa8 << 8;

            uint32_t da8 = dp[i + 3];
            uint32_t dr = dp[i];
            dr |= dr << 8;
            dr = dr * da8 / 0xff;
            uint32_t dg = dp[i + 1];
            dg |= dg << 8;
            dg = dg * da8 / 0xff;
            uint32_t db = dp[i + 2];
            db |= db << 8;
            db = db * da8 / 0xff;
            uint32_t da = da8 | da8 << 8;
            uint32_t a = m - sa;
            // uint16 truncation of each term before the add.
            uint32_t rr = uint16_t(uint16_t(dr * a / m) + uint16_t(sr));
            uint32_t rg = uint16_t(uint16_t(dg * a / m) + uint16_t(sg));
            uint32_t rb = uint16_t(uint16_t(db * a / m) + uint16_t(sb));
            uint32_t ra = uint16_t(uint16_t(da * a / m) + uint16_t(sa));
            // Un-premultiply, then take the high byte.
            if (ra != 0 && ra != 0xffff) {
                rr = (rr * 0xffff) / ra;
                rg = (rg * 0xffff) / ra;
                rb = (rb * 0xffff) / ra;
            }
            dp[i] = uint8_t(rr >> 8);
            dp[i + 1] = uint8_t(rg >> 8);
            dp[i + 2] = uint8_t(rb >> 8);
            dp[i + 3] = uint8_t(ra >> 8);
        }
    }
}

namespace {

// The master's filters in order, then EITHER its colour matrix OR its scalar colour+alpha.
// Never both: the scalar is a legacy sentinel (ff ff ff 00) whenever a matrix rides along, and applying it after the
// matrix zeroes every pixel's alpha, which turns a lighting multiply into a no-op. The filters-then-colour order is the
// stock DreamSeeker order.
RgbaImage finishMaster(const PlaneMasterSpec& m, RgbaImage s, ExecStats& st,
                       const std::map<std::string, RgbaImage>& slates) {
    size_t npix = size_t(s.w) * size_t(s.h);
    auto colour = [&] {
        if (m.cmat)
            applyColorMatrixInPlace(s.px.data(), npix, *m.cmat);
        else if (m.hasColor)
            applyColorInPlace(s.px.data(), npix, m.r, m.g, m.b, m.alpha);
    };
    if (g_pipeMutation == 11) colour();  // test-only: colour BEFORE the filters
    for (const FilterSpec& f : m.filters) {
        if (g_pipeMutation == 62 && (f.kind == FilterSpec::ColorMatrix || f.kind == FilterSpec::AlphaMask))
            continue;  // test-only: neither alpha mask nor colour matrix
        switch (f.kind) {
            case FilterSpec::Blur: s = filterBlur(s, f.size); break;
            case FilterSpec::Outline: s = filterOutline(s, f.size, f.r, f.g, f.b, f.alpha, f.flags); break;
            case FilterSpec::DropShadow:
                s = filterDropShadow(s, f.x, f.y, f.size, f.offset, f.r, f.g, f.b, f.alpha);
                break;
            case FilterSpec::ColorMatrix:
                if (f.cmat) applyColorMatrixInPlace(s.px.data(), npix, *f.cmat);
                break;
            case FilterSpec::AlphaMask: {
                auto it = slates.find(f.source);
                if (it == slates.end()) st.unresolvedMasks++;
                s = filterAlphaMask(s, it == slates.end() ? nullptr : &it->second, f.x, f.y, f.flags);
                break;
            }
        }
    }
    st.unappliedFilters += m.unappliedFilters;
    if (g_pipeMutation != 11) colour();
    if (g_pipeMutation == 14 && m.cmat && m.hasColor) applyColorInPlace(s.px.data(), npix, m.r, m.g, m.b, m.alpha);
    // finish: the master's `transform` last (after filters and colour).
    if (m.hasXform && m.xf != std::array<float, 6>{1, 0, 0, 0, 1, 0})
        warpAboutCentre(s.px, s.w, s.h, double(m.xf[0]), double(m.xf[1]), double(m.xf[3]), double(m.xf[4]),
                        double(m.xf[2]), -double(m.xf[5]));
    return s;
}

RgbaImage zeroSurface(int w, int h) {
    RgbaImage s;
    s.w = w;
    s.h = h;
    s.px.assign(size_t(w) * size_t(h) * 4, 0);
    return s;
}

// The plane pass runs in two passes. Pass 1 fills the slate table over every plane that declares a render_target,
// in ascending order (a slate may reference an earlier slate). Pass 2 builds the scene with the complete table. A relay
// can sit on a lower plane than the master that fills its slate, so a single ascending pass would resolve the relay
// before its slate exists and draw nothing. A plane's layers are composited exactly once: pass 1 owns the slate planes,
// pass 2 the rest.
RgbaImage compositePlanes(const DrawList& dl, ExecStats& st) {
    std::map<std::string, RgbaImage> slates;
    const int W = dl.outW, H = dl.outH;

    auto compose = [&](const PlanePass& p, RgbaImage& surf, bool countMissing = true) {
        // resolve(): a relay swaps its missing image for the slate it names; an absent slate draws nothing.
        for (const DrawLayer& l : p.layers) {
            if (!l.relay.empty()) {
                auto it = slates.find(l.relay);
                if (it == slates.end()) {
                    if (countMissing) st.unresolvedRelays++;
                    continue;
                }
                st.relays++;
                const RgbaImage& sl = it->second;
                blendLayer(surf.px.data(), W, H, sl.px.data(), sl.w, sl.h, l.x, l.relayBottomY - sl.h, fxOf(l),
                           l.blend);
                continue;
            }
            if (!l.sprite) continue;
            blendLayer(surf.px.data(), W, H, l.sprite->px.data(), l.sprite->w, l.sprite->h, l.x, l.y, fxOf(l), l.blend);
        }
    };
    // Would pass 2 find anything to draw on this plane? An unresolved relay is counted here, whether or not the plane draws.
    auto anyResolved = [&](const PlanePass& p) {
        bool any = false;
        for (const DrawLayer& l : p.layers) {
            if (!l.relay.empty()) {
                if (slates.count(l.relay))
                    any = true;
                else
                    st.unresolvedRelays++;
                continue;
            }
            any = true;
        }
        return any;
    };
    auto isSlate = [](const PlanePass& p) { return p.master && !p.master->renderTarget.empty(); };

    // ---- PASS 1: fill the slate table ----
    if (g_pipeMutation != 12) {
        for (const PlanePass& p : dl.planes) {
            if (!isSlate(p)) continue;
            RgbaImage surf = zeroSurface(W, H);
            compose(p, surf);
            slates[p.master->renderTarget] = finishMaster(*p.master, std::move(surf), st, slates);
            st.slates++;
        }
    }

    // ---- PASS 2: build the scene ----
    // A plane with a master is composed into a buffer of its own, which is blended onto what lies beneath with the master's
    // blend mode. A plane without one is no surface at all: its layers blend straight onto what lies beneath, so an ADD,
    // SUBTRACT, MULTIPLY or INSET_OVERLAY layer there sees the map and every plane below it.
    struct Item {
        uint8_t mode = 0;
        const RgbaImage* surf = nullptr;
        const PlanePass* direct = nullptr;
    };
    std::vector<Item> items;
    std::vector<std::unique_ptr<RgbaImage>> own;
    for (const PlanePass& p : dl.planes) {
        if (isSlate(p)) {
            if (g_pipeMutation == 12) {  // test-only: ONE ascending pass - the slate appears when its plane is reached
                RgbaImage surf = zeroSurface(W, H);
                compose(p, surf);
                slates[p.master->renderTarget] = finishMaster(*p.master, std::move(surf), st, slates);
                st.slates++;
            }
            // A render_target starting with * is not drawn on the map.
            if (p.master->renderTarget[0] == '*') {
                st.offScreenPlanes++;
                continue;
            }
            items.push_back(Item{p.master->blend, &slates[p.master->renderTarget], nullptr});
            continue;
        }
        if (!anyResolved(p)) continue;
        if (!p.master && g_pipeMutation != 60) {  // 60, test-only: a masterless plane gets a buffer of its own
            items.push_back(Item{kBlendDefault, nullptr, &p});
            continue;
        }
        auto surf = std::make_unique<RgbaImage>(zeroSurface(W, H));
        compose(p, *surf, false);
        uint8_t mode = kBlendDefault;
        if (p.master) {
            mode = p.master->blend;
            *surf = finishMaster(*p.master, std::move(*surf), st, slates);
        }
        own.push_back(std::move(surf));
        items.push_back(Item{mode, own.back().get(), nullptr});
    }

    RgbaImage out = zeroSurface(W, H);
    for (const Item& it : items) {
        if (it.direct) {
            compose(*it.direct, out, false);
            continue;
        }
        blendLayer(out.px.data(), W, H, it.surf->px.data(), it.surf->w, it.surf->h, 0, 0, LayerFx{}, it.mode, 0, -1,
                   true);  // plane buffer -> frame
    }
    return out;
}

}  // namespace

RgbaImage execute(const DrawList& dl, ExecStats* stats) {
    ExecStats local;
    ExecStats& st = stats ? *stats : local;
    st = ExecStats{};
    RgbaImage frame = compositePlanes(dl, st);

    std::vector<uint8_t> tile;
    for (const HudPass& h : dl.hud) {
        if (h.tileW <= 0 || h.tileH <= 0) continue;
        tile.assign(size_t(h.tileW) * size_t(h.tileH) * 4, 0);
        for (const DrawLayer& l : h.layers) blendLayerOver(tile.data(), h.tileW, h.tileH, l);
        for (const auto& d : h.dests) {
            // The overlap test is subsumed by blitOver's clip.
            blitOver(frame.px.data(), dl.outW, dl.outH, d.x, d.y, d.x + h.tileW, d.y + h.tileH, tile.data(), h.tileW,
                     h.tileH, 0, 0);
        }
    }

    if (!dl.groundBlack) return frame;
    RgbaImage out;
    out.w = dl.outW;
    out.h = dl.outH;
    out.px.assign(frame.px.size(), 0);
    for (size_t i = 3; i < out.px.size(); i += 4) out.px[i] = 255;
    blitOver(out.px.data(), out.w, out.h, 0, 0, out.w, out.h, frame.px.data(), frame.w, frame.h, 0, 0);
    return out;
}

// ---- list equality -------------------------------------------------------------------------------
namespace {
template <class T, class Eq>
bool samePtr(const std::shared_ptr<T>& a, const std::shared_ptr<T>& b, Eq eq) {
    if (a == b) return true;
    if (!a || !b) return false;
    return eq(*a, *b);
}
bool sameSprite(const Sprite& a, const Sprite& b) { return a.w == b.w && a.h == b.h && a.px == b.px; }
bool sameMatrix(const ColorMatrix& a, const ColorMatrix& b) { return a == b; }
bool sameLayer(const DrawLayer& a, const DrawLayer& b) {
    return a.x == b.x && a.y == b.y && a.hasTint == b.hasTint && a.tintR == b.tintR && a.tintG == b.tintG &&
           a.tintB == b.tintB && a.alpha == b.alpha && a.blend == b.blend && a.relay == b.relay &&
           a.relayBottomY == b.relayBottomY && samePtr(a.cmat, b.cmat, sameMatrix) &&
           samePtr(a.sprite, b.sprite, sameSprite);
}
bool sameLayers(const std::vector<DrawLayer>& a, const std::vector<DrawLayer>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!sameLayer(a[i], b[i])) return false;
    return true;
}
bool sameFilter(const FilterSpec& a, const FilterSpec& b) {
    return a.kind == b.kind && a.size == b.size && a.offset == b.offset && a.r == b.r && a.g == b.g && a.b == b.b &&
           a.alpha == b.alpha && a.flags == b.flags && a.x == b.x && a.y == b.y && a.source == b.source &&
           samePtr(a.cmat, b.cmat, sameMatrix);
}
bool sameMaster(const PlaneMasterSpec& a, const PlaneMasterSpec& b) {
    if (!(a.hasColor == b.hasColor && a.r == b.r && a.g == b.g && a.b == b.b && a.alpha == b.alpha &&
          a.blend == b.blend && a.renderTarget == b.renderTarget && a.unappliedFilters == b.unappliedFilters &&
          a.hasXform == b.hasXform && a.xf == b.xf && samePtr(a.cmat, b.cmat, sameMatrix) &&
          a.filters.size() == b.filters.size()))
        return false;
    for (size_t i = 0; i < a.filters.size(); i++)
        if (!sameFilter(a.filters[i], b.filters[i])) return false;
    return true;
}
}  // namespace

bool samePlanePass(const PlanePass& a, const PlanePass& b) {
    return a.plane == b.plane && samePtr(a.master, b.master, sameMaster) && sameLayers(a.layers, b.layers);
}

bool sameDrawList(const DrawList& a, const DrawList& b) {
    if (a.outW != b.outW || a.outH != b.outH || a.groundBlack != b.groundBlack || a.planes.size() != b.planes.size() ||
        a.hud.size() != b.hud.size())
        return false;
    for (size_t i = 0; i < a.planes.size(); i++) {
        if (!samePlanePass(a.planes[i], b.planes[i])) return false;
    }
    for (size_t i = 0; i < a.hud.size(); i++) {
        const HudPass &p = a.hud[i], &q = b.hud[i];
        if (p.tileW != q.tileW || p.tileH != q.tileH || p.dests.size() != q.dests.size() ||
            !sameLayers(p.layers, q.layers))
            return false;
        for (size_t k = 0; k < p.dests.size(); k++)
            if (p.dests[k].x != q.dests[k].x || p.dests[k].y != q.dests[k].y) return false;
    }
    return true;
}

}  // namespace fugue::render

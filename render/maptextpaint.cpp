// See maptextpaint.h.
#include "render/maptextpaint.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

namespace fugue::render {
namespace {

std::mutex g_mu;
GlyphRasterizer g_raster;
using GlyphKey = std::tuple<std::string, int, bool, bool, bool, char32_t>;
std::map<GlyphKey, std::shared_ptr<const GlyphMask>> g_glyphs;  // a failed glyph is cached as nullptr
using BoxKey = std::tuple<std::string, std::string, int, int>;
std::map<BoxKey, SpritePtr> g_boxes;  // a box is repainted every frame it lives, so paint it once
constexpr size_t kMaxBoxes = 512;

std::shared_ptr<const GlyphMask> glyphLocked(const DsFace& f, char32_t cp) {
    GlyphKey k{f.family, f.em, f.bold, f.italic, f.bitmap, cp};
    auto it = g_glyphs.find(k);
    if (it != g_glyphs.end()) return it->second;
    std::shared_ptr<const GlyphMask> m;
    if (g_raster) {
        auto gm = std::make_shared<GlyphMask>();
        if (g_raster(f, cp, *gm) && gm->w > 0 && gm->h > 0 && gm->a.size() == size_t(gm->w) * size_t(gm->h)) m = gm;
    }
    g_glyphs.emplace(k, m);
    return m;
}

// Straight-alpha source-over of one colour at coverage `cov` (0..255) onto dst (RGBA8).
void over(uint8_t* d, uint32_t rgba, int cov) {
    int sa = int(rgba & 0xff) * cov / 255;
    if (sa <= 0) return;
    double a = sa / 255.0, da = d[3] / 255.0;
    double oa = a + da * (1 - a);
    const int src[3] = {int(rgba >> 24), int((rgba >> 16) & 0xff), int((rgba >> 8) & 0xff)};
    for (int c = 0; c < 3; c++) d[c] = uint8_t(std::clamp(int((src[c] * a + d[c] * da * (1 - a)) / oa + 0.5), 0, 255));
    d[3] = uint8_t(std::clamp(int(oa * 255 + 0.5), 0, 255));
}

}  // namespace

void setGlyphRasterizer(GlyphRasterizer r) {
    std::lock_guard<std::mutex> g(g_mu);
    g_raster = std::move(r);
    g_glyphs.clear();
    g_boxes.clear();
}

SpritePtr paintMaptext(std::string_view markup, std::string_view mapStyle, int boxW, int boxH) {
    if (markup.empty() || boxW <= 0 || boxH <= 0) return nullptr;
    BoxKey bk{std::string(markup), std::string(mapStyle), boxW, boxH};
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_raster) return nullptr;
        auto it = g_boxes.find(bk);
        if (it != g_boxes.end()) return it->second;
    }
    DsLayout L = dsLayoutMaptext(markup, mapStyle, boxW);  // takes dsmaptext's own lock; g_mu is not held here
    SpritePtr result;
    if (L.ok && !L.lines.empty()) {
        auto sp = std::make_shared<Sprite>();
        sp->w = boxW;
        sp->h = boxH;
        sp->px.assign(size_t(boxW) * size_t(boxH) * 4, 0);
        // valign bottom (the default) sets the last line flush with the box bottom.
        const int blockTop = L.valign == 0 ? 0 : L.valign == 1 ? (boxH - L.height) / 2 : boxH - L.height;
        bool painted = false;
        std::lock_guard<std::mutex> g(g_mu);
        // Pass 0: every outline, pass 1: every fill, so a halo never covers a neighbouring glyph's ink.
        for (int pass = 0; pass < 2; pass++) {
            for (const DsLine& ln : L.lines) {
                // A left-aligned line the box cannot hold whole is not drawn; a centred or right-aligned one is clipped.
                if (ln.align == 0 && (blockTop + ln.top < 0 || blockTop + ln.top + ln.height > boxH)) continue;
                const int aw = ln.width + ln.overhang;
                int x0 = ln.align == 1 ? (boxW - aw) / 2 : ln.align == 2 ? boxW - aw : 0;
                int baseline = blockTop + ln.top + ln.ascent;
                for (const DsGlyph& gl : ln.glyphs) {
                    const DsFace& f = L.faces[size_t(gl.face)];
                    const DsPaint& p = L.paints[size_t(gl.paint)];
                    if (pass == 0 && !p.outline) continue;
                    auto m = glyphLocked(f, gl.cp);
                    if (!m) continue;
                    int gx = x0 + gl.x + m->left, gy = baseline - f.ascent + m->top;
                    const int minX = f.bitmap && f.italic ? 1 : 0;  // bitmap italic never inks the box's first column
                    for (int y = (pass == 0 ? -1 : 0); y < m->h + (pass == 0 ? 1 : 0); y++) {
                        int dy = gy + y;
                        if (dy < 0 || dy >= boxH) continue;
                        for (int x = (pass == 0 ? -1 : 0); x < m->w + (pass == 0 ? 1 : 0); x++) {
                            int dx = gx + x;
                            if (dx < minX || dx >= boxW) continue;
                            int cov = 0;
                            if (pass == 1) {
                                cov = m->a[size_t(y) * size_t(m->w) + size_t(x)];
                            } else {  // the coverage grown by one pixel in all eight directions
                                for (int oy = -1; oy <= 1; oy++)
                                    for (int ox = -1; ox <= 1; ox++) {
                                        int sx = x + ox, sy = y + oy;
                                        if (sx < 0 || sy < 0 || sx >= m->w || sy >= m->h) continue;
                                        cov = std::max(cov, int(m->a[size_t(sy) * size_t(m->w) + size_t(sx)]));
                                    }
                            }
                            if (cov <= 0) continue;
                            over(&sp->px[(size_t(dy) * size_t(boxW) + size_t(dx)) * 4],
                                 pass == 0 ? p.outlineColor : p.color, cov);
                            painted = true;
                        }
                    }
                }
            }
        }
        if (painted) result = sp;
        if (g_boxes.size() >= kMaxBoxes) g_boxes.clear();
        g_boxes.emplace(bk, result);
        return result;
    }
    std::lock_guard<std::mutex> g(g_mu);
    if (g_boxes.size() >= kMaxBoxes) g_boxes.clear();
    g_boxes.emplace(bk, result);
    return result;
}

}  // namespace fugue::render

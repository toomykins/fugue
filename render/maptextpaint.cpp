// See maptextpaint.h.
#include "render/maptextpaint.h"
#include "shell/blurkernel.h"  // stock's blur kernel: the one description of it, shared with the map's filters

#include <algorithm>
#include <cmath>
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
std::map<BoxKey, std::pair<SpritePtr, int>> g_boxes;  // a box is repainted every frame it lives, so paint it once
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

// -dm-text-outline and text-shadow are stock's outline and drop-shadow filters run on the drawn text, which they grow
// past the text's box (stock 516.1689: every width, flag and blur probed is the filter's picture to within two
// levels; the outline of ink at x 1 reaches x -1).

// A coverage plane sampled between pixels; nothing outside it.
float sampleAt(const std::vector<float>& a, int w, int h, double x, double y) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = float(x - x0), fy = float(y - y0);
    const auto at = [&](int px, int py) { return px < 0 || py < 0 || px >= w || py >= h ? 0.f : a[size_t(py) * size_t(w) + size_t(px)]; };
    return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

// The steps the outline filter takes for a width, each with its flags (1 sharp, 2 square): one pixel, then 1.75 times
// the last (twice, square) while more than that is left, then the rest, so 3 px is 1, 1.75 and 0.25. A sharp outline
// starts with a square half. shell/compositionplan.cpp runs the same ladder for an atom's outline filter.
std::vector<std::pair<double, int>> outlineSteps(double width, int flags) {
    std::vector<std::pair<double, int>> out;
    const auto ladder = [&](double left, int f) {
        double step = 1;
        if (left <= 1) {
            out.emplace_back(left, f);
            return;
        }
        for (int pass = 0; pass < 16; pass++) {
            if (!(f & 2) && 2 * step >= left && step > 2) step = left * .5;
            out.emplace_back(step, f);
            left -= step;
            step *= f & 2 ? 2 : 1.75;
            if (left <= step) {
                out.emplace_back(left, f);
                break;
            }
        }
    };
    if (flags == 1 && width > 1.5) {
        const double prefix = std::max(1.0, std::floor(width * .5));
        ladder(prefix, flags | 2);
        ladder(width - prefix, flags);
    } else
        ladder(width, flags);
    return out;
}

// One pass of the stock outline: a pixel adds up its own alpha and eight taps `step` px away, the diagonal ones at
// step / sqrt(2) unless square, and takes the outline's alpha times that sum (at most 1; sharp: all or nothing from
// 0.75) on what the picture so far leaves open. `alpha` is the picture's, `halo` the outline's share of it.
void outlinePass(std::vector<float>& alpha, std::vector<float>& halo, int w, int h, double step, int flags, float colourAlpha) {
    const std::vector<float> from = alpha;
    const double d = flags & 2 ? step : step * 0.70710677;
    const double taps[8][2] = {{-step, 0}, {step, 0}, {0, -step}, {0, step}, {-d, -d}, {d, -d}, {-d, d}, {d, d}};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (from[i] >= 1) continue;
            float sum = from[i];
            for (const auto& t : taps) sum += sampleAt(from, w, h, x + t[0], y + t[1]);
            const float cover = flags & 1 ? (sum >= .75f ? 1.f : 0.f) : std::min(1.f, sum);
            const float more = (1 - from[i]) * colourAlpha * cover;
            alpha[i] += more;
            halo[i] += more;
        }
}

// The drop shadow's blur: the filter's kernel (shell/blurkernel.h), across and then down, once per step of a large size.
void shadowBlur(std::vector<float>& a, int w, int h, double size) {
    const auto steps = shell::stockBlurSteps(size);
    for (int s = 0; s < steps.count; s++) {
        const auto kernel = shell::stockBlurKernel(steps.sizes[size_t(s)]);
        for (int axis = 0; axis < 2; axis++) {
            const std::vector<float> from = a;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    float sum = from[size_t(y) * size_t(w) + size_t(x)] * kernel.taps[1];
                    for (size_t k = 1; k < 10 && kernel.taps[2 * k + 1] > 0; k++) {
                        const double o = kernel.taps[2 * k];
                        sum += (axis ? sampleAt(from, w, h, x, y - o) + sampleAt(from, w, h, x, y + o)
                                     : sampleAt(from, w, h, x - o, y) + sampleAt(from, w, h, x + o, y)) *
                               kernel.taps[2 * k + 1];
                    }
                    a[size_t(y) * size_t(w) + size_t(x)] = sum;
                }
        }
    }
}

// How far a paint's outline and shadow reach past the ink.
int decorationReach(const DsPaint& p) {
    int reach = 0;
    if (p.outline)
        for (const auto& step : outlineSteps(p.outlineWidth, p.outlineFlags)) reach += int(std::ceil(step.first));
    if (p.shadow) {
        int blur = 0;
        const auto steps = shell::stockBlurSteps(p.shadowBlur);
        for (int s = 0; s < steps.count; s++) blur += shell::stockBlurKernel(steps.sizes[size_t(s)]).radius;
        reach += std::max(std::abs(p.shadowX), std::abs(p.shadowY)) + blur;
    }
    return reach;
}

}  // namespace

void setGlyphRasterizer(GlyphRasterizer r) {
    std::lock_guard<std::mutex> g(g_mu);
    g_raster = std::move(r);
    g_glyphs.clear();
    g_boxes.clear();
}

SpritePtr paintMaptext(std::string_view markup, std::string_view mapStyle, int boxW, int boxH, int* padOut) {
    if (padOut) *padOut = 0;
    if (markup.empty() || boxW <= 0 || boxH <= 0) return nullptr;
    BoxKey bk{std::string(markup), std::string(mapStyle), boxW, boxH};
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_raster) return nullptr;
        auto it = g_boxes.find(bk);
        if (it != g_boxes.end()) {
            if (padOut) *padOut = it->second.second;
            return it->second.first;
        }
    }
    DsLayout L = dsLayoutMaptext(markup, mapStyle, boxW);  // takes dsmaptext's own lock; g_mu is not held here
    SpritePtr result;
    if (L.ok && !L.lines.empty()) {
        // The sprite is the box and, round it, as far as the outline and shadow reach.
        int pad = 0;
        for (const DsPaint& p : L.paints) pad = std::max(pad, decorationReach(p));
        pad = std::min(pad, 96);
        const int bw = boxW + 2 * pad, bh = boxH + 2 * pad;
        auto sp = std::make_shared<Sprite>();
        sp->w = bw;
        sp->h = bh;
        sp->px.assign(size_t(bw) * size_t(bh) * 4, 0);
        // valign bottom (the default) sets the last line flush with the box bottom.
        const int blockTop = L.valign == 0 ? 0 : L.valign == 1 ? (boxH - L.height) / 2 : boxH - L.height;
        bool painted = false;
        std::lock_guard<std::mutex> g(g_mu);
        // Per paint: the ink of its glyphs and underlines, which ends at the box, and the outline's share of the
        // picture round and between it.
        struct Layer {
            std::vector<float> ink, halo;
        };
        std::vector<Layer> layers(L.paints.size());
        auto at = [&](std::vector<float>& buffer, int x, int y) -> float* {
            if (buffer.empty()) buffer.assign(size_t(bw) * size_t(bh), 0.f);
            return x < 0 || y < 0 || x >= boxW || y >= boxH ? nullptr : &buffer[size_t(y + pad) * size_t(bw) + size_t(x + pad)];
        };
        for (const DsLine& ln : L.lines) {
            // A left-aligned line the box cannot hold whole is not drawn; a centred or right-aligned one is clipped.
            if (ln.align == 0 && (blockTop + ln.top < 0 || blockTop + ln.top + ln.height > boxH)) continue;
            const int aw = ln.width + ln.overhang;
            const int x0 = ln.align == 1 ? (boxW - aw) / 2 : ln.align == 2 ? boxW - aw : 0;
            const int baseline = blockTop + ln.top + ln.ascent;
            for (const DsGlyph& gl : ln.glyphs) {
                const DsFace& f = L.faces[size_t(gl.face)];
                auto m = glyphLocked(f, gl.cp);
                if (!m) continue;
                const int gx = x0 + gl.x + m->left, gy = baseline - f.ascent + m->top;
                const int minX = f.bitmap && f.italic ? 1 : 0;  // bitmap italic never inks the box's first column
                // A TrueType glyph starts at its pen position: the antialiased column GDI puts left of it is not drawn
                // (stock 516.1689: no pixel left of an "l"). A glyph linked into
                // bitmap text keeps that column after another of its run.
                const int penX = f.bitmap || gl.joined ? gx : x0 + gl.x;
                for (int y = 0; y < m->h; y++)
                    for (int x = 0; x < m->w; x++) {
                        if (gx + x < minX || gx + x < penX) continue;
                        const float cov = m->a[size_t(y) * size_t(m->w) + size_t(x)] / 255.f;
                        if (cov <= 0) continue;
                        // (drawn as one run, such glyphs lie over each other where they meet)
                        if (float* d = at(layers[size_t(gl.paint)].ink, gx + x, gy + y))
                            *d = gl.joined ? 1 - (1 - *d) * (1 - cov) : std::max(*d, cov);
                    }
            }
            // An underline is the bottom row of its face's cell, one pixel left of the pen (stock 516.1689:
            // 8pt MS Sans Serif two rows under the baseline row,
            // 16pt five, Tahoma 11pt three; "a b" underlined is 14 px from the box's edge).
            for (const DsUnderline& u : ln.underlines) {
                const DsFace& f = L.faces[size_t(u.face)];
                const int y = baseline + (f.height - f.ascent) - 1;
                for (int x = x0 + u.x - 1; x < x0 + u.x + u.w - 1; x++)
                    if (x >= 0)
                        if (float* d = at(layers[size_t(u.paint)].ink, x, y)) *d = 1.f;
            }
        }
        // The outline is the filter's, on the text as drawn: its alpha is the ink's times the text colour's.
        std::vector<std::vector<float>> shadows(layers.size()), shades(layers.size());
        for (size_t i = 0; i < layers.size(); i++) {
            Layer& layer = layers[i];
            const DsPaint& p = L.paints[i];
            if (layer.ink.empty() || (!p.outline && !p.shadow)) continue;
            std::vector<float> alpha = layer.ink;
            const float textAlpha = float(p.color & 0xff) / 255.f;
            for (float& a : alpha) a *= textAlpha;
            if (p.outline) {
                layer.halo.assign(alpha.size(), 0.f);
                for (const auto& [step, flags] : outlineSteps(p.outlineWidth, p.outlineFlags))
                    outlinePass(alpha, layer.halo, bw, bh, step, flags, float(p.outlineColor & 0xff) / 255.f);
            }
            // The shadow is the outlined text's alpha times the shadow colour's, moved and blurred
            // (stock 516.1689). As in the drop-shadow filter its colour is weighted by that alpha once more, so where
            // the outline is half there the shadow is a quarter of its colour at half alpha.
            if (p.shadow) {
                auto& shadow = shadows[i];
                auto& shade = shades[i];
                shadow.assign(alpha.size(), 0.f);
                shade.assign(alpha.size(), 0.f);
                const float shadowAlpha = float(p.shadowColor & 0xff) / 255.f;
                for (int y = 0; y < bh; y++)
                    for (int x = 0; x < bw; x++) {
                        const int sx = x - p.shadowX, sy = y - p.shadowY;
                        if (sx < 0 || sy < 0 || sx >= bw || sy >= bh) continue;
                        const float a = alpha[size_t(sy) * size_t(bw) + size_t(sx)] * shadowAlpha;
                        shadow[size_t(y) * size_t(bw) + size_t(x)] = a;
                        shade[size_t(y) * size_t(bw) + size_t(x)] = a * a;
                    }
                if (p.shadowBlur > 0) {
                    shadowBlur(shadow, bw, bh, p.shadowBlur);
                    shadowBlur(shade, bw, bh, p.shadowBlur);
                }
            }
            // Drawn under its own ink below, which leaves 1 - ink alpha of it: the outline's share is exact again.
            for (size_t k = 0; k < layer.halo.size(); k++) {
                const float open = 1 - layer.ink[k] * textAlpha;
                layer.halo[k] = open > 0 ? std::min(1.f, layer.halo[k] / open) : 0.f;
            }
        }
        auto draw = [&](const std::vector<float>& coverage, uint32_t colour) {
            if (coverage.empty()) return;
            for (size_t k = 0; k < coverage.size(); k++) {
                const int cov = int(std::clamp(coverage[k], 0.f, 1.f) * 255 + .5f);
                if (cov <= 0) continue;
                over(&sp->px[k * 4], colour, cov);
                painted = true;
            }
        };
        // Every shadow, then every outline, then every fill, so a halo never covers a neighbouring glyph's ink.
        for (size_t i = 0; i < layers.size(); i++)
            for (size_t k = 0; k < shadows[i].size(); k++) {
                const float a = std::min(1.f, shadows[i][k]);
                const int cov = int(a * 255 + .5f);
                if (cov <= 0) continue;
                const float dim = std::min(1.f, shades[i][k] / a);
                const uint32_t c = L.paints[i].shadowColor;
                const auto channel = [&](int shift) { return uint32_t(float((c >> shift) & 0xff) * dim + .5f) << shift; };
                over(&sp->px[k * 4], channel(24) | channel(16) | channel(8) | 0xffu, cov);
                painted = true;
            }
        for (size_t i = 0; i < layers.size(); i++) draw(layers[i].halo, L.paints[i].outlineColor | 0xffu);  // its alpha is in the share
        for (size_t i = 0; i < layers.size(); i++) draw(layers[i].ink, L.paints[i].color);
        if (painted) result = sp;
        if (!painted) pad = 0;
        if (padOut) *padOut = pad;
        if (g_boxes.size() >= kMaxBoxes) g_boxes.clear();
        g_boxes.emplace(bk, std::make_pair(result, pad));
        return result;
    }
    std::lock_guard<std::mutex> g(g_mu);
    if (g_boxes.size() >= kMaxBoxes) g_boxes.clear();
    g_boxes.emplace(bk, std::make_pair(result, 0));
    return result;
}

}  // namespace fugue::render

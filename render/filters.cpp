// Plane-master filters (see filters.h). float64 arithmetic in a fixed operand order.
#include "render/filters.h"

#include <algorithm>
#include <cmath>

#include "render/goexp.h"
#include "render/pixops.h"

namespace fugue::render {

namespace {

RgbaImage blank(int w, int h) {
    RgbaImage o;
    o.w = w;
    o.h = h;
    o.px.assign(size_t(w) * size_t(h) * 4, 0);
    return o;
}

std::vector<double> gaussianKernel(double sigma) {
    if (sigma <= 0) return {1};
    int radius = int(std::ceil(sigma * 3));
    if (radius < 1) radius = 1;
    int ksize = radius * 2 + 1;
    std::vector<double> k(size_t(ksize), 0.0);
    double sum = 0;
    double twoSig2 = 2 * sigma * sigma;
    for (int i = -radius; i <= radius; i++) {
        double v = gomath::exp(-double(i * i) / twoSig2);
        k[size_t(i + radius)] = v;
        sum += v;
    }
    for (double& v : k) v /= sum;
    return k;
}

// convolveSep: one axis, premultiplied float64, sampled edge clamped.
RgbaImage convolveSep(const RgbaImage& src, const std::vector<double>& k, bool horiz) {
    int w = src.w, h = src.h;
    RgbaImage dst = blank(w, h);
    if (w == 0 || h == 0) return dst;
    int radius = int(k.size()) / 2;
    size_t n = size_t(w) * size_t(h);
    std::vector<double> pr(n), pg(n), pb(n), pa(n);
    for (size_t i = 0; i < n; i++) {
        double a = double(src.px[i * 4 + 3]) / 255;
        pr[i] = double(src.px[i * 4]) * a;
        pg[i] = double(src.px[i * 4 + 1]) * a;
        pb[i] = double(src.px[i * 4 + 2]) * a;
        pa[i] = a;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double sr = 0, sg = 0, sb = 0, sa = 0;
            for (int t = -radius; t <= radius; t++) {
                size_t idx = horiz ? size_t(y) * size_t(w) + size_t(std::clamp(x + t, 0, w - 1))
                                   : size_t(std::clamp(y + t, 0, h - 1)) * size_t(w) + size_t(x);
                double wgt = k[size_t(t + radius)];
                sr += pr[idx] * wgt;
                sg += pg[idx] * wgt;
                sb += pb[idx] * wgt;
                sa += pa[idx] * wgt;
            }
            uint8_t* o = &dst.px[(size_t(y) * size_t(w) + size_t(x)) * 4];
            if (sa <= 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            o[0] = clamp8(sr / sa);
            o[1] = clamp8(sg / sa);
            o[2] = clamp8(sb / sa);
            o[3] = clamp8(sa * 255);
        }
    }
    return dst;
}

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
uint8_t unpremul8(double p, double a) {
    if (a <= 0) return 0;
    return uint8_t(std::round(clamp01(p / a) * 255));
}

// outlinePassLadder: the per-pass offsets that add up to `size` (the sharp variant uses the same ladder).
std::vector<double> outlinePassLadder(double size) {
    if (size <= 1) return {size};
    std::vector<double> out;
    double step = 1.0;
    for (;;) {
        if (2 * step >= size && step > 2.0) step = size * 0.5;
        out.push_back(step);
        size -= step;
        step *= 1.75;
        if (size <= step) break;
        if (out.size() > 64) break;
    }
    out.push_back(size);
    return out;
}

// bilerpAlpha: bilinear alpha tap at pixel-index coordinates; outside the layer reads 0 (Border sampler).
double bilerpAlpha(const std::vector<double>& a, int w, int h, double fx, double fy) {
    double x0 = std::floor(fx);
    double y0 = std::floor(fy);
    double tx = fx - x0, ty = fy - y0;
    int ix = int(x0), iy = int(y0);
    auto at = [&](int x, int y) -> double {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return a[size_t(y) * size_t(w) + size_t(x)];
    };
    return (1 - tx) * (1 - ty) * at(ix, iy) + tx * (1 - ty) * at(ix + 1, iy) + (1 - tx) * ty * at(ix, iy + 1) +
           tx * ty * at(ix + 1, iy + 1);
}

// outlinePass: one `technique outline` pass (nine bilinear alpha taps, saturated or thresholded).
std::vector<double> outlinePass(const std::vector<double>& a, int w, int h, double t, bool square, bool sharp,
                                double outlineAlpha) {
    double d = t * 0.70710677;  // float64 literal
    if (square) d = t;
    std::vector<double> out(size_t(w) * size_t(h));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            double fx = double(x), fy = double(y);
            double c = a[size_t(y) * size_t(w) + size_t(x)];
            double s = c;
            s += bilerpAlpha(a, w, h, fx - t, fy) + bilerpAlpha(a, w, h, fx + t, fy);
            s += bilerpAlpha(a, w, h, fx, fy - t) + bilerpAlpha(a, w, h, fx, fy + t);
            s += bilerpAlpha(a, w, h, fx - d, fy - d) + bilerpAlpha(a, w, h, fx + d, fy - d);
            s += bilerpAlpha(a, w, h, fx - d, fy + d) + bilerpAlpha(a, w, h, fx + d, fy + d);
            if (sharp)
                s = (s >= 0.75) ? 1 : 0;
            else if (s > 1)
                s = 1;
            out[size_t(y) * size_t(w) + size_t(x)] = c + (1 - c) * outlineAlpha * s;
        }
    }
    return out;
}

// Maximum alpha within radius r (the dilation a drop shadow's offset needs); pixels outside the image count as alpha 0.
uint8_t maxAlphaWithin(const RgbaImage& src, int x, int y, int r, double dist) {
    int w = src.w, h = src.h;
    double maxD2 = dist * dist;
    uint8_t best = 0;
    for (int dy = -r; dy <= r; dy++) {
        int ny = y + dy;
        if (ny < 0 || ny >= h) continue;
        for (int dx = -r; dx <= r; dx++) {
            int nx = x + dx;
            if (nx < 0 || nx >= w) continue;
            if (double(dx * dx + dy * dy) > maxD2) continue;
            uint8_t a = src.px[(size_t(ny) * size_t(w) + size_t(nx)) * 4 + 3];
            if (a > best) {
                best = a;
                if (best == 255) return 255;
            }
        }
    }
    return best;
}

}  // namespace

RgbaImage filterBlur(const RgbaImage& src, double size) {
    if (size <= 0) return src;
    std::vector<double> k = gaussianKernel(size);
    RgbaImage tmp = convolveSep(src, k, true);
    return convolveSep(tmp, k, false);
}

RgbaImage filterOutline(const RgbaImage& n, double size, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t alpha,
                        uint16_t flags) {
    int w = n.w, h = n.h;
    if (size <= 0 || alpha == 0 || w == 0 || h == 0) return n;
    size_t cnt = size_t(w) * size_t(h);
    std::vector<double> a(cnt), src0(cnt);
    for (size_t i = 0; i < cnt; i++) {
        double v = double(n.px[i * 4 + 3]) / 255;
        a[i] = v;
        src0[i] = v;
    }
    double oa = double(alpha) / 255;
    bool sharp = (flags & 1) != 0;
    bool square = (flags & 2) != 0;
    for (double t : outlinePassLadder(size)) a = outlinePass(a, w, h, t, square, sharp, oa);

    RgbaImage dst = blank(w, h);
    for (size_t i = 0; i < cnt; i++) {
        const uint8_t* s = &n.px[i * 4];
        uint8_t* o = &dst.px[i * 4];
        double sa = src0[i];
        double af = a[i];
        if (af <= sa) {
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            o[3] = s[3];
            continue;
        }
        double ring = af - sa;
        double pr = double(s[0]) / 255 * sa + double(cr) / 255 * ring;
        double pg = double(s[1]) / 255 * sa + double(cg) / 255 * ring;
        double pb = double(s[2]) / 255 * sa + double(cb) / 255 * ring;
        o[0] = unpremul8(pr, af);
        o[1] = unpremul8(pg, af);
        o[2] = unpremul8(pb, af);
        o[3] = uint8_t(std::round(clamp01(af) * 255));
    }
    return dst;
}

RgbaImage filterDropShadow(const RgbaImage& n, int fx, int fy, double size, double offset, uint8_t cr, uint8_t cg,
                           uint8_t cb, uint8_t alpha) {
    int w = n.w, h = n.h;
    RgbaImage sil = blank(w, h);
    double af = double(alpha) / 255;
    int grow = int(std::ceil(offset));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t sa = n.px[(size_t(y) * size_t(w) + size_t(x)) * 4 + 3];
            if (grow > 0) sa = maxAlphaWithin(n, x, y, grow, offset);
            uint8_t* o = &sil.px[(size_t(y) * size_t(w) + size_t(x)) * 4];
            o[0] = cr;
            o[1] = cg;
            o[2] = cb;
            o[3] = clamp8(double(sa) * af);
        }
    }
    if (size > 0) sil = filterBlur(sil, size);
    RgbaImage dst = blank(w, h);
    blendLayer(dst.px.data(), w, h, sil.px.data(), sil.w, sil.h, fx, fy, LayerFx{}, kBlendDefault);
    // The source over the shadow at every pixel, with no shortcuts.
    for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
        Rgba8 o = overPx(Rgba8{n.px[i * 4], n.px[i * 4 + 1], n.px[i * 4 + 2], n.px[i * 4 + 3]},
                         Rgba8{dst.px[i * 4], dst.px[i * 4 + 1], dst.px[i * 4 + 2], dst.px[i * 4 + 3]});
        dst.px[i * 4] = o.r;
        dst.px[i * 4 + 1] = o.g;
        dst.px[i * 4 + 2] = o.b;
        dst.px[i * 4 + 3] = o.a;
    }
    return dst;
}

// The mask is centred on the image, then moved by (x, y). Outside the mask its alpha is 0; a null mask is all outside.
RgbaImage filterAlphaMask(const RgbaImage& src, const RgbaImage* mask, int x, int y, uint16_t flags) {
    const bool inverse = flags & 1, swap = flags & 2;
    RgbaImage dst = blank(src.w, src.h);
    const int mx0 = mask ? x + (src.w - mask->w) / 2 : 0, my0 = mask ? y + (src.h - mask->h) / 2 : 0;
    for (int py = 0; py < src.h; py++)
        for (int px = 0; px < src.w; px++) {
            const uint8_t* s = &src.px[(size_t(py) * size_t(src.w) + size_t(px)) * 4];
            static const uint8_t kNone[4] = {0, 0, 0, 0};
            const uint8_t* m = kNone;
            const int mx = px - mx0, my = py - my0;
            if (mask && mx >= 0 && my >= 0 && mx < mask->w && my < mask->h)
                m = &mask->px[(size_t(my) * size_t(mask->w) + size_t(mx)) * 4];
            const uint8_t* colour = swap ? m : s;
            double keep = double(swap ? s[3] : m[3]);
            if (inverse) keep = 255 - keep;
            const uint8_t a = clamp8(double(swap ? m[3] : s[3]) * keep / 255.0);
            if (a == 0) continue;
            uint8_t* d = &dst.px[(size_t(py) * size_t(src.w) + size_t(px)) * 4];
            d[0] = colour[0];
            d[1] = colour[1];
            d[2] = colour[2];
            d[3] = a;
        }
    return dst;
}

}  // namespace fugue::render

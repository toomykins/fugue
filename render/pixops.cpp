// Pixel operations of the compositor (see pixops.h). The arithmetic is float64 in a fixed operand order and this module
// is compiled with -ffp-contract=off.
#include "render/pixops.h"

#include <algorithm>
#include <cstring>

namespace fugue::render {

namespace {
int g_mut = 0;

// The matrix's constant row: 255*m[16+c], unit-scaled.
struct MatrixOp {
    const ColorMatrix& m;
    double k0, k1, k2, k3;  // the offset added to each output channel
    Rgba8 z;                // the result for a fully blank input pixel: the offset alone, clamped
    explicit MatrixOp(const ColorMatrix& mm) : m(mm) {
        if (g_mut == 10) {
            k0 = m[16];
            k1 = m[17];
            k2 = m[18];
            k3 = m[19];
        } else {
            k0 = 255 * m[16];
            k1 = 255 * m[17];
            k2 = 255 * m[18];
            k3 = 255 * m[19];
        }
        z = Rgba8{clamp8(k0), clamp8(k1), clamp8(k2), clamp8(k3)};
    }
    Rgba8 operator()(Rgba8 p) const {
        if ((p.r | p.g | p.b | p.a) == 0) return z;
        double r = double(p.r), g = double(p.g), bl = double(p.b), a = double(p.a);
        return Rgba8{clamp8(r * m[0] + g * m[4] + bl * m[8] + a * m[12] + k0),
                     clamp8(r * m[1] + g * m[5] + bl * m[9] + a * m[13] + k1),
                     clamp8(r * m[2] + g * m[6] + bl * m[10] + a * m[14] + k2),
                     clamp8(r * m[3] + g * m[7] + bl * m[11] + a * m[15] + k3)};
    }
};

// blendSubMul: BLEND_SUBTRACT / BLEND_MULTIPLY, matching stock DreamSeeker 516 exactly. The formulas for a
// PREMULTIPLIED plane buffer whose alpha is never grown:
//   SUBTRACT  out_pm = max(0, dc*da - sc*sa),            out_a = da     -> oc = max(0, dc - sc*sa/da)
//   MULTIPLY  out_pm = dc*da * (sc/255 + 1 - sa),        out_a = da     -> oc = dc*(1-sa) + sc*dc/255
// An EMPTY destination (da = 0) draws nothing at any source alpha; a fully transparent source is skipped.
Rgba8 blendSubMul(Rgba8 s, Rgba8 d, bool subtract) {
    if (d.a == 0) return Rgba8{0, 0, 0, 0};
    if (s.a == 0) return d;
    double sa = double(s.a) / 255;
    double da = double(d.a) / 255;
    if (subtract) {
        auto ch = [&](uint8_t sc, uint8_t dc) { return clamp8(double(dc) - double(sc) * sa / da); };
        return Rgba8{ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b), d.a};
    }
    double inv = 1 - sa;
    auto ch = [&](uint8_t sc, uint8_t dc) { return clamp8(double(dc) * inv + double(sc) * double(dc) / 255); };
    return Rgba8{ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b), d.a};
}

// blendMulPlane: BLEND_MULTIPLY with a PREMULTIPLIED source (a finished plane buffer blended onto the scene).
Rgba8 blendMulPlane(Rgba8 s, Rgba8 d) {
    if (d.a == 0) return Rgba8{0, 0, 0, 0};
    if (s.a == 0) return d;
    double sa = double(s.a) / 255;
    double inv = 1 - sa;
    auto ch = [&](uint8_t sc, uint8_t dc) { return clamp8(double(dc) * inv + double(sc) * double(dc) / 255 * sa); };
    return Rgba8{ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b), d.a};
}

// channelwiseMode (BLEND_ADD / SUBTRACT / MULTIPLY).
//   ADD       normalised by the output alpha: ((1-da)*sa*Cs + (1-sa)*da*Cd + sa*da*(Cd+Cs)) / oa, oa = union
//   SUBTRACT / MULTIPLY  blendSubMul above (destination alpha preserved; exact against stock DreamSeeker).
Rgba8 channelwiseMode(Rgba8 s, Rgba8 d, int mode) {
    if (s.a == 255) {
        if (mode == kBlendMultiply) {
            if (d.a == 0) return Rgba8{0, 0, 0, 0};
            return Rgba8{clamp8(double(s.r) * double(d.r) / 255), clamp8(double(s.g) * double(d.g) / 255),
                         clamp8(double(s.b) * double(d.b) / 255), d.a};
        }
        if (mode == kBlendSubtract && d.a == 255) {
            auto sub = [](uint8_t sc, uint8_t dc) -> uint8_t { return dc <= sc ? 0 : uint8_t(dc - sc); };
            return Rgba8{sub(s.r, d.r), sub(s.g, d.g), sub(s.b, d.b), 255};
        }
    }
    if (mode == kBlendSubtract || mode == kBlendMultiply) return blendSubMul(s, d, mode == kBlendSubtract);
    double sa = double(s.a) / 255;
    double da = double(d.a) / 255;
    double sr = double(s.r), sg = double(s.g), sb = double(s.b);
    double dr = double(d.r), dg = double(d.g), db = double(d.b);
    double oa = sa + da * (1 - sa);
    uint8_t oa8 = clamp8(oa * 255);
    if (oa <= 0) return Rgba8{0, 0, 0, 0};
    if (g_mut == 16) oa8 = d.a;
    if (g_mut == 13) {
        double inv = 1 - sa;
        auto ch = [&](double dc, double b) { return clamp8(dc * inv + b * sa); };
        return Rgba8{ch(dr, dr + sr), ch(dg, dg + sg), ch(db, db + sb), oa8};
    }
    double srcTerm = (1 - da) * sa;
    double dstTerm = (1 - sa) * da;
    double both = sa * da;
    auto ch = [&](double sc, double dc, double b) { return clamp8((srcTerm * sc + dstTerm * dc + both * b) / oa); };
    return Rgba8{ch(sr, dr, dr + sr), ch(sg, dg, dg + sg), ch(sb, db, db + sb), oa8};
}

// insetOverlay: the source is masked by the destination's own alpha, drawn over, and the destination
// alpha is restored (the silhouette never grows).
Rgba8 insetOverlay(Rgba8 s, Rgba8 d) {
    double dm = double(d.a) / 255;
    Rgba8 masked{s.r, s.g, s.b, clamp8(double(s.a) * dm)};
    Rgba8 out = overPx(masked, d);
    out.a = d.a;
    return out;
}
}  // namespace

void setPixopsMutation(int m) { g_mut = m; }
int pixopsMutation() { return g_mut; }

uint8_t clamp8(double v) {
    if (v <= 0) return 0;
    if (v >= 255) return 255;
    if (g_mut == 1) return uint8_t(v);  // test-only: truncate
    return uint8_t(v + 0.5);
}

Rgba8 overPx(Rgba8 s, Rgba8 d) {
    double sa = double(s.a) / 255;
    double da = double(d.a) / 255;
    double oa = sa + da * (1 - sa);
    if (oa <= 0) return Rgba8{0, 0, 0, 0};
    auto ch = [&](uint8_t sc, uint8_t dc) {
        double v = (double(sc) * sa + double(dc) * da * (1 - sa)) / oa;
        return clamp8(v);
    };
    return Rgba8{ch(s.r, d.r), ch(s.g, d.g), ch(s.b, d.b), clamp8(oa * 255)};
}

void blendLayer(uint8_t* dst, int dw, int dh, const uint8_t* spx, int sw, int sh, int offX, int offY, const LayerFx& fx,
                int mode, int dstY0, int dstY1, bool planeSurface) {
    if (dstY1 < 0) dstY1 = dh;
    int x0 = 0, y0 = 0, x1 = sw, y1 = sh;
    if (offX < 0) x0 = -offX;
    if (offY < 0) y0 = -offY;
    if (x1 > dw - offX) x1 = dw - offX;
    if (y1 > dh - offY) y1 = dh - offY;
    if (y0 < dstY0 - offY) y0 = dstY0 - offY;
    if (y1 > dstY1 - offY) y1 = dstY1 - offY;
    if (x0 >= x1 || y0 >= y1) return;

    // Tint and alpha (alpha clamped to 0..255, colour scaled by mul/255 as float64), then the matrix.
    int alpha = std::clamp(fx.alpha, 0, 255);
    bool tintOrAlpha = fx.hasTint || alpha != 255;
    double mr = 1, mg = 1, mb = 1;
    if (fx.hasTint) {
        mr = double(fx.tr) / 255;
        mg = double(fx.tg) / 255;
        mb = double(fx.tb) / 255;
    }
    double ma = double(alpha) / 255;
    std::unique_ptr<MatrixOp> mat;
    if (fx.cm) mat = std::make_unique<MatrixOp>(*fx.cm);

    const bool fast = mode == kBlendDefault || mode == kBlendOver;
    const bool channelwise = mode == kBlendAdd || mode == kBlendSubtract || mode == kBlendMultiply;
    const bool skipClear = fast || channelwise;
    for (int sy = y0; sy < y1; sy++) {
        const uint8_t* s = &spx[(size_t(sy) * size_t(sw) + size_t(x0)) * 4];
        uint8_t* d = &dst[(size_t(sy + offY) * size_t(dw) + size_t(x0 + offX)) * 4];
        for (int x = x0; x < x1; x++, s += 4, d += 4) {
            Rgba8 p{s[0], s[1], s[2], s[3]};
            if (tintOrAlpha) {
                p.r = clamp8(double(s[0]) * mr);
                p.g = clamp8(double(s[1]) * mg);
                p.b = clamp8(double(s[2]) * mb);
                p.a = clamp8(double(s[3]) * ma);
            }
            if (mat) p = (*mat)(p);
            if (p.a == 0 && skipClear) {
                if (fast && d[3] == 0) { d[0] = d[1] = d[2] = 0; }
                continue;
            }
            if (p.a == 255 && fast) {
                d[0] = p.r;
                d[1] = p.g;
                d[2] = p.b;
                d[3] = 255;
                continue;
            }
            Rgba8 dp{d[0], d[1], d[2], d[3]};
            Rgba8 o = channelwise
                          ? ((planeSurface && mode == kBlendMultiply && p.a != 255) ? blendMulPlane(p, dp)
                                                                                    : channelwiseMode(p, dp, mode))
                          : (mode == kBlendInset ? insetOverlay(p, dp) : overPx(p, dp));
            d[0] = o.r;
            d[1] = o.g;
            d[2] = o.b;
            d[3] = o.a;
        }
    }
}

void applyColorMatrixInPlace(uint8_t* px, size_t npix, const ColorMatrix& m) {
    MatrixOp op(m);
    for (size_t i = 0; i < npix; i++, px += 4) {
        Rgba8 o = op(Rgba8{px[0], px[1], px[2], px[3]});
        px[0] = o.r;
        px[1] = o.g;
        px[2] = o.b;
        px[3] = o.a;
    }
}

void applyColorInPlace(uint8_t* px, size_t npix, uint8_t r, uint8_t g, uint8_t b, int alpha) {
    int a = std::clamp(alpha, 0, 255);
    double mr = double(r) / 255, mg = double(g) / 255, mb = double(b) / 255, ma = double(a) / 255;
    for (size_t i = 0; i < npix; i++, px += 4) {
        px[0] = clamp8(double(px[0]) * mr);
        px[1] = clamp8(double(px[1]) * mg);
        px[2] = clamp8(double(px[2]) * mb);
        px[3] = clamp8(double(px[3]) * ma);
    }
}

}  // namespace fugue::render

// See xform.h.
#include "render/xform.h"

#include <algorithm>
#include <cmath>

#include "model/gomath.h"

namespace fugue::render {

namespace {

struct Px {
    uint8_t r = 0, g = 0, b = 0, a = 0;
};

// clamp8: uint8(v+0.5) once 0 < v < 255 is established.
uint8_t clamp8(double v) {
    if (v <= 0) return 0;
    if (v >= 255) return 255;
    return uint8_t(v + 0.5);
}

// Out-of-bounds pixels are transparent.
Px at(const Sprite& s, int x, int y) {
    if (x < 0 || y < 0 || x >= s.w || y >= s.h) return Px{};
    const uint8_t* p = &s.px[(size_t(y) * size_t(s.w) + size_t(x)) * 4];
    return Px{p[0], p[1], p[2], p[3]};
}

// Rounds half away from zero.
Px sampleNearest(const Sprite& s, double x, double y) { return at(s, int(std::round(x)), int(std::round(y))); }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// sampleBilinear: premultiplied-space interpolation.
Px sampleBilinear(const Sprite& s, double x, double y) {
    int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    double fx = x - double(x0), fy = y - double(y0);
    Px c00 = at(s, x0, y0), c10 = at(s, x0 + 1, y0), c01 = at(s, x0, y0 + 1), c11 = at(s, x0 + 1, y0 + 1);
    struct F {
        double r, g, b, a;
    };
    auto pm = [](Px c) {
        double a = double(c.a) / 255;
        return F{double(c.r) * a, double(c.g) * a, double(c.b) * a, a};
    };
    F p00 = pm(c00), p10 = pm(c10), p01 = pm(c01), p11 = pm(c11);
    double r = lerp(lerp(p00.r, p10.r, fx), lerp(p01.r, p11.r, fx), fy);
    double g = lerp(lerp(p00.g, p10.g, fx), lerp(p01.g, p11.g, fx), fy);
    double b = lerp(lerp(p00.b, p10.b, fx), lerp(p01.b, p11.b, fx), fy);
    double a = lerp(lerp(p00.a, p10.a, fx), lerp(p01.a, p11.a, fx), fy);
    if (a <= 0) return Px{};
    return Px{clamp8(r / a), clamp8(g / a), clamp8(b / a), clamp8(a * 255)};
}

Mat translate(double dx, double dy) { return Mat{1, 0, dx, 0, 1, dy}; }

bool invert(const Mat& m, Mat& out) {
    double det = m.a * m.e - m.b * m.d;
    if (det == 0) return false;
    double id = 1 / det;
    double ia = m.e * id, ib = -m.b * id, idd = -m.d * id, ie = m.a * id;
    double ic = -(ia * m.c + ib * m.f);
    double iff = -(idd * m.c + ie * m.f);
    out = Mat{ia, ib, ic, idd, ie, iff};
    return true;
}

void apply(const Mat& m, double x, double y, double& ox, double& oy) {
    ox = m.a * x + m.b * y + m.c;
    oy = m.d * x + m.e * y + m.f;
}

struct Decomposed {
    double scaleX = 0, scaleY = 0, shear = 0, angle = 0, tx = 0, ty = 0;
};

bool decompose(const Mat& m, Decomposed& out) {
    double scaleX = gomath::hypot(m.a, m.d);
    if (scaleX == 0) return false;
    double angle = gomath::atan2(m.d, m.a);
    double cs = gomath::cos(angle), sn = gomath::sin(angle);
    double shear = cs * m.b + sn * m.e;
    double scaleY = -sn * m.b + cs * m.e;
    out = Decomposed{scaleX, scaleY, shear, angle, m.c, m.f};
    return true;
}

Mat recompose(const Decomposed& s) {
    double cs = gomath::cos(s.angle), sn = gomath::sin(s.angle);
    return Mat{s.scaleX * cs, s.shear * cs - s.scaleY * sn, s.tx, s.scaleX * sn, s.shear * sn + s.scaleY * cs, s.ty};
}

}  // namespace

bool invertMat(const Mat& m, Mat& out) { return invert(m, out); }

Mat matMul(const Mat& m, const Mat& n) {
    return Mat{m.a * n.a + m.b * n.d, m.a * n.b + m.b * n.e, m.a * n.c + m.b * n.f + m.c,
               m.d * n.a + m.e * n.d, m.d * n.b + m.e * n.e, m.d * n.c + m.e * n.f + m.f};
}

Mat dmMatrixToImage(const std::array<float, 6>& t) {
    return Mat{double(t[0]), -double(t[1]), double(t[2]), -double(t[3]), double(t[4]), -double(t[5])};
}

SpritePtr applyTransform(const Sprite& src, const Mat& m, Sampling s) {
    int w = src.w, h = src.h;
    auto out = std::make_shared<Sprite>();
    if (w == 0 || h == 0) {
        out->w = w;
        out->h = h;
        return out;
    }
    double cx = double(w) / 2, cy = double(h) / 2;
    Mat linear{m.a, m.b, 0, m.d, m.e, 0};
    double corners[4][2] = {{-cx, -cy}, {cx, -cy}, {-cx, cy}, {cx, cy}};
    double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (auto& c : corners) {
        double x, y;
        apply(linear, c[0], c[1], x, y);
        minX = std::fmin(minX, x);
        maxX = std::fmax(maxX, x);
        minY = std::fmin(minY, y);
        maxY = std::fmax(maxY, y);
    }
    int newW = int(std::ceil(maxX - minX - 1e-9));
    int newH = int(std::ceil(maxY - minY - 1e-9));
    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;
    const int maxGrowSrcDim = 256;  // the plane-surface performance guard
    if (w > maxGrowSrcDim || h > maxGrowSrcDim) {
        if (newW > w) newW = w;
        if (newH > h) newH = h;
    }
    out->w = newW;
    out->h = newH;
    out->px.assign(size_t(newW) * size_t(newH) * 4, 0);
    double dcx = double(newW) / 2, dcy = double(newH) / 2;
    Mat full = matMul(matMul(translate(dcx, dcy), m), translate(-cx, -cy));
    Mat inv;
    if (!invert(full, inv)) return out;  // degenerate transform collapses to nothing
    for (int y = 0; y < newH; y++)
        for (int x = 0; x < newW; x++) {
            double sx, sy;
            apply(inv, double(x) + 0.5, double(y) + 0.5, sx, sy);
            sx -= 0.5;
            sy -= 0.5;
            Px c = (s == Sampling::Bilinear) ? sampleBilinear(src, sx, sy) : sampleNearest(src, sx, sy);
            uint8_t* d = &out->px[(size_t(y) * size_t(newW) + size_t(x)) * 4];
            d[0] = c.r;
            d[1] = c.g;
            d[2] = c.b;
            d[3] = c.a;
        }
    return out;
}

void warpAboutCentre(std::vector<uint8_t>& px, int w, int h, double a, double b, double d, double e, double tx,
                     double ty) {
    Sprite src;
    src.w = w;
    src.h = h;
    src.px = px;
    std::fill(px.begin(), px.end(), uint8_t(0));
    double det = a * e - b * d;
    if (det == 0 || w == 0 || h == 0) return;
    double inv00 = e / det, inv01 = -b / det;
    double inv10 = -d / det, inv11 = a / det;
    double cx = double(w) / 2, cy = double(h) / 2;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double dxP = double(x) + 0.5 - cx - tx;
            double dyP = -(double(y) + 0.5 - cy - ty);
            double dxDm = inv00 * dxP + inv01 * dyP;
            double dyDm = inv10 * dxP + inv11 * dyP;
            double u = cx + dxDm - 0.5;
            double v = cy - dyDm - 0.5;
            Px c = sampleBilinear(src, u, v);
            if (c.a != 0) {
                uint8_t* o = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
                o[0] = c.r;
                o[1] = c.g;
                o[2] = c.b;
                o[3] = c.a;
            }
        }
}

bool lerpMatrix(const Mat& from, const Mat& to, double t, Mat& out) {
    Decomposed fs, ts;
    if (!decompose(from, fs) || !decompose(to, ts)) return false;
    double delta = ts.angle - fs.angle;
    while (delta > 180) delta -= 360;  // angles are in radians, so these never fire
    while (delta < -180) delta += 360;
    Decomposed s;
    s.scaleX = fs.scaleX + (ts.scaleX - fs.scaleX) * t;
    s.scaleY = fs.scaleY + (ts.scaleY - fs.scaleY) * t;
    s.shear = fs.shear + (ts.shear - fs.shear) * t;
    s.angle = fs.angle + delta * t;
    s.tx = fs.tx + (ts.tx - fs.tx) * t;
    s.ty = fs.ty + (ts.ty - fs.ty) * t;
    out = recompose(s);
    return true;
}

}  // namespace fugue::render

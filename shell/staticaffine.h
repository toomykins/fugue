// Worker-validated, model-free source-pixel geometry. BYOND matrices are
// [a,b,c,d,e,f], x'=a*x+b*y+c, y'=d*x+e*y+f, about the icon center (Y up).
#pragma once
#include <QTransform>
#include <QRect>
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>

namespace fugue::shell {
struct StaticAffine {
    QTransform forward, inverse; // source top-left/Y-down pixels, relative to sprite.position
    QRect bounds;
    bool visualCoordinates = false;
    // When set, a bilinear sample reads the surface as though it ended at this rectangle (whole surface pixels):
    // its edge texels repeat past it. Past the surface's own edge it stays clear. Empty for no limit.
    QRect sampled;
    bool operator==(const StaticAffine&) const = default;
    static std::optional<StaticAffine> make(const std::array<double, 6>& m, QSize size) {
        if (size.isEmpty() || size.width() > 8192 || size.height() > 8192) return {};
        for (int i = 0; i < 6; ++i)
            if (!std::isfinite(m[i]) || std::abs(m[i]) > (i == 2 || i == 5 ? 8192 : 256)) return {};
        const double det = m[0]*m[4]-m[1]*m[3];
        if (!std::isfinite(det) || std::abs(det) < 1e-8) return {};
        const double x = size.width()/2.0, y = size.height()/2.0;
        StaticAffine out;
        out.forward = QTransform(m[0], -m[3], -m[1], m[4],
                                 x-m[0]*x+m[1]*y+m[2], y+m[3]*x-m[4]*y-m[5]);
        const QRectF bounds = out.forward.mapRect(QRectF(QPointF(), size));
        if (std::max({std::abs(bounds.left()), std::abs(bounds.right()),
                      std::abs(bounds.top()), std::abs(bounds.bottom())}) > 65536) return {};
        bool ok = false;
        out.inverse = out.forward.inverted(&ok);
        if (!ok || std::max({std::abs(out.inverse.m11()), std::abs(out.inverse.m12()),
                             std::abs(out.inverse.m21()), std::abs(out.inverse.m22())}) > 65536) return {};
        out.bounds = bounds.toAlignedRect();
        out.visualCoordinates = m[0] != 1 || m[1] != 0 || m[3] != 0 || m[4] != 1;
        return out;
    }
};
}

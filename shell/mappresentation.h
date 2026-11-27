// Final MAP resampling only. Scene pixels remain at native icon resolution.
#pragma once
#include <QMargins>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace fugue::shell {
struct MapPresentationSettings {
    enum Mode { Distort, Normal, Blur } mode = Normal;
    double zoom = 0, iconSize = 0;
    bool letterbox = true;
    bool operator==(const MapPresentationSettings&) const = default;
};
struct MapPresentation {
    QRectF destination; // logical widget coordinates; retained with the rendered scene
    double scale = 0; // logical pixels per native scene pixel
    bool linear = false;
    bool sharpen = false; // Normal's bounded pixel-edge transition, in physical pixels
};
inline MapPresentation mapPresentation(QSizeF widget, QSize scene, int tile,
                                       MapPresentationSettings settings = {}) {
    MapPresentation out;
    if (widget.isEmpty() || scene.isEmpty() || tile <= 0) return out;
    const auto positive = [](double v) { return std::isfinite(v) && v > 0; };
    if (positive(settings.zoom)) out.scale = settings.zoom;
    else if (positive(settings.iconSize)) out.scale = settings.iconSize / tile;
    else {
        const double x = widget.width() / scene.width(), y = widget.height() / scene.height();
        out.scale = settings.letterbox ? std::min(x, y) : std::max(x, y);
    }
    // Normal is not ordinary bilinear magnification. The final shader narrows
    // its transition using the actual framebuffer scale (not logical zoom).
    // Minification remains linear; Distort's point sampler is unchanged.
    out.linear = settings.mode != MapPresentationSettings::Distort;
    out.sharpen = settings.mode == MapPresentationSettings::Normal;
    const QSizeF extent(scene.width() * out.scale, scene.height() * out.scale);
    if (!std::isfinite(extent.width()) || !std::isfinite(extent.height()) ||
        extent.width() > std::numeric_limits<int>::max() || extent.height() > std::numeric_limits<int>::max()) return {};
    out.destination = QRectF(QPointF((widget.width()-extent.width())/2, (widget.height()-extent.height())/2), extent);
    return out;
}
// Where in the scene (in its own pixels, the top left at 0,0) stock draws the world: turfs, objects, images and vis
// copies; screen objects draw everywhere. When screen objects outside the view have enlarged the map (`hud` tiles of
// `tile` pixels on each side), stock 516.1689 takes, on each axis, the part of the view (the scene less those tiles)
// that the control shows, and where the scene is larger than the control and so starts left of (above) it, moves
// that part right (down) by what is cut off. So the world past the view shows only where that part reaches, and the
// control's left (top) edge has a black strip as wide as the cut, on an axis without added tiles too. A map that is
// not enlarged is drawn whole, centred and cut by the control, with no strip. (master-screen-loc-world "|wide"
// phases in controls of 192, 224, 240 and 288 pixels with 32 pixel tiles.) No result is no clip. In a scaled map the
// rule holds in scene pixels (icon-size 48 in a control of 336 pixels: the 224 pixel case magnified 1.5 times). A turned
// world sprite is cut by it too.
inline std::optional<QRect> mapWorldClip(const MapPresentation& p, QSizeF widget, QSize scene, QMargins hud, int tile) {
    if (hud.isNull() || p.destination.isEmpty() || !(p.scale > 0) || scene.isEmpty() || tile <= 0) return {};
    const auto axis = [&](double offset, double control, int size, int nearTiles, int farTiles, int& from, int& to) {
        const double cut = -offset / p.scale;           // scene pixels left of (above) the control, negative if none
        const double shown = control / p.scale;          // the control's width in scene pixels
        const int viewFrom = nearTiles * tile, viewTo = size - farTiles * tile;
        const double moved = std::max(0.0, cut);
        from = int(std::lround(std::max<double>(viewFrom, cut) + moved));
        to = int(std::lround(std::min<double>(viewTo, cut + shown) + moved));
    };
    int x0, x1, y0, y1;
    axis(p.destination.x(), widget.width(), scene.width(), hud.left(), hud.right(), x0, x1);
    axis(p.destination.y(), widget.height(), scene.height(), hud.top(), hud.bottom(), y0, y1);
    if (x0 <= 0 && y0 <= 0 && x1 >= scene.width() && y1 >= scene.height()) return {}; // all of the scene
    return QRect(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}
// The view in the scene, in the scene's own pixels: the scene less the tiles screen objects outside the view added.
// Stock 516.1689 draws a world atom coloured by a colour matrix as a view-sized picture of it stretched from this
// rectangle onto mapWorldClip's (master-screen-loc-world "|wide-fx" phases).
inline QRect mapWorldView(QSize scene, QMargins hud, int tile) {
    return QRect(QPoint(), scene) - hud * tile;
}
}

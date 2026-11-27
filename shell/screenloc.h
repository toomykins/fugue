// Bounded, deliberately partial screen_loc grammar. Logical pixels, bottom-left origin.
#pragma once
#include <QMargins>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QRegularExpression>
#include <optional>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace fugue::shell {
struct ScreenLoc {
    enum class Anchor { Tile, NearEdge, FarEdge };
    // percent: a share of the view less one tile, so LEFT+100% is the last column (stock 516.1689,
    // stock-parity-world screen-loc-forms, screen-loc-percent-ranges; a bare 50% counts from LEFT or BOTTOM).
    struct Axis { double tiles = 1, relative = 0, percent = 0; int pixels = 0; Anchor anchor = Anchor::Tile; };
    struct Point { Axis x, y; };
    Point first, last;
    bool range = false;
    // A map selector is not a coordinate. Keep it even when the remaining
    // placement is unsupported, so a foreign plane master cannot select a plane.
    struct Target {
        QString map, location;
    };
    static std::optional<Target> target(QString text) {
        if (text.size() > 256) return {};
        text = text.trimmed();
        const auto colon = text.indexOf(':');
        const auto comma = text.indexOf(',');
        if (colon >= 0 && (comma < 0 || colon < comma)) {
            // The colon in an ordinary axis is a pixel offset, not a map
            // selector (including vertical-first and signed/fractional axes).
            static const QRegularExpression axis(
                R"(^(WEST|EAST|SOUTH|NORTH|TOP|BOTTOM|LEFT|RIGHT|CENTER|[+-]?[0-9]+(?:\.[0-9]{1,3})?%?)([+-]{1,2}[0-9]+(?:\.[0-9]{1,3})?%?)?$)",
                QRegularExpression::CaseInsensitiveOption);
            const auto prefix = text.left(colon);
            if (!axis.match(prefix).hasMatch()) {
                if (prefix.isEmpty()) return {};
                return Target{prefix, text.mid(colon + 1).trimmed()};
            }
        }
        return Target{{}, text};
    }
    // Coordinate-only grammar. Call target() first for client.screen routing.
    // malformed: set when the text is not coordinates at all ("bogus"), as opposed to a form this does not place.
    static std::optional<ScreenLoc> parse(QString text, bool* malformed = nullptr) {
        if (malformed) *malformed = false;
        if (text.size() > 256) return {};
        text = text.trimmed().toUpper(); // stock reads center-6 and Center like CENTER
        ScreenLoc result;
        const auto ends = text.split(" TO ");
        if (ends.size() > 2) return {};
        auto point = [malformed](QString s) -> std::optional<Point> {
            s = s.trimmed();
            if (s == "CENTER") s = "CENTER,CENTER";
            else if (s == "SOUTHWEST") s = "WEST,SOUTH";
            else if (s == "SOUTHEAST") s = "EAST,SOUTH";
            else if (s == "NORTHWEST") s = "WEST,NORTH";
            else if (s == "NORTHEAST") s = "EAST,NORTH";
            else if (s == "TOPLEFT") s = "LEFT,TOP";
            else if (s == "TOPRIGHT") s = "RIGHT,TOP";
            else if (s == "BOTTOMLEFT") s = "LEFT,BOTTOM";
            else if (s == "BOTTOMRIGHT") s = "RIGHT,BOTTOM";
            auto parts = s.split(',');
            if (parts.size() != 2) { if (malformed) *malformed = true; return {}; }
            // Whitespace belongs to the separators, not to an axis token. Normalize
            // every axis before detecting its orientation, including range endpoints.
            for (auto& part : parts) part = part.trimmed();
            // Stock admits vertical anchors first, but not horizontal anchors second
            // after an ambiguous CENTER/numeric axis (e.g. CENTER,RIGHT is not RIGHT,CENTER).
            if (parts[0].startsWith("NORTH") || parts[0].startsWith("SOUTH") ||
                parts[0].startsWith("TOP") || parts[0].startsWith("BOTTOM")) parts.swapItemsAt(0, 1);
            static const QRegularExpression expression(
                R"(^(WEST|EAST|SOUTH|NORTH|TOP|BOTTOM|LEFT|RIGHT|CENTER|[+-]?[0-9]+(?:\.[0-9]{1,3})?%?)([+-]{1,2}[0-9]+(?:\.[0-9]{1,3})?%?)?(?::([+-]?[0-9]+))?$)");
            Point p;
            for (int i = 0; i < 2; ++i) {
                const auto m = expression.match(parts[i]);
                if (!m.hasMatch()) { if (malformed) *malformed = true; return {}; }
                Axis& a = i ? p.y : p.x;
                const auto base = m.captured(1);
                if (base == "CENTER") { a.tiles = .5; a.relative = .5; }
                else if (base == (i ? "NORTH" : "EAST")) { a.tiles = 0; a.relative = 1; }
                else if (base == (i ? "TOP" : "RIGHT")) a.anchor = Anchor::FarEdge;
                else if (base == (i ? "BOTTOM" : "LEFT")) a.anchor = Anchor::NearEdge;
                else if (base == (i ? "SOUTH" : "WEST")) a.tiles = 1;
                else if (base.endsWith('%')) a.percent = base.chopped(1).toDouble() / 100;
                else {
                    bool ok = false; a.tiles = base.toDouble(&ok);
                    if (!ok) return {};
                }
                // After the operator the number may carry a sign of its own, and that sign is the one that counts:
                // CENTER+-1 is one tile left, CENTER-+2 two right, CENTER--3 three left (stock 516.1689,
                // stock-parity-world screen-loc-signs; tgstation writes its hand slots as "CENTER+-1:16,SOUTH+0:5").
                auto offset = m.captured(2);
                if (offset.size() > 1 && (offset[1] == '+' || offset[1] == '-')) offset.remove(0, 1);
                if (offset.endsWith('%')) a.percent += offset.chopped(1).toDouble() / 100;
                else a.tiles += offset.toDouble();
                bool ok = true;
                if (!m.captured(3).isEmpty()) a.pixels = m.captured(3).toInt(&ok);
                if (!ok || !std::isfinite(a.tiles) || std::abs(a.tiles) > 16384 || std::abs(int64_t(a.pixels)) > 1048576) return {};
            }
            return p;
        };
        const auto first = point(ends[0]);
        if (!first) return {};
        result.first = result.last = *first;
        if (ends.size() == 2) {
            const auto last = point(ends[1]);
            if (!last) return {};
            result.last = *last; result.range = true;
            // The grid starts at the lower endpoint, on a half tile or a fraction too (CENTER on an even view, 2.5),
            // and covers the other one (stock 516.1689, stock-parity-world screen-loc-forms,
            // screen-loc-percent-ranges); positions() says how percents and pixel offsets move it.
            // Different offsets with an edge-aligned end (LEFT, TOP) were not tried and are not placed, except an
            // edge-aligned first end's own before a plain second end: they move the grid ("LEFT+50%:8,1 to 3,1" is 8
            // pixels right of "LEFT+50%,1 to 3,1", "RIGHT-50%:8,1 to 1,1" of "RIGHT-50%,1 to 1,1",
            // master-screen-loc-world).
            const auto untried = [](const Axis& a, const Axis& b) {
                const bool tried = a.anchor != Anchor::Tile && b.anchor == Anchor::Tile && !b.pixels;
                return a.pixels != b.pixels && (a.anchor != Anchor::Tile || b.anchor != Anchor::Tile) && !tried;
            };
            if (untried(result.first.x, result.last.x) || untried(result.first.y, result.last.y)) return {};
        }
        return result;
    }
    // How many whole tiles this placement reaches outside the view on each side. Stock makes the map that much
    // larger, black where there is no map, and centres the larger scene: 0,6 adds a column on the left, 0:16,6 too,
    // 18:8,6 (a pixel offset past the edge) none, and an icon of two tiles on the last column or the top row one more
    // (stock 516.1689, stock-parity-world screen-loc-off-view-*). icon: the root icon's size in whole tiles.
    // A percent enlarges nothing, like a pixel offset: "5+50%,1" (8 on a 7 wide view) is not drawn and the map keeps
    // its size, and so does "3+100%,1 to 1,1", whose copies reach 9 (master-screen-loc-world).
    QMargins outside(QSize view, QSize icon = {1, 1}) const {
        const auto at = [](const Axis& a, int dimension) {
            return a.tiles + a.relative * dimension + (a.anchor == Anchor::FarEdge ? dimension - 1 : 0);
        };
        QMargins m;
        for (const Point* p : {&first, &last}) {
            const double x = at(p->x, view.width()), y = at(p->y, view.height());
            // RIGHT and TOP put the icon's far edge on that tile, so a larger icon reaches back from it.
            const int w = icon.width() - 1, h = icon.height() - 1;
            const bool farX = p->x.anchor == Anchor::FarEdge, farY = p->y.anchor == Anchor::FarEdge;
            m = QMargins(std::max(m.left(), int(std::ceil(1 - x)) + (farX ? w : 0)),
                         std::max(m.top(), int(std::ceil(y - view.height())) + (farY ? 0 : h)),
                         std::max(m.right(), int(std::ceil(x - view.width())) + (farX ? 0 : w)),
                         std::max(m.bottom(), int(std::ceil(1 - y)) + (farY ? h : 0)));
        }
        return m;
    }
    // outside: the tiles around the view that the scene has room for (TurfMap::Frame's geometry.hud).
    std::vector<QPoint> positions(QSize view, int tile, QMargins outside = {}) const {
        if (view.isEmpty() || view.width() > 16384 || view.height() > 16384 || tile <= 0 || tile > 4096) return {};
        // The tile part of an axis: what places the scene, orders a range and counts its copies. A percent is added
        // afterwards, like a pixel offset (master-screen-loc-world: "2,1 to 50%,1" is two copies from 4,1).
        const auto local = [](const Axis& a, int dimension) { return a.tiles + a.relative * dimension; };
        const auto shift = [](const Axis& a, int dimension) { return a.anchor == Anchor::FarEdge ? dimension - 1 : 0; };
        // A percent that lands between pixels: from an edge it goes to the left and up (RIGHT-10% at 489.6 is 489,
        // BOTTOM+33% at 116.2 is 117, TOP-90% at 19.2 is 20); added to a tile, its own pixels are cut towards zero
        // (stock 516.1689, plane-relay-world locpd-mark: "1+10%,1+10%" 19 right and 19 up; locpe: "1-20%,1-20%" 38
        // left and 38 down). at: the tile part, with an edge's own shift.
        const auto pixel = [&](double at, double percent, int dimension, bool vertical, bool edge) {
            if (percent == 0) return int(std::lround((at-1)*tile));
            if (!edge) {
                const double share = percent*(dimension-1)*tile;
                return int(std::lround((at-1)*tile)) + int(std::trunc(share + (share < 0 ? -1e-6 : 1e-6)));
            }
            const double v = (at-1+percent*(dimension-1))*tile;
            return int(vertical ? std::ceil(v) : std::floor(v));
        };
        if (!range) {
            const double x = local(first.x,view.width()) + shift(first.x,view.width());
            const double y = local(first.y,view.height()) + shift(first.y,view.height());
            // Automatic logical-view expansion remains outside this subset. A percent past the edge is placed there
            // and not seen, as in stock.
            if (x < 1 - outside.left() || y < 1 - outside.bottom() || x > view.width() + outside.right() ||
                y > view.height() + outside.top()) return {};
            return {QPoint(pixel(x,first.x.percent,view.width(),false,first.x.anchor != Anchor::Tile)+first.x.pixels,
                           pixel(y,first.y.percent,view.height(),true,first.y.anchor != Anchor::Tile)+first.y.pixels)};
        }
        struct Run { int origin = 0, count = 0; };
        const auto run = [&](const Axis& a, const Axis& b, int dimension, int near, int far, bool vertical) -> Run {
            const double av = local(a,dimension), bv = local(b,dimension);
            // A range that runs down from a half tile or to a CENTER on one fills stock's whole view or more
            // (NORTH,EAST TO CENTER,CENTER; 12,8 to CENTER+1,9 on an 18 wide view): not drawn here. From a CENTER that
            // is a whole tile (an odd view) it runs down like any other ("CENTER,1 to 1,1", master-screen-loc-world).
            if (bv < av && ((a.relative == .5 && av != std::floor(av)) || b.relative == .5 || av - bv != std::floor(av - bv))) return {};
            // Written from its upper end down, a range of tile ends starts at the lower end and is moved by the
            // percents and pixel offsets of both ends; otherwise by the first end's alone (stock 516.1689,
            // master-screen-loc-world "3:4,1 to 1:8,1" 12 pixels right, "2+50%,1 to 1+25%,1" from 5.5, "1,1 to 3+50%,1"
            // from 1, "1,1 to 50%,1" from 1: two ends on the same tile keep the first). A LEFT end counts as tile 1
            // ("3,1 to LEFT+50%,1" from 4); pixel offsets with an edge-aligned end, or a percent with a RIGHT or TOP
            // end, were not tried.
            const bool reversed = bv < av && a.anchor != Anchor::FarEdge;
            if (reversed && (a.anchor != Anchor::Tile || b.anchor != Anchor::Tile) &&
                (a.pixels || b.pixels || (b.anchor == Anchor::FarEdge && (a.percent != 0 || b.percent != 0)))) return {};
            const double percent = reversed ? a.percent + b.percent : a.percent;
            const int pixels = reversed ? a.pixels + b.pixels : a.pixels;
            if (a.anchor == Anchor::FarEdge && bv < av && (a.pixels || b.pixels)) return {};
            double lo = av, hi = bv;
            const Axis* origin = &a;
            // Stock 516: edge alignment is NOT a tile endpoint. Far-edge -> tile
            // is directed; two aligned endpoints retain the first far-edge anchor
            // while sorting their local coordinates. Otherwise the lower local
            // endpoint owns the origin (ties retain the first). See stock corpus.
            if (a.anchor == Anchor::FarEdge) {
                if (b.anchor != Anchor::Tile) { lo = std::min(av,bv); hi = std::max(av,bv); }
            } else if (bv < av) {
                lo = bv; hi = av; origin = &b;
            }
            const int count = int(std::ceil(hi-lo))+1;
            if (count <= 0 || count > 256) return {};
            // Ordinary out-of-view tile grids can expand stock's view. Far-edge
            // aligned repetition instead clips to the existing viewport, verified
            // even when its range extends past the far edge. Do not expand here.
            if (origin->anchor != Anchor::FarEdge && (lo < 1 - near || hi > dimension + far)) return {};
            // Copies a percent carries past the scene are not seen and enlarge nothing ("3+100%,1 to 1,1" shows 7,1).
            return {pixel(lo+shift(*origin,dimension),percent,dimension,vertical,origin->anchor != Anchor::Tile)+pixels,count};
        };
        const auto x = run(first.x,last.x,view.width(),outside.left(),outside.right(),false);
        const auto y = run(first.y,last.y,view.height(),outside.bottom(),outside.top(),true);
        // Bound the WHOLE range before allocation, including clipped copies.
        if (!x.count || !y.count || x.count*y.count > 256) return {};
        std::vector<QPoint> out; out.reserve(x.count*y.count);
        for (int iy = 0; iy < y.count; ++iy) for (int ix = 0; ix < x.count; ++ix)
            out.emplace_back(x.origin + ix*tile, y.origin + iy*tile);
        return out;
    }
};
}

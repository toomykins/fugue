#include "shell/mapmenu.h"

#include <QPainter>
#include <QPixmap>
#include <algorithm>
#include <cmath>

#include "model/statpanel.h"
#include "model/viscontents.h"
#include "shell/compositionplan.h"

namespace fugue::shell {

std::vector<MapMenuEntry> mapMenuStack(const MapPointer& p, const model::World& w, uint64_t session) {
    std::vector<MapMenuEntry> out;
    for (const auto& rendered : mapPointerStack(p)) {
        if (!mapPointerCurrent(rendered, w, session)) continue;
        const auto& ref = rendered.click.over;
        const auto& loc = rendered.click.loc;
        const auto named = mapPointerTarget(rendered, w);
        const bool turf = named && named->kind == MapPointerTarget::Turf;
        // A turf is targeted at its own cell, which for a vis_contents member is not the cell it is drawn on.
        if (turf && (!named->appid || named->cell[2] != uint16_t(p.z))) continue;
        MapMenuEntry e;
        e.rendered = rendered;
        e.x = rendered.screen ? 0 : turf ? int(named->cell[0]) + 1 : int(loc.x) + 1;
        e.y = rendered.screen ? 0 : turf ? int(named->cell[1]) + 1 : int(loc.y) + 1;
        e.hit = model::targetFromHit(w, ref.kind, ref.ref, e.x, e.y, rendered.screen);
        if (named && named->appid) e.hit.appid = *named->appid;
        if (auto ap = w.appearances.find(e.hit.appid); ap != w.appearances.end())
            e.name = model::plainAtomName(ap->second.fields.name);
        // A nameless atom has no entry in stock's menu, whatever verbs it has (tests/data/menu-world empty).
        if (!e.name.empty()) {
            // A vis_contents member with no loc is on no cell: no range clause reaches it, though it is drawn on its
            // holder's (stock 516.1689, tests/data/menu-world holder: the member is offered poke, set src in world,
            // and not examine, obj in view(); vismenu-world ghost: anywhere and not near).
            auto offered = e.hit.target;
            if (rendered.visHit && named && named->kind == MapPointerTarget::Atom)
                if (auto atom = w.atoms.find(named->atom); atom != w.atoms.end() && !atom->second.hasPos)
                    offered.x = offered.y = 0;
            e.verbs = model::atomMenu(w, e.hit.appid, offered);
        }
        out.push_back(std::move(e));
    }
    return out;
}

namespace {
bool sameRef(const proto::enc::AtomRef& a, const proto::enc::AtomRef& b) {
    return a.kind == b.kind && a.ref == b.ref && a.x == b.x && a.y == b.y;
}
// The key a vis_contents edge names its holder by (entitymap.cpp): the atom's registry key (a HUD object's ref), or the
// turf's cell with VisContents::turfBit.
uint64_t holderKey(const MapPointer& e) {
    const auto& r = e.click.over;
    if (r.kind == proto::enc::RefTurf) return model::VisContents::turf(r.x, r.y, uint16_t(e.z));
    return r.kind == proto::enc::RefMob ? (r.ref | model::kRegistryA) : r.ref;
}
// Whether a drawn sprite is part of the entry's picture: the atom's own icon, overlays and underlays, a client image
// clicked as the atom, and the vis_contents members under it (stock 516.1689, tests/data/menu-world overlay, underlay,
// imaged, holder). For a member's own entry, its icon and what is under it.
bool partOf(const MapSprite& s, const MapPointer& e) {
    if (s.image.isNull() || s.footprint || s.maptext || s.kind == MapSprite::Master || s.kind == MapSprite::Blackness ||
        !s.renderSource.isEmpty())
        return false;
    if (e.visHit) {
        if (!s.visHit || s.visHit->path.size() < e.visHit->path.size()) return false;
        return std::equal(e.visHit->path.begin(), e.visHit->path.end(), s.visHit->path.begin());
    }
    if (s.visHit) return !s.visHit->path.empty() && s.visHit->path.front().holder == holderKey(e);
    return (s.kind == MapSprite::Screen) == e.screen && sameRef(mapSpriteRef(s), e.click.over);
}
// Where a part's icon is drawn before any matrix: a transformed part (its matrix turns and scales about the icon's
// centre) is put back at its own size about the same centre.
QRect natural(const MapSprite& s) {
    const QRect drawn = s.rect();
    if (!s.affine && (s.size.isEmpty() || s.size == s.image.size())) return drawn;
    QRect r(QPoint(), s.image.size());
    r.moveCenter(drawn.center());
    return r;
}
}  // namespace

QImage mapMenuFit(const QImage& image, int size) {
    if (image.isNull() || size <= 0) return {};
    const QImage src = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = src.width(), h = src.height();
    const double scale = double(size) / std::max(w, h);
    const int tw = std::max(1, int(std::lround(w * scale))), th = std::max(1, int(std::lround(h * scale)));
    const double fx = double(w) / tw, fy = double(h) / th;
    QImage out(size, size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    const int ox = (size - tw) / 2, oy = (size - th) / 2;
    for (int ty = 0; ty < th; ++ty) {
        const double y0 = ty * fy, y1 = (ty + 1) * fy;
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(oy + ty));
        for (int tx = 0; tx < tw; ++tx) {
            const double x0 = tx * fx, x1 = (tx + 1) * fx;
            double sum[4] = {0, 0, 0, 0}, area = 0;
            for (int sy = int(y0); sy < std::min(h, int(std::ceil(y1))); ++sy) {
                const double wy = std::min(y1, sy + 1.0) - std::max(y0, double(sy));
                const auto* line = reinterpret_cast<const QRgb*>(src.constScanLine(sy));
                for (int sx = int(x0); sx < std::min(w, int(std::ceil(x1))); ++sx) {
                    const double wgt = wy * (std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                    if (wgt <= 0) continue;
                    const QRgb p = line[sx];
                    sum[0] += wgt * qRed(p); sum[1] += wgt * qGreen(p); sum[2] += wgt * qBlue(p); sum[3] += wgt * qAlpha(p);
                    area += wgt;
                }
            }
            if (area <= 0) continue;
            auto c = [&](int i) { return std::clamp(int(std::lround(sum[i] / area)), 0, 255); };
            row[ox + tx] = qRgba(c(0), c(1), c(2), c(3));
        }
    }
    return out;
}

QImage mapMenuIcon(const MapPointer& entry, int size) {
    const MapSprites& sprites = entry.composition ? entry.composition->sprites : entry.renderedScene;
    std::vector<const MapSprite*> parts;
    QRect box;
    for (const auto& s : sprites) {
        if (!partOf(s, entry)) continue;
        parts.push_back(&s);
        box = box.united(natural(s));
    }
    if (parts.empty() || box.isEmpty() || box.width() > 4096 || box.height() > 4096) return {};
    QImage canvas(box.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        for (const auto* s : parts) painter.drawImage(natural(*s).topLeft() - box.topLeft(), s->image);
    }
    return mapMenuFit(canvas, size);
}

QIcon mapMenuEntryIcon(const QImage& icon, qreal devicePixelRatio) {
    if (icon.isNull()) return {};
    const qreal dpr = devicePixelRatio > 0 ? devicePixelRatio : 1;
    const int side = std::max({int(std::ceil(16 * dpr)), icon.width(), icon.height()});
    QImage canvas(side, side, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.drawImage((side - icon.width()) / 2, (side - icon.height()) / 2, icon);
    }
    QPixmap pixmap = QPixmap::fromImage(canvas);
    pixmap.setDevicePixelRatio(dpr);
    return QIcon(pixmap);
}

}  // namespace fugue::shell

#include "shell/turfmap.h"
#include "shell/sceneappearance.h"
#include "shell/clientoffset.h"
#include <QPainter>
#include <algorithm>
#include <set>

namespace fugue::shell {
QImage TurfMap::compose(const model::World& w, const Read& read, int64_t nowNs) {
    auto frame = prepare(w, read, nowNs);
    return frame.image.isNull() ? QImage() : frame.image.copy(frame.source);
}
TurfMap::Frame TurfMap::prepare(const model::World& w, const Read& read, int64_t nowNs, bool flatten, QMargins hud) {
    const int columns = w.bounds.shownW(), rows = w.bounds.shownH();
    const int tile = model::iconSizeOf(w);
    // covers carried glides (up to two tiles) plus movement headroom, and the tiles screen objects add around the view
    const MapTurn turn(flatten ? 1 : w.clientDir);
    const auto padded = [&] {
        return 4 + std::max({hud.left(), hud.top(), hud.right(), hud.bottom()}) +
               (turn.dir == 4 || turn.dir == 8 ? (std::abs(columns - rows) + 1) / 2 : 0);
    };
    if (hud.left() < 0 || hud.top() < 0 || hud.right() < 0 || hud.bottom() < 0 ||
        qint64(std::max(columns, rows) + 2 * padded()) * tile > 3840) hud = {};
    const int padding = padded();
    const int width = columns + 2 * padding, height = rows + 2 * padding;
    auto invalid = [&]() -> Frame { terrain_ = {}; region_ = {}; inputs_ = {}; return {}; };
    if (!w.bounds.have || !w.resync.haveOrigin || columns <= 0 || rows <= 0 || tile <= 0 ||
        qint64(width) * tile > 8192 || qint64(height) * tile > 8192 ||
        qint64(width) * height * tile * tile > 16 * 1024 * 1024) return invalid();
    const int x0 = w.haveEye ? w.eyeX - 1 - columns / 2 : w.resync.originX + (w.bounds.winW - columns) / 2;
    const int y0 = w.haveEye ? w.eyeY - 1 - rows / 2 : w.resync.originY + (w.bounds.winH - rows) / 2;
    int scrollX = 0, scrollY = 0;
    if (w.haveEye) model::eyeScrollOffset(w, nowNs, scrollX, scrollY);
    const auto offset = clientOffset(w, nowNs);
    if (!offset) return invalid(); // do not silently draw an unsupported camera chain as settled
    scrollX -= offset->x(); scrollY += offset->y();
    auto sourceRect = [&] {
        return turn.rect(QRect((x0 - region_.x()) * tile - scrollX,
                               (region_.y() + region_.height() - y0 - rows) * tile - scrollY,
                               columns * tile, rows * tile)) + hud * tile;
    };
    if (flattened_ != flatten) { terrain_ = {}; inputs_ = {}; flattened_ = flatten; }
    bool moved = terrain_.isNull() || tile_ != tile || z_ != w.viewZ || region_.size() != QSize(width, height) ||
                 !terrain_.rect().contains(sourceRect());
    if (moved) {
        region_ = QRect(x0 - padding + int(std::floor(-double(scrollX) / tile)),
                        y0 - padding + int(std::floor(double(scrollY) / tile)), width, height);
        tile_ = tile; z_ = w.viewZ;
    }
    // Unified scene presentation retains only the padded geometry and black backing.
    // Turf appearance parts now participate in the same ordered cache as movables.
    if (!flatten) {
        if (terrain_.size() != QSize(width * tile, height * tile)) {
            terrain_ = QImage(width * tile, height * tile, QImage::Format_RGBA8888);
            terrain_.fill(Qt::black);
        }
        return {terrain_, sourceRect(), {region_, QSize(w.bounds.maxX, w.bounds.maxY), tile, int(w.viewZ), 0, hud, turn.dir}};
    }
    Inputs next;
    std::vector<uint32_t> pending;
    // Track only this cache's turfs and their appearance dependencies, never entities.
    for (int y = region_.y(); y < region_.y() + height; ++y) for (int x = region_.x(); x < region_.x() + width; ++x) {
        auto it = w.tiles.end();
        if (x >= 0 && y >= 0 && x < w.bounds.maxX && y < w.bounds.maxY)
            it = w.tiles.find({uint16_t(x), uint16_t(y), w.viewZ});
        next.tiles.push_back(it == w.tiles.end() ? UINT64_MAX : it->second);
        if (it != w.tiles.end()) pending.push_back(it->second);
    }
    icons_.begin(w.resources, read);
    std::set<uint32_t> seen;
    while (!pending.empty()) {
        uint32_t id = pending.back(); pending.pop_back();
        if (!seen.insert(id).second) continue;
        if (seen.size() > 65536) return invalid(); // bounded work for pathological overlay graphs
        auto it = w.appearances.find(id);
        if (it == w.appearances.end()) continue; // a later appearance arrival changes the dependency set
        const auto& ap = it->second;
        AppearanceSource src{ap.fields};
        if (ap.fields.iconResID < w.resources.size()) {
            const auto resource = icons_.lookup(ap.fields.iconResID);
            src.haveResource = resource.ready;
            src.resource = resource.id;
            src.resourceRevision = resource.revision;
        }
        src.children = ap.fields.underlays;
        src.children.insert(src.children.end(), ap.fields.overlays.begin(), ap.fields.overlays.end());
        pending.insert(pending.end(), src.children.begin(), src.children.end());
        next.appearances.emplace(id, std::move(src));
    }
    if (moved || next != inputs_) {
        QImage image = paintRegion(w, tile);
        if (image.isNull()) return invalid();
        // Metadata may change without changing any pixels. Preserve the image identity then,
        // so neither the worker nor the GPU performs an unnecessary terrain upload.
        if (image != terrain_) terrain_ = std::move(image);
        inputs_ = std::move(next);
    }
    return {terrain_, sourceRect(), {region_, QSize(w.bounds.maxX, w.bounds.maxY), tile, int(w.viewZ), 0, hud}};
}
QImage TurfMap::paintRegion(const model::World& w, int tile) {
    QImage image(region_.width() * tile, region_.height() * tile, QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    image.fill(Qt::black);
    QPainter painter(&image);
    std::set<uint32_t> path;
    int drawBudget = 0;
    std::function<void(uint32_t, QPoint, uint32_t, int)> draw;
    draw = [&](uint32_t id, QPoint origin, uint32_t inheritedIcon, int inheritedDir) {
        if (--drawBudget < 0 || path.size() >= 32 || !path.insert(id).second) return;
        auto found = w.appearances.find(id);
        if (found == w.appearances.end()) { path.erase(id); return; }
        const proto::Appearance& fields = found->second.fields;
        uint32_t ordinal = fields.iconResID != 0xffffffffu && fields.iconResID != 0xffffu ? fields.iconResID :
                           fields.iconState.empty() ? 0xffffffffu : inheritedIcon; // as for objects (entitymap.cpp)
        int dir = int(fields.dir);
        if (path.size() == 1) { if (!dir) dir = 2; }
        else if (!SceneAppearance(fields).fixedDir) dir = inheritedDir;
        origin += QPoint(fields.pixelX + fields.pixelW, -(fields.pixelY + fields.pixelZ));
        for (uint32_t child : fields.underlays) draw(child, origin, ordinal, dir);
        if (ordinal < w.resources.size()) {
            const auto resource = icons_.lookup(ordinal);
            if (resource.ready) {
                QImage frame = icons_.icon(resource.id).frame(QString::fromStdString(fields.iconState), dir);
                if (fields.hasColor && !frame.isNull()) {
                    frame = frame.convertToFormat(QImage::Format_ARGB32);
                    const int r = fields.color[0], g = fields.color[1], b = fields.color[2], a = fields.color[3];
                    for (int y = 0; y < frame.height(); ++y) {
                        auto* pixels = reinterpret_cast<QRgb*>(frame.scanLine(y));
                        for (int x = 0; x < frame.width(); ++x) {
                            QRgb p = pixels[x];
                            pixels[x] = qRgba(qRed(p)*r/255, qGreen(p)*g/255, qBlue(p)*b/255, qAlpha(p)*a/255);
                        }
                    }
                }
                painter.drawImage(origin + QPoint(0, tile - frame.height()), frame);
            }
        }
        for (uint32_t child : fields.overlays) draw(child, origin, ordinal, dir);
        path.erase(id);
    };
    for (int row = 0; row < region_.height(); ++row) for (int x = 0; x < region_.width(); ++x) {
        int wx = region_.x() + x, wy = region_.y() + region_.height() - 1 - row;
        if (wx < 0 || wy < 0 || wx >= w.bounds.maxX || wy >= w.bounds.maxY) continue;
        auto turf = w.tiles.find({uint16_t(wx), uint16_t(wy), w.viewZ});
        drawBudget = 4096;
        if (turf != w.tiles.end()) draw(turf->second, QPoint(x * tile, row * tile), 0xffffffffu, 2);
    }
    return image;
}
}

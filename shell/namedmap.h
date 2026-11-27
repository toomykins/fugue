// Worker-only, independent screen scene. Named MAPs start with a one-icon view,
// not the default eye/terrain. No model references survive prepare().
#pragma once
#include "shell/entitymap.h"
#include "model/json.h"

namespace fugue::shell {
class NamedMap {
public:
    explicit NamedMap(QString name) : name_(std::move(name)), entities_(name_) {}
    struct Frame { TurfMap::Frame terrain; MapSprites sprites; };
    Frame prepare(const model::World& world, const TurfMap::Read& read, int64_t now,
                  uint64_t session, EntityMap::Stats* stats = nullptr) {
        const int tile = model::iconSizeOf(world);
        if (tile <= 0 || tile > 8192 || int64_t(tile) * tile > 16 * 1024 * 1024) return {};
        const QSize view = screenView(world);
        const QSize pixels = view * tile;
        if (pixels.width() > 8192 || pixels.height() > 8192 ||
            int64_t(pixels.width()) * pixels.height() > 16 * 1024 * 1024) return {};
        if (terrain_.image.size() != pixels) {
            terrain_.image = QImage(pixels, QImage::Format_RGBA8888);
            terrain_.image.fill(Qt::black);
        }
        terrain_.source = QRect(QPoint(), pixels);
        terrain_.geometry = {QRect(QPoint(), view), {}, tile, int(world.viewZ), session};
        return {terrain_, entities_.prepare(world, terrain_, read, now, stats, true)};
    }
private:
    // Stock's explicit positive tile anchors expand a named viewport from 1x1.
    // Pixel offsets do not expand it. Relative/edge anchors resolve AFTER sizing.
    QSize screenView(const model::World& world) {
        QSize view(1, 1);
        std::set<uint32_t> used;
        size_t work = 65536;
        for (uint32_t key : world.hudOrder) {
            if (!work--) break;
            const auto hud = world.hudAppid.find(key);
            if (hud == world.hudAppid.end()) continue;
            const auto ap = world.appearances.find(hud->second);
            if (ap == world.appearances.end()) continue;
            auto& cached = locations_[hud->second]; used.insert(hud->second);
            if (cached.screenLoc != ap->second.fields.screenLoc) {
                cached.screenLoc = ap->second.fields.screenLoc; cached.location.reset();
                const auto target = ScreenLoc::target(QString::fromStdString(*cached.screenLoc));
                if (target && target->map == name_) cached.location = ScreenLoc::parse(target->location);
            }
            if (!cached.location) continue;
            const auto& loc = *cached.location;
            auto extent = [](const ScreenLoc::Axis& axis) {
                return axis.anchor == ScreenLoc::Anchor::Tile && axis.relative == 0 &&
                    axis.tiles >= 1 ? int(std::ceil(axis.tiles)) : 1;
            };
            view = view.expandedTo({extent(loc.first.x), extent(loc.first.y)});
            if (loc.range) view = view.expandedTo({extent(loc.last.x), extent(loc.last.y)});
        }
        std::erase_if(locations_, [&](const auto& entry) { return !used.count(entry.first); });
        return view;
    }
    struct Location { std::optional<std::string> screenLoc; std::optional<ScreenLoc> location; };
    std::map<uint32_t, Location> locations_;
    const QString name_;
    EntityMap entities_;
    TurfMap::Frame terrain_;
};
}

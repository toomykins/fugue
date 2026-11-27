// One latest terrain/camera frame, not a queued stream of QImages. The producer
// requests at most one outstanding GUI notification; renderers pull again at draw time.
#pragma once
#include <QImage>
#include <QRect>
#include <memory>
#include <mutex>
#include <optional>
#include <map>
#include <vector>
#include "shell/mapmetrics.h"
#include "shell/mapinput.h"

namespace fugue::shell {
class MapMailbox {
public:
    struct Frame {
        QImage terrain;
        QRect source;
        uint64_t sequence = 0;
        MapMetrics::FrameTiming timing;
        MapGeometry geometry;
        MapSprites sprites;
    };
    explicit MapMailbox(std::shared_ptr<MapMetrics> metrics) : metrics_(std::move(metrics)) {}
    std::shared_ptr<MapMetrics> metrics() const { return metrics_; }
    // true means the producer must emit a notification (no image payload).
    bool publish(const QImage& terrain, const QRect& source, int64_t started, int64_t prepared,
                 MapGeometry geometry = {}, const MapSprites& sprites = {}) {
        std::lock_guard lock(mu_);
        metrics_->published(latest_.sequence != 0 && !rendered_);
        latest_ = {terrain, source, latest_.sequence + 1, {started, prepared, 0, 0}, geometry, sprites};
        rendered_ = false;
        if (notified_) return false;
        notified_ = true;
        return true;
    }
    // GUI callback acknowledges its notification and timestamps the latest available frame.
    void acknowledge(int64_t now = 0) {
        std::lock_guard lock(mu_);
        if (!now) now = MapMetrics::nowNs();
        notified_ = false;
        if (!latest_.timing.received) latest_.timing.received = now;
    }
    bool newerThan(uint64_t sequence) const {
        std::lock_guard lock(mu_);
        return latest_.sequence > sequence;
    }
    // Each MAP has its own sequence. A newly rebuilt MAP can fetch the retained latest
    // frame even if another MAP already rendered it. Copying the QImage shares its pixels.
    std::optional<Frame> forRender(uint64_t sequence, int64_t now = 0) {
        std::lock_guard lock(mu_);
        if (latest_.sequence <= sequence) return {};
        if (!now) now = MapMetrics::nowNs();
        if (!latest_.timing.received) latest_.timing.received = now;
        Frame frame = latest_;
        if (rendered_) frame.timing = {}; // a skin rebuild/second MAP replays pixels, not a fresh latency sample
        rendered_ = true;
        metrics_->consumed();
        return frame;
    }
private:
    std::shared_ptr<MapMetrics> metrics_;
    mutable std::mutex mu_;
    Frame latest_;
    bool notified_ = false, rendered_ = true;
};

// GUI-owned subscriptions; the worker takes bounded snapshots, never widgets.
// A destroyed/recreated control gets a new route even if its name is reused.
class MapRoutes {
public:
    struct Route {
        uint64_t id;
        QString name;
        std::shared_ptr<MapMailbox> mailbox;
    };
    std::shared_ptr<MapMailbox> subscribe(const QString& name) {
        std::lock_guard lock(mu_);
        prune();
        if (routes_.size() >= 16 || name.isEmpty() || name.size() > 256) return {};
        auto mailbox = std::make_shared<MapMailbox>(std::make_shared<MapMetrics>());
        routes_.emplace(++next_, Entry{name, mailbox});
        return mailbox;
    }
    std::vector<Route> snapshot() {
        std::lock_guard lock(mu_);
        prune();
        std::vector<Route> out;
        for (const auto& [id, entry] : routes_)
            if (auto mailbox = entry.mailbox.lock()) out.push_back({id, entry.name, std::move(mailbox)});
        return out;
    }
private:
    struct Entry { QString name; std::weak_ptr<MapMailbox> mailbox; };
    void prune() {
        std::erase_if(routes_, [](const auto& entry) { return entry.second.mailbox.expired(); });
    }
    std::mutex mu_;
    uint64_t next_ = 0;
    std::map<uint64_t, Entry> routes_;
};
}

// Session-thread camera region and optional turf-only CPU reference cache.
#pragma once
#include <QImage>
#include <QRect>
#include <QString>
#include <functional>
#include <map>
#include "model/world.h"
#include "shell/mapinput.h"
#include "shell/iconcache.h"

namespace fugue::shell {
class TurfMap {
public:
    using Read = std::function<bool(uint32_t, QByteArray&)>;
    struct Frame {
        QImage image; // padded terrain, implicitly shared until pixels change
        QRect source; // visible pixel rectangle within image (top-left image coordinates)
        MapGeometry geometry;
    };
    // Live scenes use flatten=false: geometry and retained black backing only.
    // The default turf-only reference retries missing resources. nowNs uses the ingest clock.
    // hud: whole tiles to add around the view for screen objects outside it (EntityMap::hudMargins of the frame
    // before); dropped when the scene would be too large.
    Frame prepare(const model::World& world, const Read& read, int64_t nowNs = 0, bool flatten = true, QMargins hud = {});
    // CPU reference/capture of the visible map; presentation uses prepare() without this copy.
    QImage compose(const model::World& world, const Read& read, int64_t nowNs = 0);
private:
    struct AppearanceSource {
        proto::Appearance fields;
        uint32_t resource = 0;
        bool haveResource = false;
        uint64_t resourceRevision = 0;
        std::vector<uint32_t> children;
        bool operator==(const AppearanceSource&) const = default;
    };
    struct Inputs {
        std::vector<uint64_t> tiles;
        std::map<uint32_t, AppearanceSource> appearances;
        bool operator==(const Inputs&) const = default;
    };
    QImage paintRegion(const model::World& world, int tile);
    IconCache icons_;
    QRect region_; // zero-based world tiles, y increases north
    int tile_ = 0;
    uint16_t z_ = 0;
    bool flattened_ = true;
    Inputs inputs_;
    QImage terrain_;
};
}

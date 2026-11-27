// Worker-only INFO row appearance resolution. No MAP atom lookup or GUI pixmaps.
#pragma once
#include "shell/atomdiagnostic.h"
#include "model/world.h"

namespace fugue::shell {
class StatThumbnails {
public:
    void begin(const model::World& world, const IconCache::Read& read, int64_t nowNs);
    QImage resolve(uint32_t appid, int64_t startNs, AtomTrace::Info* diagnostic = nullptr);
    size_t cached() const { return entries_.size(); }
private:
    struct Entry { QImage image; uint64_t used = 0; };
    // Visual keys also reuse thumbnails when the server assigns a new appearance
    // ID for an ignored transform. Ordered lookup keeps large lists bounded.
    std::map<QByteArray, Entry> entries_;
    IconCache icons_;
    const model::World* world_ = nullptr;
    int64_t nowNs_ = 0;
    uint64_t epoch_ = 0;
    unsigned work_ = 0, reads_ = 0;
    uint64_t pixels_ = 0, resourcePixels_ = 0;
};
}

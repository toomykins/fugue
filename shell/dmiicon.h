#pragma once
#include <QImage>
#include "render/dmi.h"
#include <QString>
#include <map>
#include <memory>
#include <vector>

namespace fugue::shell {
struct DmiIcon {
    struct State {
        int first = 0, dirs = 1, frames = 1;
        int loop = 0;
        bool rewind = false;
        // End times for forward frames, then interior reverse frames. Shared by templates.
        std::shared_ptr<const std::vector<int64_t>> ends;
        int frameAt(int64_t nowNs, int64_t startNs, int64_t originNs = 0) const;
        // One forward pass, ignoring loop/rewind. -1 means completed.
        int flickFrameAt(int64_t nowNs, int64_t startNs) const;
    };
    QImage sheet;
    bool generated = false; // DDMI materialization is not a verified flick asset
    QSize cell{32, 32};
    std::map<QString, State> states, moving; // moving: the "movement = 1" states, drawn while an atom glides
    // Original ordered metadata includes movement variants. No instance clocks live here.
    std::shared_ptr<const std::vector<assets::IconState>> metadata;
    static DmiIcon decode(const QByteArray& bytes, uint64_t maxPixels = 64 * 1024 * 1024, bool* limited = nullptr);
    static DmiIcon fromAsset(const assets::Icon& asset);
    assets::Icon toAsset() const;
    uint64_t storageBytes() const; // conservative shared-buffer accounting for cache retention
    const State* state(const QString& name) const;
    int index(const State& state, int dir, int frame = 0) const;
    QImage frame(const QString& state, int dir, int frame = 0) const;
    QImage frame(int index) const;
};
}

// GUI-side transient surface lifetimes, independent of QRhi/model state.
#pragma once
#include <QSize>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace fugue::shell {
struct SurfaceLifetime {
    QSize size;
    // Execution-order index of the last reader. size() denotes final MAP use;
    // a writer cannot share storage with any input of its own pass.
    int lastRead = 0;
    // Incompatible render-target formats cannot share a live allocation.
    int storageClass = 0;
};
// First failed admission check. Counts are the attempted value, not GPU allocations.
struct PlanRejection {
    enum Reason { None, EmptySource, InvalidPixelWeight, InvalidPass, IndexRange, ArithmeticOverflow };
    Reason reason = None;
    uint64_t observed = 0, limit = 0;
    int pass = -1; // graph index for composition; schedule index for storage admission
    const char* name() const {
        switch (reason) {
        case None: return "none";
        case EmptySource: return "empty-source";
        case InvalidPixelWeight: return "invalid-pixel-weight";
        case InvalidPass: return "invalid-pass";
        case IndexRange: return "index-range";
        case ArithmeticOverflow: return "arithmetic-overflow";
        }
        return "unknown";
    }
};
struct SurfacePlan {
    bool supported = false;
    PlanRejection rejection;
    std::vector<int> slotForPass;
    std::vector<QSize> sizes;
    std::vector<int> storageClasses;
    uint64_t storagePixels = 0, workPixels = 0; // RGBA8 equivalents, including MAP
};
inline SurfacePlan planSurfaces(QSize scene, std::span<const SurfaceLifetime> passes, int pixelWeight) {
    SurfacePlan out;
    auto area = [](QSize size) { return uint64_t(size.width())*uint64_t(size.height()); };
    auto reject = [&](PlanRejection::Reason reason, uint64_t observed = 0, uint64_t limit = 0, int pass = -1) {
        out.rejection = {reason,observed,limit,pass}; return out;
    };
    if (scene.isEmpty()) return reject(PlanRejection::EmptySource);
    if (pixelWeight != 1 && pixelWeight != 2 && pixelWeight != 4) return reject(PlanRejection::InvalidPixelWeight);
    if (passes.size() > size_t(std::numeric_limits<int>::max()))
        return reject(PlanRejection::IndexRange,passes.size(),std::numeric_limits<int>::max());
    out.storagePixels = out.workPixels = area(scene);
    const int count = int(passes.size());
    out.slotForPass.resize(count,-1);
    std::vector<int> lastRead;
    for (int i = 0; i < count; ++i) {
        const auto& pass = passes[i];
        if (pass.size.isEmpty() || pass.lastRead < i || pass.lastRead > count)
            return reject(PlanRejection::InvalidPass,0,0,i);
        const uint64_t pixels = area(pass.size)*pixelWeight;
        if (pixels > std::numeric_limits<uint64_t>::max()-out.workPixels)
            return reject(PlanRejection::ArithmeticOverflow,0,0,i);
        out.workPixels += pixels;
        // Pin final outputs in exact-size slots across MAP composition/replay.
        // Do not strand a large temporary allocation under a small final image.
        if (pass.lastRead == count) {
            if (pixels > std::numeric_limits<uint64_t>::max()-out.storagePixels)
                return reject(PlanRejection::ArithmeticOverflow,0,0,i);
            out.slotForPass[i] = int(out.sizes.size());
            out.sizes.push_back(pass.size);lastRead.push_back(count);
            out.storageClasses.push_back(pass.storageClass);
            out.storagePixels += pixels;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (out.slotForPass[i] >= 0) continue;
        const auto& pass = passes[i];
        int slot = -1;
        uint64_t growth = area(pass.size)*pixelWeight;
        // Prefer the least added storage. An unused slot may grow once during
        // planning, never while recording GPU commands. Crossed wide/tall sizes
        // can be cheaper in separate slots than one huge bounding rectangle.
        for (int j = 0; j < int(out.sizes.size()); ++j) if (lastRead[j] < i && out.storageClasses[j] == pass.storageClass) {
            const uint64_t added = (area(out.sizes[j].expandedTo(pass.size))-area(out.sizes[j]))*pixelWeight;
            if (added <= growth) { slot = j;growth = added; }
        }
        if (growth > std::numeric_limits<uint64_t>::max()-out.storagePixels)
            return reject(PlanRejection::ArithmeticOverflow,0,0,i);
        if (slot < 0) {
            slot = int(out.sizes.size());out.sizes.push_back(pass.size);lastRead.push_back(pass.lastRead);
            out.storageClasses.push_back(pass.storageClass);
        } else {
            out.sizes[slot] = out.sizes[slot].expandedTo(pass.size);lastRead[slot] = pass.lastRead;
        }
        out.slotForPass[i] = slot;out.storagePixels += growth;
    }
    out.supported = true;
    return out;
}
}

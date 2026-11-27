// Worker-only MAP visibility and bounded built-in illumination. No model pointers in a frame.
#pragma once
#include "model/world.h"
#include <QRect>
#include <memory>

namespace fugue::shell {
enum class VisibilityOutcome { Visible, Sight, Hidden, Unknown, Unsupported, Budget, OffView };
const char* visibilityName(VisibilityOutcome);
struct VisibilityFrame {
    enum Cell : uint8_t { Clear, Opaque, Missing, Deferred };
    enum Category : uint32_t { Mob = 4, Object = 8, Turf = 16 };
    static constexpr int maxSide = 129, maxCells = maxSide * maxSide;
    QRect cells; // zero-based world tiles, north-positive
    QPoint eye; // zero-based published viewport eye, not necessarily client.mob
    int z = 0, tile = 32;
    uint32_t sight = 0;
    uint8_t seeInDark = 0;
    VisibilityOutcome status = VisibilityOutcome::Unknown;
    std::vector<uint8_t> opacity, outcomes, lighting;
    QRect lightCells; // includes the bounded live-window emitter/anchor margin
    // Geometric visible / unknown / deferred summed areas. Illumination uses the
    // authoritative tile anchor, not pixel-offset/whole-appearance bounds.
    std::array<std::vector<uint32_t>,3> sums;
    // Plane-0 world/screen boundary coverage, in north-positive world tile runs.
    // No atom identity or texture; computed once with the immutable field.
    std::vector<QRect> blackness;
    bool blacknessReady = false;
    bool hasBlackness() const { return (sight & 0x400u) && (sight & 0x1cu) != 0x1cu; }
    size_t unknownCells = 0, deferredCells = 0, visits = 0;
    // Appearance bounds in EntityMap's world-pixel coordinates (south-positive Y).
    // Stock admits the whole appearance when its transformed bounds overlap a visible cell.
    VisibilityOutcome test(QRect bounds, Category category, QPoint anchor) const;
    VisibilityOutcome cell(int x, int y) const;
    VisibilityOutcome illumination(QPoint anchor) const;
};
class MapVisibility {
public:
    static constexpr size_t maxVisits = 65536;
    // World tiles (north-positive) whose pixels the presented terrain source rectangle shows.
    // This is the actual viewport, not the eye-centred nominal field.
    static QRect displayedCells(QRect region, int tile, QRect source);
    // displayed: optionally the presented source rectangle's tiles. The field domain is
    // the eye-centred nominal view united with it, still bounded by maxSide.
    std::shared_ptr<const VisibilityFrame> prepare(const model::World&, QRect displayed = {});
    bool reused() const { return reused_; }
private:
    struct Inputs {
        QRect cells, window;
        std::vector<uint8_t> opacity, ambient, emission, uncertain;
        bool operator==(const Inputs&) const = default;
    } inputs_;
    std::shared_ptr<const VisibilityFrame> frame_;
    bool reused_ = false;
};
}

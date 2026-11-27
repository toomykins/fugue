// Worker-owned bounded evidence. Values only; no model or resource loading in output.
#pragma once
#include "shell/iconcache.h"
#include "shell/mapsprite.h"
#include <QRect>
#include <optional>

namespace fugue::shell {
enum class TraceReason {
    Unobserved, AbsentModel, Freed, NoTerrain, NotVisited, Placement, SceneWork,
    MissingAppearance, InvalidAppearance, TraversalLimit, InvalidTransform, TransformTree,
    EffectCombination, AnimationUnavailable, MaptextUnsupported, MaptextWork, Invisible, ZeroAlpha, AssetUnavailable, EmptyAsset, MissingState,
    FrameWork, EmptyFrame, EmptyBounds, OffView, Prepared, TemplateComplete, TemplateRefused, TemplateDeferred, NoMatchingRow, NotAtomRow,
    InfoUnsupported, InfoTransformTree, InfoWork, Composed, Occluded, VisibilityUnknown, VisibilityUnsupported, VisibilityBudget
};
const char* traceReasonName(TraceReason reason);
struct TraceFailure {
    TraceReason reason = TraceReason::Unobserved;
    uint32_t appearance = 0, parent = 0;
};
struct TraceNode {
    std::vector<uint32_t> path; // <=32 IDs, root to this appearance
    uint32_t ordinal = UINT32_MAX;
    std::vector<IconCache::Observation> assets; // <=16, cached evidence only
    QString requested, selected; // <=64 characters each
    int direction = 0, directionSlot = -1, cell = -1, frame = 0;
    int invisibility = 0;
    float alpha = 1;
    bool transformed = false, transformValid = true, transformIgnored = false;
    bool fallback = false;
};
struct TraceTree {
    std::vector<TraceNode> nodes; // <=16
    std::vector<uint32_t> firstPath; // <=32, retained even when the node list truncates
    bool truncated = false;
};
struct AtomTrace {
    uint32_t key = 0;
    bool inspectPixels = false;
    std::optional<unsigned> infoRow;
    struct Map {
        bool present = false, freed = false, positioned = false, haveWireKey = false;
        uint32_t appearance = 0;
        uint16_t wireKey = 0;
        uint64_t lifetime = 0, revision = 0;
        int x = 0, y = 0, z = 0, viewZ = 0, seeInvisible = 0;
        TraceReason placement = TraceReason::Unobserved, templateStatus = TraceReason::Unobserved;
        TraceFailure first;
        bool complete = false, affine = false, detailsObserved = false;
        size_t parts = 0;
        int prepared = 0;
        QRect bounds, placedBounds, view;
        std::optional<QTransform> validatedTransform;
        TraceTree tree;
        QString visibility = "Unobserved", fieldStatus = "Unobserved", anchorIllumination = "Unobserved";
        bool haveSeeInDark = false;
        uint8_t seeInDark = 0;
        bool haveEye = false, haveSight = false, haveNarrowSight = false, haveWideSight = false, haveSelf = false;
        uint32_t narrowSight = 0, wideSight = 0, selfRef = 0;
        QRect source, terrainRegion;
        QPoint scroll;
        QPoint visibilityEye;
        QRect visibilityCells, visibilityBounds;
        uint32_t sight = 0;
        size_t unknownCells = 0, deferredCells = 0;
        int anchorOpacity = -1;
        bool visibilityReused = false;
    } map;
    struct Info {
        uint64_t lifetime = 0;
        int tab = 0;
        uint32_t appearance = 0, atomKey = 0;
        bool hasAtomKey = false;
        TraceFailure first;
        TraceReason outcome = TraceReason::NoMatchingRow;
        TraceTree tree;
    } info;
    // Strict decimal IDs, no bare IDs or registry-bit guessing. Row is zero-based.
    static std::optional<AtomTrace> parse(const QByteArray& selector, const QByteArray& row = {});
};
// Per-session bounded transition sampler. Caller provides monotonic milliseconds.
// No strings/images/model reads when disabled (do not construct/call this object).
class AtomTraceOutput {
public:
    QByteArray sample(const AtomTrace& trace, uint64_t session, uint64_t mapPublication,
                      int publishedSprites, uint64_t infoPublication, int64_t nowMs,
                      const MapSprites* published = nullptr, const QImage* thumbnail = nullptr);
    unsigned emitted() const { return emitted_; }
private:
    QByteArray previous_;
    int64_t nextMs_ = 0;
    unsigned emitted_ = 0;
};
}

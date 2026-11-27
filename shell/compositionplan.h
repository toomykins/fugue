// Immutable, model-free composition graph. No QRhi resources or model references.
#pragma once
#include "shell/localtargets.h"
#include "shell/planesources.h"
#include "shell/surfaceplan.h"
#include "shell/compositionmetrics.h"
#include <deque>
#include <memory>

namespace fugue::shell {
struct CompositionBackend {
    bool floatSurfaces = false; // renderable float surfaces; filters require them
    bool boundedMaterials = true; // false retains the full-plane differential-test reference
    bool wideFloatSurfaces = false; // the float surfaces are RGBA32F (four RGBA8 pixels each), not RGBA16F (two)
};
class CompositionPlan {
public:
    struct Item {
        const MapSprite* sprite;
        int input = -1; // raster (-1), refused (-2), or operation index
        std::array<float,12> effect{};
        bool straightInput = false;
        qsizetype original = -1;
        int cyclicInput = -1; // removed back-edge; never reuse a previous texture
        bool alphaMask = false; // internal destination-in, not a wire blend mode
        // An alpha filter's mask: the named plane's surface, drawn at its own place moved by maskOffset.
        bool maskSource = false;
        QPoint maskOffset;
        QRect maskCentre; // an atom's render target is centred here
        bool stockSubtract = false; // internal: RGB reverse-subtract, destination alpha kept (stock bloom)
        bool hardEdge = false; // a turned surface that ends at its own box: the edge texel repeats, nothing beyond the quad
        std::shared_ptr<const std::array<float,64>> params; // wave/ripple uniforms; null for every other operation
    };
    struct Pass {
        int surface;
        std::vector<Item> items;
        int state = 0;
        QRect bounds;
        bool byteCapture = false, straightOutput = false;
    };
    struct Binding {
        int localSource = -1, planeSource = -1, planeDependency = -1;
        bool localName = false, localDependency = false;
        bool refused = false, localVisible = true;
        int sourceOperation = -1, dependencyOperation = -1;
        // Viewport-inverse mouse basis, intentionally not the visual relay basis.
        std::optional<StaticAffine> mouseAffine;
        // A part of a turned group (KEEP_TOGETHER or filters): scene pixels map back to the part's untransformed
        // position as origin + inverse(pixel - origin), the inverse of the group quads' matrices.
        std::optional<QTransform> groupInverse;
        QPoint groupOrigin;
    };
    static std::shared_ptr<const CompositionPlan> build(const MapSprites&, QRect, CompositionBackend);
    // How far a relay of a plane's surface moves the plane's picture: as far as the relay stands from 1,1.
    static QPoint relayOffset(const MapSprite& relay, QRect source) {
        return relay.iconBottom - QPoint(source.left(), source.bottom() + 1);
    }
    CompositionPlan(const CompositionPlan&) = delete;
    CompositionPlan& operator=(const CompositionPlan&) = delete;

    const MapSprites sprites; // keeps original items/rasters alive; no caller storage references
    const QRect source;
    const CompositionBackend backend;
    const LocalTargets local;
    const std::map<int,qsizetype> masters; // stable last master, including refusal markers
    const PlaneSources planeSources; // original-ink proof, NOT composited alpha
    enum class MasterDisposition { Forward, Skipped, Refused, Captured };
    std::map<int,MasterDisposition> masterDisposition;
    std::map<QString,int> targets; // capture lookup; per-item bindings retain ambiguity/refusal decisions
    std::map<QString,int> planeTargetCounts; // only selected masters; duplicates cannot authorize relays
    std::vector<Binding> bindings; // indexed by original sprite, shared with picking
    std::vector<Pass> passes;
    std::vector<Item> final;
    std::vector<int> schedule;
    SurfacePlan storage;
    PlanRejection rejection;
    CompositionMetrics metrics;
    bool supported = false;
    uint64_t ignoredFilters = 0, requestedSurfaces = 0, livePasses = 0;
    // Input policy is a separate projection of the same selected masters.
    std::set<int> hidden, hiddenTree, outlines;
    // Planes hidden only because a relay draws their '*' render target: their atoms take no clicks where they stand,
    // but the right-click menu still lists them there (stock 516.1689, plane-relay-world loc23, nopass-loc23).
    std::set<int> captured;
    std::map<int,StaticAffine> transforms;
    qsizetype fallback = -1;
    // Icon alpha masks hide their masked-out pixels from input too (stock 516.1689, tests/data/click-mask-world): a
    // plane master's by plane, an atom's by group occurrence. A plane master's mask by render source is tested at the
    // pointer instead (mapinput.cpp); an atom's is not applied.
    struct InputMask { QImage image; QPoint topLeft; bool inverse = false; };
    std::map<int,std::vector<InputMask>> planeMasks;
    std::map<std::pair<uint64_t,const AppearanceGroup*>,std::vector<InputMask>> groupMasks;
    // groupPixel: after the sprite's group inverse; planePixel: after its plane's.
    bool maskedOut(const MapSprite& sprite, QPoint groupPixel, QPoint planePixel) const;
private:
    CompositionPlan(const MapSprites&, QRect, CompositionBackend);
    void plan();
    void planInput();
    std::deque<MapSprite> generated_; // stable immutable operation quads
};
}

// Session-thread cached scene extraction. No model pointers cross to presentation.
#pragma once
#include "shell/turfmap.h"
#include "shell/mapvisibility.h"
#include "shell/mapsprite.h"
#include "shell/screenloc.h"
#include "shell/atomdiagnostic.h"
#include "shell/maptext.h"
#include "shell/entityappearance.h"
#include <variant>
#include <memory>
#include <tuple>
#include <unordered_map>

namespace fugue::shell {
class EntityMap {
public:
    explicit EntityMap(QString mapName = {}) : mapName_(std::move(mapName)) {}
    // Set once before preparing this session. Worker-owned; never routed to input.
    void setDiagnostics(AtomTrace* trace) { trace_ = trace; }
    // Stock retains existing text after a style-only write. New/invalidated
    // appearance templates use the latest style, with separately keyed rasters.
    void setMapTextStyle(const MapTextStyle& style) { maptext_.setStyle(style); }
    EntityMap(const EntityMap&) = delete;
    EntityMap& operator=(const EntityMap&) = delete;
    // Optional per-attempt diagnostics. Visits count template expansion, not cached instances.
    struct Stats {
        uint64_t candidates = 0, visits = 0, appearanceDecodes = 0, frameDecodes = 0, resourceReads = 0;
        uint64_t templateBuilds = 0, instanceUpdates = 0, culled = 0, sortedSprites = 0, sceneReuses = 0;
        uint64_t maptextRasters = 0;
        double traverseMs = 0, sortMs = 0, assembleMs = 0, pruneMs = 0;
    };
    // scene=true includes turfs, supported object images and client.screen in the same order.
    // The default entity-only mode is retained for isolated cache/glide reference tests.
    // Whole tiles the last prepared frame's screen objects reach outside the view (ScreenLoc::outside); the caller
    // hands them to TurfMap::prepare for the next frame.
    QMargins hudMargins() const { return hudMargins_; }
    MapSprites prepare(const model::World& world, const TurfMap::Frame& terrain,
                       const TurfMap::Read& read, int64_t nowNs = 0, Stats* stats = nullptr, bool scene = false);
private:
    const QString mapName_; // immutable scope; caches/instances never cross MAPs
    static constexpr uint64_t maxSceneVisits = 65536;
    std::weak_ptr<const VisibilityFrame> tracedVisibility_;
    struct Appearance : EntityAppearanceFields {
        proto::Appearance source;
        std::string state;
        uint32_t icon = UINT32_MAX, layerBits = 0;
        uint64_t checked = 0, revision = 0;
        bool present = false, valid = false;
    };
    struct Resource {
        uint32_t ordinal = 0, id = 0;
        bool mapped = false, ready = false;
        uint64_t revision = 0;
        IconCache::Status status = IconCache::Status::Unmapped;
        IconCache::Result result() const { return {id, revision, mapped, ready, status}; }
        bool operator==(const Resource& b) const {
            return ordinal == b.ordinal && id == b.id && mapped == b.mapped && ready == b.ready && revision == b.revision;
        }
    };
    using FrameKey = std::tuple<uint32_t, uint64_t, int>; // resource, dependency revision, sheet cell
    struct CachedFrame {
        std::shared_ptr<const QImage> image;
        uint64_t used = 0;
    };
    struct Part {
        std::shared_ptr<const QImage> image;
        QPoint position;
        int plane = 0;
        float layer = 0;
        std::array<float, 4> tint{1, 1, 1, 1};
        ColorMatrix color = identityColor;
        int blend = 0;
        bool master = false;
        bool nearest = false; // MapSprite::nearest
        QString renderSource, renderTarget;
        QSize size;
        std::optional<StaticAffine> affine;
        DmiIcon::State animation;
        FrameKey frameKey{};
        // The state's "movement = 1" twin, drawn instead while the atom glides (stock 516.1689, stock-parity-world
        // moving-atoms: on the atom and on its overlays, back to the ordinary state when the glide ends).
        std::optional<std::pair<DmiIcon::State, FrameKey>> moving;
        bool flickRoot = false, smooth = false, maptext = false, footprint = false;
        int textMargin = 0; // MapSprite::textMargin
        bool inkInteractive = true;
        std::shared_ptr<const PlaneEffects> planeEffects;
        std::shared_ptr<const AppearanceGroup> group;
        std::shared_ptr<const std::vector<uint32_t>> sourcePath;
    };
    struct Template {
        uint64_t checked = 0, revision = 0, visits = 0;
        std::vector<std::pair<uint32_t, uint64_t>> dependencies;
        std::vector<Resource> resources;
        std::vector<Part> parts;
        std::vector<size_t> animated;
        QPoint iconBottom;
        QSize iconSize; // root icon only; TOP/RIGHT align it, not the overlay union
        std::optional<StaticAffine> rootAffine;
        QSize rootSize;
        bool complete = true, affine = false, grouped = false;
        // The root's KEEP_TOGETHER surface, which its images are composed in.
        std::shared_ptr<const AppearanceGroup> rootGroup;
        bool joinsTarget = false; // an image template: drawn inside its target's KEEP_TOGETHER surface
        QString planeSourceDependency; // extended source appearance requires a plane, not a local target
        TraceFailure first;
        TraceReason diagnosticStatus = TraceReason::Unobserved;
        uint64_t evidenceRevision = 0;
        QRect bounds; // union of all parts, including offset/oversized overlays
    };
    struct Instance {
        uint64_t used = 0, revision = 0;
        QPoint origin, cell;
        QSize iconSize; // the root icon's, which a particle set is centred on
        uint32_t screenOrder = 0;
        uint64_t imageIdentity = 0, targetIdentity = 0, hudIdentity = 0;
        uint16_t key = 0;
        bool haveKey = false, masterPlacementRefused = false, masterPartsOffScene = false;
        const AppearanceGroup* joined = nullptr; // an image: the target surface its sprites were built into
        std::shared_ptr<const AppearanceGroup> rootGroup; // the KEEP_TOGETHER surface this instance drew, if any
        std::shared_ptr<const VisHit> visHit;
        MapSprites sprites; // world pixels, not tied to the terrain cache's origin
    };
    struct Draw { const Instance* instance; qsizetype part; };
    std::unordered_map<uint32_t, Appearance> appearances_;
    // Inherited plane, layer, tint, colour, blend and transform; assets/material only, never clocks.
    using VisContext = std::tuple<int, uint32_t, std::array<float, 4>, ColorMatrix, int, std::array<double, 6>>;
    // vis_flags a vis copy takes from its holder (stock 516.1688, tests/data/vis-world set b): VIS_INHERIT_ICON (1) and
    // VIS_INHERIT_ICON_STATE (2) with the holder's effective icon and state, VIS_INHERIT_LAYER (8) and VIS_INHERIT_PLANE
    // (0x10) from the context. DIR travels as the request's direction.
    using VisInherit = std::tuple<uint16_t, uint32_t, std::string>;
    struct TemplateCache {
        std::unordered_map<uint64_t, Template> ordinary, self;
        // Assets only: event timestamps/identity never enter shared template keys.
        std::map<std::tuple<uint32_t, int, uint32_t, std::string>, Template> flick;
        std::map<std::tuple<uint32_t, uint32_t, int>, Template> image;
        std::map<std::tuple<uint32_t, int, VisContext, VisInherit>, Template> vis;
        void clear();
        void prune(uint64_t epoch);
    } templates_;
    struct OrdinaryRequest {};
    struct ImageRequest { uint32_t parent; };
    struct VisRequest { VisContext context; VisInherit inherit{}; };
    struct FlickRequest { uint32_t iconResIndex; const std::string& iconState; };
    using TemplateSource = std::variant<OrdinaryRequest, ImageRequest, VisRequest, FlickRequest>;
    enum class VisibilityPolicy { RespectInvisibility, SeeSelf };
    struct TemplateRequest {
        uint32_t rootId;
        int rootDir;
        TemplateSource source = OrdinaryRequest{};
        VisibilityPolicy visibility = VisibilityPolicy::RespectInvisibility;
    };
    // Sampling is independent of image inheritance and self visibility, but
    // never overrides flick assets or a preplanned vis template.
    struct SampleRequest {
        uint32_t rootId;
        int rootDir;
        std::variant<OrdinaryRequest, ImageRequest, VisRequest, FlickRequest> inheritance = OrdinaryRequest{};
        VisibilityPolicy visibility = VisibilityPolicy::RespectInvisibility;
    };
    struct Occurrence { uint64_t id = 0, used = 0; std::shared_ptr<const VisHit> hit; };
    std::map<std::vector<uint64_t>, Occurrence> occurrences_;
    uint64_t nextOccurrence_ = 0;
    std::unordered_map<uint64_t, Instance> instances_;
    IconCache icons_;
    MapTextCache maptext_;
    std::map<FrameKey, CachedFrame> frames_;
    uint64_t frameBytes_ = 0;
    // Attempt-scoped services have only the dependencies their stage consumes.
    // No instance/placement state, playback timestamps or whole-World access in
    // template construction. Their references never survive prepare().
    struct AppearanceLookup {
        const std::map<uint32_t, model::Appearance>& source;
        std::unordered_map<uint32_t, Appearance>& cache;
        uint64_t epoch;
        uint64_t& revision;
        int tile;
        Stats* stats;
        const Appearance& operator()(uint32_t id);
    };
    bool beginAttempt(const model::World& w, const TurfMap::Frame& terrain,
                      const TurfMap::Read& read, int64_t nowNs);
    void sortOccurrences(bool changed, const QRect& view, Stats* stats);
    void assembleOutput(bool changed, QPoint translation, const QRect& view,
                        bool blacknessActive, const std::vector<QRect>& blackness, Stats* stats);
    void prune();
    struct AnimationSampler {
        AppearanceLookup& appearance;
        int tile_;
        int64_t nowNs;
        uint64_t animationWork = 2'097'152;
        bool unsettled(const model::Atom::Chain& chain);
        bool sample(const model::Atom::Chain& chain, uint32_t settled, Appearance& out, bool& composing, bool hudMaster = false,
                    bool attachment = false);
    };
    struct FrameLookup {
        IconCache& icons_;
        std::map<FrameKey, CachedFrame>& frames_;
        uint64_t& frameBytes_;
        uint64_t epoch_;
        Stats* stats;
        uint64_t decodedPixels = 0; // per-attempt decode budget, shared with playback
        std::shared_ptr<const QImage> operator()(const FrameKey& key);
    };
    struct TemplateBuilder {
        AppearanceLookup& appearance;
        FrameLookup& frameImage;
        IconCache& icons_;
        MapTextCache& maptext_;
        TemplateCache& cache;
        uint64_t epoch_;
        uint64_t& revision_;
        int tile_, seeInvisible_;
        Stats* stats;
        // Bounded auxiliary evidence only; template decisions remain authoritative.
        bool trace_;
        std::map<uint64_t, TraceTree>& traceTrees_;
        const uint64_t& protectedTraceRevision_;
        uint64_t visTemplateWork = maxSceneVisits;
        // Set only around sample(): an image's animating target, sampled at the
        // frame time, in place of its settled appearance. Never cached.
        const Appearance* sampledParent = nullptr;
        int turn_ = 1; // client.dir: a root's icon direction turns with the map
        Resource resource(uint32_t ordinal);
        // The effects with their alpha-mask icons loaded; `complete` is cleared while one is not there yet.
        std::shared_ptr<const PlaneEffects> withMasks(std::shared_ptr<const PlaneEffects> effects, bool& complete);
        const Template& get(const TemplateRequest& request);
        // Caller owns the returned sample through placement. No shared scratch
        // result or borrowed sampled-appearance pointer can escape construction.
        Template sample(const SampleRequest& request, const Appearance& sampled);
        Template constructTree(const TemplateRequest& request, const Appearance* sampled);
    private:
        Template construct(const TemplateRequest& request, const Appearance* sampled);
    };
    std::vector<uint64_t> visible_, nextVisible_;
    std::vector<Draw> order_;
    MapSprites output_;
    // client.color: recoloured copies of filtered atoms' surfaces, kept while the colour stays the same.
    ColorMatrix clientTint_ = identityColor;
    std::map<const AppearanceGroup*, std::pair<std::shared_ptr<const AppearanceGroup>, std::shared_ptr<const AppearanceGroup>>> clientTintGroups_;
    std::vector<QRect> blackness_;
    bool blacknessActive_ = false;
    QRect view_;
    QMargins hudMargins_;
    QPoint translation_;
    AtomTrace* trace_ = nullptr;
    // Bounded auxiliary evidence; template decisions remain authoritative on eviction.
    std::map<uint64_t, TraceTree> traceTrees_;
    uint64_t protectedTraceRevision_ = 0;
    uint64_t epoch_ = 0, revision_ = 0;
    int tile_ = 0, seeInvisible_ = -1, turn_ = 1;
    uint64_t fonts_ = 0; // resourceFontGeneration() the templates' text was drawn at
};
}

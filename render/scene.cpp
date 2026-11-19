// Scene assembly (see scene.h), organised as:
//   buildGrid()      cells, LOS cull, movables in id order, vis_contents, client.images
//   buildApp()       appearance build (+ inheritOverlayIcon/Color, applyOverlayDir, stateFor)
//   placeApp()       viewport-pixel placement, plane/layer resolution
//   placeHud()       client.screen objects and their client.images, as screen-space layers in the plane pipeline
//   run()            plane grouping and the layer sort (the sort lives HERE)
#include "render/scene.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <climits>
#include <chrono>
#include <cmath>
#include <functional>
#include <cstring>
#include <map>
#include <list>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "model/gomath.h"
#include "model/statpanel.h"
#include "net/resource.h"
#include "net/textmacro.h"
#include "render/ddmi.h"
#include "render/easing.h"
#include "render/maptextpaint.h"
#include "render/screenloc.h"
#include "render/xform.h"

namespace fugue::render {

namespace {

int g_sceneMutation = 0;

constexpr uint32_t kRegistryA = 0x80000000u;
int floorDivI(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
constexpr uint32_t kNoIcon = 0xFFFF;
constexpr int kDefaultViewRange = 7;
constexpr int kMinAbsolutePlane = -16384;
constexpr int kFloatPlane = -32767;  // FLOAT_PLANE
constexpr double kFlyLayer = 5;      // FLY_LAYER
constexpr uint32_t kSightMobs = 1u << 2, kSightObjs = 1u << 3, kSightTurfs = 1u << 4;
constexpr int kDirEast = 4, kDirWest = 8;

// Undo DD's int16 truncation of a float-relative plane.
double truePlane(int wire) {
    if (wire > -kMinAbsolutePlane - 1) return double(int32_t(wire) - 65536);
    return double(wire);
}
double resolvePlane(double plane, double base) {
    if (plane < kMinAbsolutePlane) return base + (plane - kFloatPlane);
    return plane;
}

// The subset of an appearance the renderer reads, built from the model's decoded record.
struct Ap {
    uint32_t iconResID = kNoIcon;
    std::string iconState;
    int dir = 0;
    uint32_t flags = 0;
    double layer = 0;
    bool hasPlane = false;
    int plane = 0;
    bool hasColor = false;
    int cr = 0, cg = 0, cb = 0, ca = 0;
    int pixelX = 0, pixelY = 0, pixelW = 0, pixelZ = 0;
    std::string screenLoc, renderSource, maptext, name;
    int mtW = 0, mtH = 0, mtX = 0, mtY = 0;  // maptext_width / _height / _x / _y
    std::vector<uint32_t> overlays, underlays;
    bool hasTransform = false;
    std::array<float, 6> transform{1, 0, 0, 0, 1, 0};  // the six float32 cells, DM matrix(a,b,c,d,e,f) order
    bool hasPixelOffsets = false;
    int invisibility = 0;
    bool hasBlend = false;
    int blend = 0;
    bool hasColorMatrix = false;
    ColorMatrixPtr cmat;       // 20 float32 -> float64
    std::string renderTarget;  // the SECOND 0x1000 cstr (RenderTarget)
    struct Flt {
        int type = 0;
        uint32_t mask = 0;
        Bytes params;
        std::string str;  // the 0x200 cstr (render_source)
    };
    std::vector<Flt> filters;  // the 0x0800 chain, params raw in ascending param-bit order

    // MouseOpacity: a 2-bit field at bits 12-13, rotated so the default (1) encodes as 0.
    int mouseOpacity() const { return int(((flags >> 12) + 1) & 3); }
    bool opaque() const { return flags & 1; }              // Opaque
    bool mouseWheel() const { return flags & 0x100000; }   // declares MouseWheel or MouseMove
    bool mouseHover() const { return flags & 0x800; }      // declares a hover proc (Entered/Exited/Move/Wheel)
    bool mouseMove() const { return flags & 0x80000; }     // declares MouseMove
    bool dirValid() const { return flags & 0x200; }        // DirValid (ApprDirValid)
    bool resetColor() const { return (flags >> 21) & 2; }  // DM appearance flag: reset colour
    bool resetAlpha() const { return (flags >> 21) & 4; }  // DM appearance flag: reset alpha
    bool keepTogether() const { return ((flags >> 21) & 32) != 0; }  // DM appearance flag KEEP_TOGETHER
    bool keepApart() const { return ((flags >> 21) & 64) != 0; }     // DM appearance flag KEEP_APART
    bool planeMaster() const { return ((flags >> 21) & 128) != 0; }
    bool passMouse() const { return ((flags >> 21) & 1024) != 0; }  // DM appearance flag PASS_MOUSE
    // image.override: bit 0x40000 of the low (client-internal) flags word, set on images with override = 1.
    bool overrides() const { return (flags & 0x40000) != 0; }
};

}  // namespace

struct SceneCache::Impl {
    struct Entry {
        uint32_t id;
        proto::Appearance fields;  // what `parsed` was built from: a recycled id with new fields misses
        std::shared_ptr<const Ap> parsed;
        size_t bytes;
    };
    size_t maxBytes, maxEntries, bytes = 0;
    std::list<Entry> recent;  // MRU first; shared parsed values keep active assemblers safe during eviction
    std::unordered_map<uint32_t, std::list<Entry>::iterator> index;

    Impl(size_t b, size_t n) : maxBytes(b), maxEntries(n) {}
    void erase(std::list<Entry>::iterator it) {
        bytes -= it->bytes;
        index.erase(it->id);
        recent.erase(it);
    }
    bool find(uint32_t id, const proto::Appearance& fields, std::shared_ptr<const Ap>& result) {
        auto found = index.find(id);
        if (found == index.end()) return false;
        auto it = found->second;
        if (it->fields != fields) {
            erase(it);
            return false;
        }
        result = it->parsed;
        recent.splice(recent.begin(), recent, it);
        return true;
    }
    // The heap a record holds, besides sizeof(proto::Appearance).
    static size_t heapBytes(const proto::Appearance& f) {
        size_t n = 0;
        for (const auto* s :
             {&f.name, &f.suffix, &f.iconState, &f.text, &f.maptext, &f.screenLoc, &f.renderSource, &f.renderTarget})
            n += s->capacity() + 1;
        n +=
            f.verbs.capacity() * sizeof(uint16_t) + (f.overlays.capacity() + f.underlays.capacity()) * sizeof(uint32_t);
        n += f.vars.capacity() * sizeof(proto::Appearance::Var) +
             f.filters.capacity() * sizeof(proto::Appearance::Filter);
        for (const auto& v : f.vars) n += v.data.capacity();
        for (const auto& flt : f.filters) n += flt.params.capacity() + flt.str.capacity() + 1;
        return n;
    }
    void insert(uint32_t id, const proto::Appearance& fields, const std::shared_ptr<const Ap>& parsed) {
        if (!maxEntries) return;
        Entry entry{id, fields, parsed, 0};
        size_t size = sizeof(Entry) + heapBytes(entry.fields);
        if (parsed) {
            // Keep dynamic-field accounting in sync with Ap; fixed fields are covered by sizeof(Ap).
            const auto& a = *parsed;
            size += sizeof(Ap);
            for (const auto* s : {&a.iconState, &a.screenLoc, &a.renderSource, &a.maptext, &a.name, &a.renderTarget})
                size += s->capacity() + 1;
            size += (a.overlays.capacity() + a.underlays.capacity()) * sizeof(uint32_t);
            size += a.filters.capacity() * sizeof(Ap::Flt);
            for (const auto& f : a.filters) size += f.params.capacity() + f.str.capacity();
            if (a.cmat) size += sizeof(ColorMatrix);
        }
        if (size > maxBytes) return;
        while (!recent.empty() && (recent.size() >= maxEntries || size > maxBytes - bytes))
            erase(std::prev(recent.end()));
        entry.bytes = size;
        recent.push_front(std::move(entry));
        try {
            index.emplace(id, recent.begin());
        } catch (...) {
            recent.pop_front();
            throw;
        }
        bytes += size;
    }
};

SceneCache::SceneCache(size_t maxBytes, size_t maxEntries) : impl_(std::make_unique<Impl>(maxBytes, maxEntries)) {}
SceneCache::~SceneCache() = default;
void SceneCache::clear() {
    impl_->index.clear();
    impl_->recent.clear();
    impl_->bytes = 0;
}
size_t SceneCache::retainedBytes() const { return impl_->bytes; }
size_t SceneCache::entries() const { return impl_->recent.size(); }

namespace {
// An appearance as the assembler works with it.
struct RApp {
    uint32_t appid = 0;
    const Icon* icon = nullptr;
    std::string state;
    int dir = 0;
    bool dirValid = false;  // dir is its OWN, not the parent's (applyOverlayDir)
    bool underlay = false;  // placed BEFORE its parent's own icon (placeApp, flatten)
    int frame = 0;
    bool hasTint = false;
    uint8_t tr = 255, tg = 255, tb = 255;
    int alpha = 255;
    double layer = 0, plane = 0;
    int px = 0, py = 0;
    bool hasXform = false;
    std::array<float, 6> xf{1, 0, 0, 0, 1, 0};
    std::vector<RApp> overlays;
    uint8_t blend = 0;  // a BlendMode
    ColorMatrixPtr cmat;
    std::string relay;       // render_source: a relay draws a slate, not an icon
    std::string target;      // render_target: relays of this name draw this atom's image
    bool passMouse = false;  // PASS_MOUSE: a relay's pixels answer for its source atom
    std::string maptext;     // drawn by placeApp in a box of mtW x mtH at offset mtX, mtY
    int mtW = 0, mtH = 0, mtX = 0, mtY = 0;
    int hitAtom = -1;  // HitScene::atoms index of the TOP-LEVEL atom this appearance belongs to (hit collection only)
    bool hasParticles = false;  // the atom carries a particle set (model::World::particles), drawn over its own icon
    uint32_t particleKey = 0;
    bool borrowsHit = false;  // a client.image: hitAtom is its PARENT's, whose position/facing it must not overwrite
    bool keepTogether =
        false;  // KEEP_TOGETHER: this appearance and its overlays are composed as one image (placeKeptTogether)
    bool keepApart = false;  // KEEP_APART: an overlay of a KEEP_TOGETHER parent that is drawn on its own
    bool holdsCell =
        false;  // a map atom's own appearance: drawing nothing, it still has a footprint (Placed::footprint)
    bool hitOnly =
        false;  // a client.image drawn inside its parent's KEEP_TOGETHER group: here it only answers the mouse
    std::vector<RApp> groupImages;  // a KEEP_TOGETHER parent's client.images, drawn in its group and never hit there
};

using Tile = std::vector<RApp>;

struct Placed {
    DrawLayer layer;
    double plane = 0;
    double sortLayer = 0;
    int seq = 0;
    int atom = -1;
    bool hitVisible = true;  // false for layers the hit sidecar never saw (blackness, group overlays)
    bool screen = false;     // a client.screen layer (drawKey)
    int cell = -1;           // a map layer's view cell, mapCell(x, y) (drawKey)
    DrawKey key;             // set by compositePlanes
    HitFrame frame;          // the owning atom's icon frame (hittest.h), for icon-x / vis-x
    bool own = false;        // the atom's own icon, not an overlay/underlay (HitLayer::own)
    bool maptext = false;    // a maptext box (HitLayer::maptext)
    bool footprint = false;  // HitLayer::footprint: hit sidecar only, never drawn
    bool noDraw =
        false;  // a part of a kept-together image: answers the mouse as itself, drawn through the group's layer
    bool passMouse = false;  // HitLayer::passMouse
};

// A collected PLANE_MASTER: its plane, how it composites, and the overlays that join its group.
struct MasterRec {
    double plane = 0;
    std::shared_ptr<PlaneMasterSpec> spec;
    std::vector<RApp> overlays;
    // The master's mouse_opacity == 0: everything drawn on its plane is mouse-transparent, whatever each atom says.
    // Stock DreamSeeker 516 gives the click to whatever lies under an overlay that sits on such a plane: the click goes to
    // the turf where only that overlay paints. With the master at 1, or no master, the overlay answers for its atom.
    bool hideFromMouse = false;
};

// The colour/blend half of a DrawLayer, from the appearance it draws.
void setFx(DrawLayer& l, const RApp& a) {
    l.hasTint = a.hasTint;
    l.tintR = a.tr;
    l.tintG = a.tg;
    l.tintB = a.tb;
    l.alpha = a.alpha;
    l.blend = a.blend;
    l.cmat = a.cmat;
}

class Assembler {
public:
    Assembler(const model::Session& s, ResStore& res, int64_t clockNs, SceneStats& st, bool withHit = false,
              std::string_view mapStyle = {}, SceneCache::Impl* cache = nullptr)
        : s_(s), w_(s.world()), res_(res), clockNs_(clockNs), st_(st), mapStyle_(mapStyle), cache_(cache) {
        if (withHit) hit_ = std::make_shared<HitScene>();
    }
    std::shared_ptr<HitScene> takeHit() { return std::move(hit_); }

    bool run(DrawList& out, std::string* err);
    // A SECONDARY map control (assembleSecondaryMap): only the client.screen objects whose screen_loc names `mapName`.
    bool runSecondary(std::string_view mapName, DrawList& out, std::string* err);

private:
    std::string mapName_;  // the map control being drawn: "" = the default map
    const model::Session& s_;
    const model::World& w_;
    ResStore& res_;
    int64_t clockNs_;
    int scrollX_ = 0, scrollY_ = 0;  // eye scroll offset at clockNs_, set by buildGrid
    // What the map layers are moved by (the eye scroll plus the client.pixel_* view offset, image pixels: +x right,
    // +y down), and the ring of tiles beyond the view drawn so the move reveals no gutter. Set by buildGrid / run.
    int shiftX_ = 0, shiftY_ = 0, ring_ = 0;
    SceneStats& st_;
    std::string mapStyle_;  // the default map control's `style`: maptext's sheet (render/maptextpaint.h)
    std::string err_;
    bool failed_ = false;
    std::shared_ptr<HitScene> hit_;  // non-null => collect the hit-test sidecar
    int curAtom_ = -1;               // placeApp: the top-level atom being placed
    HitFrame curFrame_;              // placeApp: that atom's icon frame, shared by all its layers
    int newHitAtom(uint32_t appid, uint8_t kind, uint32_t ref, bool screen);
    void finishHit(DrawList& dl);  // HitScene::anyHover, and the scene handed to the list

    SceneCache::Impl* cache_;
    std::unordered_map<uint32_t, std::shared_ptr<const Ap>>
        apCache_;  // pins values even if the bounded cache evicts them
    struct SpriteKey {
        const Icon* ic;
        int state, slot, frame;
        bool operator<(const SpriteKey& o) const {
            return std::tie(ic, state, slot, frame) < std::tie(o.ic, o.state, o.slot, o.frame);
        }
    };
    std::map<SpriteKey, SpritePtr> spriteCache_;
    std::map<std::pair<int, int>, SpritePtr> blankCache_;  // transparent w x h sprites for HitLayer::footprint

    // grid
    int maxX_ = 0, maxY_ = 0;
    // Only the cell window (gridX0_/gridY0_ = its bottom-left world coords, gridW_ x gridH_) is stored; tiles outside it
    // are not allocated.
    int gridX0_ = 0, gridY0_ = 0, gridW_ = 0, gridH_ = 0;
    std::vector<Tile> grid_;
    const Tile* tileIn(int x, int y) const {
        if (x < 1 || y < 1 || x > maxX_ || y > maxY_) return nullptr;
        int lx = x - gridX0_, ly = y - gridY0_;
        if (lx < 0 || ly < 0 || lx >= gridW_ || ly >= gridH_) return nullptr;
        return &grid_[size_t(ly) * size_t(gridW_) + size_t(lx)];
    }
    // Hidden cells: the viewer cannot see them (no line of sight), but BYOND paints its own opaque blackness
    // over them on plane 0. Non-empty only when a visibility pass ran (haveVisible); see appendBlackness.
    std::set<std::pair<int, int>> hidden_;
    std::vector<MasterRec> masters_;
    // Atom render targets (screen objects and map atoms, not plane masters) by name, at their own origin.
    // A screen object's target also keeps its screen_loc position (HitFrame::visLeft/visBottom) and its hit atom.
    struct Target {
        RApp app;
        bool hasOrigin = false;
        int left = 0, bottom = 0;
    };
    std::map<std::string, Target> targets_;
    std::map<uint32_t, int> screenTargetHit_;  // client.screen id -> its hit atom, shared with placeHud
    int relayDepth_ = 0;
    std::vector<Placed>* apartSink_ =
        nullptr;  // where a KEEP_APART appearance nested in a kept-together group is placed

    void fail(const std::string& e) {
        if (!failed_) {
            failed_ = true;
            err_ = e;
        }
    }

    const Ap* ap(uint32_t appid);
    const Icon* iconFor(const Ap& a);
    std::string stateFor(const Ap& a, const Icon* ic) const;
    bool hiddenFromClient(uint32_t appid);
    bool buildApp(uint32_t appid, std::set<uint32_t>& visited, RApp& out);
    void inheritOverlayColor(RApp& ov, bool pHas, uint8_t pr, uint8_t pg, uint8_t pb, int pAlpha);
    void inheritOverlayIcon(RApp& ov, const Icon* parent);
    static void applyOverlayDir(RApp& ov, int dir);
    int frameFor(const Icon* ic, const std::string& state, int64_t startNs = 0) const;
    void applyIconAnimation(RApp& a, int64_t startNs = 0) const;
    int64_t tileIconStart(int x, int y, int z) const;  // 0-based cell
    int64_t hudIconStart(uint32_t ref) const;
    void countUnmodeled(const Ap& a);

    SpritePtr sprite(const RApp& a, bool& drawable);
    bool drawableState(const RApp& a, int& stateIdx);

    void visChildren(const std::vector<uint32_t>& children, std::set<uint32_t>& visited, std::vector<RApp>& out,
                     const RApp* holder);

    // client.images (per-movable images, turf images, image vis children), read once per frame.
    struct ImgRec {
        uint32_t id = 0, appid = 0, locRef = 0, cell = 0;
        uint8_t locKind = 0;
        std::vector<uint32_t> visKids;
    };
    std::vector<ImgRec> imgs_;
    std::unordered_map<uint32_t, std::vector<size_t>> imgsOn_;  // movable key -> imgs_ indices, ascending image id
    void collectImages();
    bool imageApp(const ImgRec& im, RApp& out);
    bool imagesOn(uint32_t key, const RApp& parent, std::vector<RApp>& out, std::vector<RApp>* grouped = nullptr);
    void visContents(uint32_t id, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder);

    // world
    bool buildGrid(int& originX, int& originY, int& winW, int& winH, int& extentW, int& extentH, int& vx0, int& vy0);
    void tileSize(int& tw, int& th) const;
    void placeApp(const RApp& a, int baseX, int baseY, int tileH, double parentPlane, double parentLayer,
                  bool hasParent, std::vector<Placed>& out, const std::array<float, 6>* inheritedXf = nullptr);
    bool placeKeptTogether(const RApp& a, int baseX, int baseY, int tileH, double parentPlane, double parentLayer,
                           bool hasParent, std::vector<Placed>& out, const std::array<float, 6>* inheritedXf);
    void markParticles(RApp& a, uint32_t key) const;
    void placeParticles(const RApp& a, int iconX, int iconY, int iconW, int iconH, double plane, double layer,
                        std::vector<Placed>& out);
    bool animatedApp(uint32_t id, const model::Atom& m, RApp& out);
    void flickOverride(uint32_t id, RApp& out);
    bool tweenChain(const model::Atom::Chain& ch, uint32_t settled, RApp& out);
    void glideOffset(const model::Atom& m, int tileW, int tileH, int& px, int& py) const;
    bool animEndpointCarriesPixels(const model::Atom& m) const;
    void clientViewOffset(int& x, int& y);
    int ringFor(int tw, int th) const;
    void placeHud(int cols, int rows, int tw, int th, std::vector<Placed>& placed);
    bool screenApp(uint32_t id, uint32_t slotAppid, RApp& ra);
    void collectTargets(int cols, int rows, int tw, int th);
    void placeRelayed(const RApp& relay, const Target& src, int baseX, int baseY, int tileH, double plane, double layer,
                      std::vector<Placed>& out);
    void collectMasters(uint32_t appid, std::set<uint32_t>& seen);
    void appendBlackness(std::vector<Placed>& placed, int originX, int originY, int extentW, int extentH, int tw,
                         int th);
    bool compositePlanes(std::vector<Placed>& placed, DrawList& dl, int th);
    bool mastersFor(uint32_t appid);
    struct FlatLayer {
        DrawLayer l;
        double plane, layer;
    };
    void flatten(const RApp& a, int w, int h, std::vector<FlatLayer>& out, bool bottom = false);

public:
    bool appearanceIcon(uint32_t appid, int dir, int& w, int& h, std::vector<uint8_t>& rgba, int64_t startNs = 0);

private:
    void placeWarpedScreenObj(const RApp& ra, double anchorX, double anchorBottomY, int canvasW, int canvasH,
                              std::vector<Placed>& placed);
};

// ---- appearances -----------------------------------------------------------------------------

const Ap* Assembler::ap(uint32_t appid) {
    auto c = apCache_.find(appid);
    if (c != apCache_.end()) return c->second.get();
    auto it = w_.appearances.find(appid);
    if (it == w_.appearances.end()) {
        apCache_[appid] = nullptr;
        return nullptr;
    }
    std::shared_ptr<const Ap> cached;
    const proto::Appearance& v = it->second.fields;
    if (cache_ && cache_->find(appid, v, cached)) {
        ++st_.appearanceCacheHits;
        const Ap* result = cached.get();
        apCache_[appid] = std::move(cached);
        return result;
    }
    ++st_.appearanceParses;
    struct Timer {
        SceneStats& stats;
        std::chrono::steady_clock::time_point start;
        ~Timer() {
            if (stats.profilePreparation)
                stats.appearanceBuildMs +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
    } timer{st_, st_.profilePreparation ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};
    auto a = std::make_shared<Ap>();
    a->iconResID = v.iconResID;
    a->iconState = v.iconState;
    a->dir = v.dir;
    a->flags = v.appearanceFlags;
    float lf;
    std::memcpy(&lf, &v.layerBits, 4);
    a->layer = double(lf);
    a->hasPlane = v.hasPlane;
    a->plane = v.plane;
    a->hasColor = v.hasColor;
    a->cr = v.color[0];
    a->cg = v.color[1];
    a->cb = v.color[2];
    a->ca = v.color[3];
    a->pixelX = v.pixelX;
    a->pixelY = v.pixelY;
    a->pixelW = v.pixelW;
    a->pixelZ = v.pixelZ;
    a->screenLoc = v.screenLoc;
    a->renderSource = v.renderSource;
    a->maptext = net::plainText(v.maptext);
    a->mtW = v.maptextW;
    a->mtH = v.maptextH;
    a->mtX = v.maptextX;
    a->mtY = v.maptextY;
    // Stripped of BYOND's embedded text-macro markers here, once, so every reader of HitAtom::name (status bar, verb menu,
    // local menu) gets the plain name. A name such as "\improper Scrubbers pipe" arrives with a marker prefix from stock
    // DreamDaemon, and stock DreamSeeker shows the plain name in its status bar.
    a->name = model::plainAtomName(v.name);
    a->overlays = v.overlays;
    a->underlays = v.underlays;
    a->hasTransform = v.hasTransform;
    a->hasPixelOffsets = v.hasPixelOffsets;
    for (size_t k = 0; k < 6; k++) std::memcpy(&a->transform[k], &v.transformBits[k], 4);
    a->renderTarget = v.renderTarget;
    for (const proto::Appearance::Filter& f : v.filters) {
        Ap::Flt ft;
        ft.type = f.type;
        ft.mask = f.paramMask;
        ft.params = f.params;
        ft.str = f.str;
        a->filters.push_back(std::move(ft));
    }
    a->invisibility = v.invisibility;
    a->hasBlend = v.hasBlend;
    a->blend = v.blend;
    if (v.hasColorMatrix) {
        a->hasColorMatrix = true;
        auto cm = std::make_shared<ColorMatrix>();
        for (size_t k = 0; k < 20; k++) {
            float f;
            std::memcpy(&f, &v.colorMatrixBits[k], 4);
            (*cm)[k] = double(f);
        }
        a->cmat = std::move(cm);
    }
    const Ap* p = a.get();
    apCache_[appid] = a;
    if (cache_) cache_->insert(appid, v, a);
    return p;
}

const Icon* Assembler::iconFor(const Ap& a) {
    if (a.iconResID == kNoIcon) return nullptr;
    if (a.iconResID >= w_.resources.size()) return nullptr;
    return res_.iconResolved(w_.resources[a.iconResID].first,
                             w_.resources);  // byRes: plain icons AND evaluated DDMI recipes
}

// stateFor: the appearance's own icon_state wins whenever the sheet has it (including ""); a miss
// degrades to the unnamed default state.
std::string Assembler::stateFor(const Ap& a, const Icon* ic) const {
    if (!ic) return "";
    if (ic->stateIndex(a.iconState) >= 0) return a.iconState;
    return "";
}

bool Assembler::hiddenFromClient(uint32_t appid) {
    if (!w_.haveSeeInvisible) return false;
    const Ap* a = ap(appid);
    if (!a) return false;
    return a->invisibility > int(w_.seeInvisible);
}

void Assembler::countUnmodeled(const Ap& a) {
    if (!a.maptext.empty()) st_.maptext++;
    // render_source relays, colour matrices and blend modes are modelled (placeApp, executor.cpp), and atom.transform
    // (xform.cpp; a screen object's through placeWarpedScreenObj).
}

int Assembler::newHitAtom(uint32_t appid, uint8_t kind, uint32_t ref, bool screen) {
    if (!hit_) return -1;
    HitAtom h;
    if (const Ap* a = ap(appid)) {
        h.name = a->name;
        h.mouseOpacity = a->mouseOpacity();
        h.wheel = a->mouseWheel();
        h.hover = a->mouseHover();
        h.mouseMove = a->mouseMove();
    }
    h.appid = appid;
    h.kind = kind;
    h.ref = ref;
    h.screen = screen;
    hit_->atoms.push_back(std::move(h));
    return int(hit_->atoms.size()) - 1;
}

bool Assembler::buildApp(uint32_t appid, std::set<uint32_t>& visited, RApp& out) {
    if (visited.count(appid)) return false;
    const Ap* a = ap(appid);
    if (!a) return false;
    visited.insert(appid);

    const Icon* ic = iconFor(*a);
    RApp ra;
    ra.appid = appid;
    ra.icon = ic;
    ra.state = stateFor(*a, ic);
    ra.dir = a->dir;
    ra.dirValid = a->dirValid();
    ra.alpha = 255;
    ra.layer = a->layer;
    ra.px = a->pixelX + a->pixelW;
    ra.py = a->pixelY + a->pixelZ;
    if (a->hasPlane) ra.plane = truePlane(a->plane);
    if (a->hasColor) {
        ra.hasTint = true;
        ra.tr = uint8_t(a->cr);
        ra.tg = uint8_t(a->cg);
        ra.tb = uint8_t(a->cb);
        ra.alpha = a->ca;
    }
    if (a->hasTransform) {
        ra.hasXform = true;
        ra.xf = a->transform;
    }
    // atom.color's LIST form and atom.blend_mode ride on the appearance (NOT inherited by overlays; only the scalar
    // colour is, inheritOverlayColor).
    if (a->cmat) ra.cmat = a->cmat;
    if (a->hasBlend) ra.blend = blendModeOfWire(a->blend);
    ra.relay = a->renderSource;
    ra.target = a->renderTarget;
    ra.passMouse = a->passMouse();
    ra.keepTogether = a->keepTogether();
    ra.keepApart = a->keepApart();
    ra.maptext = a->maptext;
    ra.mtW = a->mtW;
    ra.mtH = a->mtH;
    ra.mtX = a->mtX;
    ra.mtY = a->mtY;
    countUnmodeled(*a);

    // A KEEP_TOGETHER parent's colour and alpha apply to the composed image once (placeKeptTogether), not to each part.
    for (uint32_t uid : a->underlays) {
        RApp ov;
        if (buildApp(uid, visited, ov)) {
            inheritOverlayIcon(ov, ic);
            applyOverlayDir(ov, ra.dir);  // underlays follow the parent's dir exactly like overlays
            if (a->hasColor && !(ra.keepTogether && !ov.keepApart && g_sceneMutation != 50))
                inheritOverlayColor(ov, ra.hasTint, ra.tr, ra.tg, ra.tb, ra.alpha);
            ov.underlay = true;
            ra.overlays.push_back(std::move(ov));
        }
    }
    for (uint32_t oid : a->overlays) {
        RApp ov;
        if (buildApp(oid, visited, ov)) {
            inheritOverlayIcon(ov, ic);
            applyOverlayDir(ov, ra.dir);
            if (a->hasColor && !(ra.keepTogether && !ov.keepApart && g_sceneMutation != 50))
                inheritOverlayColor(ov, ra.hasTint, ra.tr, ra.tg, ra.tb, ra.alpha);
            ra.overlays.push_back(std::move(ov));
        }
    }
    visited.erase(appid);
    out = std::move(ra);
    return true;
}

// The parent's colour and alpha MULTIPLY into the overlay's own (white / 255 when it has none), over the overlay's whole
// subtree, and RESET_COLOR / RESET_ALPHA cut the chain at that node. Rounding is (x*y + 127) / 255.
void Assembler::inheritOverlayColor(RApp& ov, bool pHas, uint8_t pr, uint8_t pg, uint8_t pb, int pAlpha) {
    bool resetColor = false, resetAlpha = false;
    if (const Ap* ca = ap(ov.appid)) {
        resetColor = ca->resetColor();
        resetAlpha = ca->resetAlpha();
    }
    if (resetColor && resetAlpha) return;
    auto mul = [](uint8_t x, uint8_t y) { return uint8_t((int(x) * int(y) + 127) / 255); };
    if (!resetColor && pHas) {
        if (!ov.hasTint) {
            ov.hasTint = true;
            ov.tr = pr;
            ov.tg = pg;
            ov.tb = pb;
        } else {
            ov.tr = mul(ov.tr, pr);
            ov.tg = mul(ov.tg, pg);
            ov.tb = mul(ov.tb, pb);
        }
    }
    if (!resetAlpha) ov.alpha = (ov.alpha * pAlpha + 127) / 255;
    for (auto& o : ov.overlays)
        if (!(ov.keepTogether && !o.keepApart && g_sceneMutation != 50))
            inheritOverlayColor(o, pHas, pr, pg, pb, pAlpha);  // a kept-together group takes it once, on itself
}

void Assembler::inheritOverlayIcon(RApp& ov, const Icon* parent) {
    if (!parent) return;
    if (!ov.icon) {
        const Ap* ca = ap(ov.appid);
        if (ca && ca->iconResID == kNoIcon) {
            ov.icon = parent;
            ov.state = stateFor(*ca, parent);
        }
    }
    const Icon* inner = ov.icon ? ov.icon : parent;
    for (auto& o : ov.overlays) inheritOverlayIcon(o, inner);
}

// applyOverlayDir: an overlay/underlay without ApprDirValid (0x200) faces its parent's dir. One with it keeps its own,
// SOUTH included (a pipe manifold's arm sets dir = SOUTH explicitly).
void Assembler::applyOverlayDir(RApp& ov, int dir) {
    if (!ov.dirValid) ov.dir = dir;
    for (auto& o : ov.overlays) applyOverlayDir(o, dir);
}

// Icon-state frame selection (deciseconds since the Unix epoch, pinned clock). A finite-loop state plays from startNs
// (model::Atom::iconStartNs) and then holds its last frame; a looping state runs on the one shared clock.
int Assembler::frameFor(const Icon* ic, const std::string& state, int64_t startNs) const {
    if (!ic) return 0;
    int si = ic->stateIndex(state);
    if (si < 0) return 0;
    const IconState& st = ic->states[size_t(si)];
    if (st.loop > 0 && startNs != 0 && g_sceneMutation != 22) return frameAtTime(st, double(clockNs_ - startNs) / 1e8);
    return frameAtTime(st, double(clockNs_) / 1e8);
}

// Icon animation, applied to movables, HUD atoms, turf cells and every screen object. Overlays play from their parent's start.
void Assembler::applyIconAnimation(RApp& a, int64_t startNs) const {
    a.frame = frameFor(a.icon, a.state, startNs);
    for (auto& o : a.overlays) applyIconAnimation(o, startNs);
}

int64_t Assembler::tileIconStart(int x, int y, int z) const {
    auto it = w_.tileIconStart.find({uint16_t(x), uint16_t(y), uint16_t(z)});
    return it == w_.tileIconStart.end() ? 0 : it->second.ns;
}

int64_t Assembler::hudIconStart(uint32_t ref) const {
    auto atom = w_.atoms.find(ref);
    if (atom != w_.atoms.end() && atom->second.iconStartNs != 0) return atom->second.iconStartNs;
    auto it = w_.hudIconStart.find(ref);
    return it == w_.hudIconStart.end() ? 0 : it->second.ns;
}

// No icon draws nothing; a missing NAMED state is an error, a missing unnamed one draws nothing.
bool Assembler::drawableState(const RApp& a, int& stateIdx) {
    stateIdx = -1;
    if (!a.icon) return false;
    int si = a.icon->stateIndex(a.state);
    if (si < 0) {
        if (!a.state.empty()) fail("render: icon has no state \"" + a.state + "\"");
        return false;
    }
    stateIdx = si;
    return true;
}

// The frame pixels of one appearance (state, dir, frame). nullptr + fail() on error.
SpritePtr Assembler::sprite(const RApp& a, bool& drawable) {
    drawable = false;
    int si;
    if (!drawableState(a, si)) return nullptr;
    const IconState& st = a.icon->states[size_t(si)];
    int dir = a.dir;
    if (g_sceneMutation == 3) {
        if (dir == kDirEast)
            dir = kDirWest;
        else if (dir == kDirWest)
            dir = kDirEast;
    }
    int slot = dirIndex(int(st.dirs), dir);
    if (slot < 0) {
        fail("dmi: unsupported dir count " + std::to_string(st.dirs) + " (want 1, 4 or 8)");
        return nullptr;
    }
    SpriteKey key{a.icon, si, slot, a.frame};
    auto c = spriteCache_.find(key);
    if (c != spriteCache_.end()) {
        drawable = true;
        return c->second;
    }
    auto sp = std::make_shared<Sprite>();
    std::string e;
    if (!a.icon->frame(st, slot, a.frame, sp->px, &e)) {
        fail("dmi: " + e);
        return nullptr;
    }
    sp->w = a.icon->width;
    sp->h = a.icon->height;
    spriteCache_[key] = sp;
    drawable = true;
    return sp;
}

// ---- particles -------------------------------------------------------------------------------------

void Assembler::markParticles(RApp& a, uint32_t key) const {
    if (!w_.particles.count(key)) return;
    a.hasParticles = true;
    a.particleKey = key;
}

// Stock DreamSeeker 516 draws a set about the centre of the width x height box it centres on
// its holder's icon, clipped to that box. An icon particle is its icon_state's frame centred on (round-half-even(x),
// -round-half-even(y)) from there, through the particle's rotation and scale (point sampled) and the set's transform, its
// colour multiplied by the particle's; a set whose first particle has no icon draws every particle as a one-pixel dot, at
// ceil(centre + x - 0.5), ceil(centre - y - 0.5). A set with depth draws its far particles first. The placement, the
// clip, the sampling and the dot rule are measured on stock DreamSeeker (tests/particles_test.cpp).
void Assembler::placeParticles(const RApp& a, int iconX, int iconY, int iconW, int iconH, double plane, double layer,
                               std::vector<Placed>& out) {
    auto it = w_.particles.find(a.particleKey);
    if (it == w_.particles.end()) return;
    model::ParticleSet& s = it->second;
    int64_t frameNs = w_.frameNanos > 0 ? w_.frameNanos : (w_.tickNanos > 0 ? w_.tickNanos : model::kDefaultTickNanos);
    model::advanceParticles(s, clockNs_, frameNs, a.particleKey * 2654435761u ^ s.pid);
    s.drawnNs = clockNs_;
    if (s.live.empty()) return;
    int bx = (iconW - int(s.width)) / 2, by = (iconH - int(s.height)) / 2;
    int cx = iconX + (2 * bx + int(s.width)) / 2, cy = iconY + (2 * by + int(s.height)) / 2;
    const int clipL = iconX + bx, clipT = iconY + by, clipR = clipL + int(s.width), clipB = clipT + int(s.height);
    const bool dots = s.live.front().icon == 0xffff;
    std::vector<size_t> order(s.live.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t x, size_t y) { return s.live[x].pos[2] > s.live[y].pos[2]; });
    static const SpritePtr kDot = [] {
        auto d = std::make_shared<Sprite>();
        d->w = d->h = 1;
        d->px = {255, 255, 255, 255};
        return SpritePtr(d);
    }();
    const auto& M = s.transform;
    for (size_t i : order) {
        const model::Particle& p = s.live[i];
        uint32_t c = p.drawColor;
        DrawLayer l;
        l.alpha = int(c >> 24);
        if (l.alpha == 0) continue;
        l.hasTint = (c & 0xffffff) != 0xffffff;
        l.tintR = uint8_t(c);
        l.tintG = uint8_t(c >> 8);
        l.tintB = uint8_t(c >> 16);
        double x = p.pos[0], y = p.pos[1], z = p.pos[2];
        if (!dots) {
            x = std::nearbyint(x);
            y = std::nearbyint(y);
        }
        // the set's transform, row-vector in DM space (+y up), then the w divide
        double ox = x, oy = y;
        Mat t;
        if (s.hasTransform) {
            double w = x * M[3] + y * M[7] + z * M[11] + M[15];
            if (w <= 0) continue;
            ox = (x * M[0] + y * M[4] + z * M[8] + M[12]) / w;
            oy = (x * M[1] + y * M[5] + z * M[9] + M[13]) / w;
            t = Mat{M[0] / w, -M[4] / w, 0, -M[1] / w, M[5] / w, 0};
        }
        if (dots) {
            l.sprite = kDot;
            l.x = int(std::ceil(cx + ox - 0.5));
            l.y = int(std::ceil(cy - oy - 0.5));
        } else {
            RApp pa;
            if (p.icon < w_.resources.size()) pa.icon = res_.iconResolved(w_.resources[p.icon].first, w_.resources);
            if (!pa.icon) continue;
            pa.state = pa.icon->stateIndex(p.iconState) >= 0 ? p.iconState : std::string();
            pa.dir = 2;
            pa.frame = frameFor(pa.icon, pa.state);
            bool drawable;
            SpritePtr sp = sprite(pa, drawable);
            if (failed_) return;
            if (!drawable) continue;
            Mat q{p.m[0], p.m[1], 0, p.m[2], p.m[3], 0};
            Mat m = matMul(t, q);
            int ux = cx + int(ox) - (sp->w + 1) / 2, uy = cy - int(oy) - (sp->h + 1) / 2;
            if (s.hasTransform) {
                ux = int(std::floor(cx + ox - sp->w / 2.0 + 0.5));
                uy = int(std::floor(cy - oy - sp->h / 2.0 + 0.5));
            }
            if (m.a == 1 && m.b == 0 && m.d == 0 && m.e == 1) {
                l.sprite = sp;
                l.x = ux;
                l.y = uy;
            } else {
                SpritePtr img = applyTransform(*sp, m, Sampling::Nearest);
                l.sprite = img;
                l.x = ux + int(gomath::round(double(sp->w - img->w) / 2));
                l.y = uy + int(gomath::round(double(sp->h - img->h) / 2));
            }
        }
        int x0 = std::max(l.x, clipL), y0 = std::max(l.y, clipT);
        int x1 = std::min(l.x + l.sprite->w, clipR), y1 = std::min(l.y + l.sprite->h, clipB);
        if (x0 >= x1 || y0 >= y1) continue;
        if (x0 != l.x || y0 != l.y || x1 != l.x + l.sprite->w || y1 != l.y + l.sprite->h) {
            auto cut = std::make_shared<Sprite>();
            cut->w = x1 - x0;
            cut->h = y1 - y0;
            cut->px.resize(size_t(cut->w) * size_t(cut->h) * 4);
            for (int yy = 0; yy < cut->h; yy++)
                std::memcpy(&cut->px[size_t(yy) * size_t(cut->w) * 4],
                            &l.sprite->px[(size_t(y0 - l.y + yy) * size_t(l.sprite->w) + size_t(x0 - l.x)) * 4],
                            size_t(cut->w) * 4);
            l.sprite = std::move(cut);
            l.x = x0;
            l.y = y0;
        }
        Placed pl;
        pl.layer = std::move(l);
        pl.plane = plane;
        pl.sortLayer = layer;
        pl.seq = int(out.size());
        pl.atom = curAtom_;
        pl.hitVisible = false;
        out.push_back(std::move(pl));
    }
}

// ---- vis_contents ------------------------------------------------------------------------------

// The holder's atom.transform reaches its vis_contents. Stock DreamSeeker 516 draws a child of a scale(1.5)
// holder 1.5x about its own centre, with its pixel_x offset unscaled (it spans -8..+40 px of the holder's centre, not
// 0..48), and a click on it names the child with vis-x/vis-y and icon-x/y through that transform in the child's own frame.
// So the child is placed as if the transform were its own. A child with a transform of its own gets holder x child (child
// first). A test-only hook makes a child keep only its own transform.
static std::array<float, 6> composeDm(const std::array<float, 6>& h, const std::array<float, 6>& c) {
    // DM matrix cells a,b,c,d,e,f: x' = a x + b y + c, y' = d x + e y + f. Apply c first, then h.
    return {h[0] * c[0] + h[1] * c[3], h[0] * c[1] + h[1] * c[4], h[0] * c[2] + h[1] * c[5] + h[2],
            h[3] * c[0] + h[4] * c[3], h[3] * c[1] + h[4] * c[4], h[3] * c[2] + h[4] * c[5] + h[5]};
}

void Assembler::visChildren(const std::vector<uint32_t>& children, std::set<uint32_t>& visited, std::vector<RApp>& out,
                            const RApp* holder) {
    for (uint32_t child : children) {
        auto cs = w_.atoms.find(child);
        if (cs == w_.atoms.end()) continue;
        if (hiddenFromClient(cs->second.appid)) continue;
        std::set<uint32_t> v2;
        RApp ca;
        if (!buildApp(cs->second.appid, v2, ca)) continue;
        // Stock DreamSeeker 516 animates a vis_contents item on the child's own icon start: a looping state on the shared
        // clock, a finite one from when the child was first sent or changed its icon_state; neither its holder's icon
        // change nor its addition to a second holder restarts it.
        applyIconAnimation(ca, cs->second.iconStartNs);
        if (cs->second.dir != 0) ca.dir = cs->second.dir;
        if (hit_)
            ca.hitAtom =
                newHitAtom(cs->second.appid, (child & kRegistryA) ? kAtomMob : kAtomObj, child & ~kRegistryA, false);
        ca.px += cs->second.pixelX;
        ca.py += cs->second.pixelY;
        markParticles(ca, child);
        if (holder && holder->hasXform && g_sceneMutation != 21) {
            ca.xf = ca.hasXform ? composeDm(holder->xf, ca.xf) : holder->xf;
            ca.hasXform = true;
        }
        out.push_back(std::move(ca));
        const RApp held = out.back();  // a grandchild takes this child's (now inherited) transform
        visContents(child, visited, out, &held);
    }
}

void Assembler::visContents(uint32_t id, std::set<uint32_t>& visited, std::vector<RApp>& out, const RApp* holder) {
    if (visited.count(id)) return;
    visited.insert(id);
    auto it = w_.visContents.find(id);
    if (it != w_.visContents.end()) visChildren(it->second, visited, out, holder);
    visited.erase(id);
}

// ---- client.images -------------------------------------------------------------------------------
// Stock DreamDaemon sends a speech bubble and a runechat as image frames (ImageCreate with an image id, appid and a
// mob location, then ImageModify and Animate). Stock DreamSeeker 516 draws the bubble over the speaker and the runechat
// maptext above it.

// The decoder's image records: kind 0x01 = obj slot, 0x02 = mob slot (the RegistryA space), 0x07 = a
// turf's flat cell; visContents Type 2 cells name obj slots (any other Type is skipped).
void Assembler::collectImages() {
    imgs_.clear();
    imgsOn_.clear();
    for (const auto& [id, op] : w_.images) {  // ascending id: the stable draw order across runs
        ImgRec im;
        im.id = id;
        im.appid = op.appid;
        im.locKind = op.locKind;
        im.locRef = op.locRef;
        im.cell = op.cell;
        for (const proto::VisItem& c : op.visContents)
            if (c.type == 2) im.visKids.push_back(c.value);
        // An obj slot keys bare, a mob slot with kRegistryA (separate id spaces: the KIND matters)
        if (im.locKind == 0x01)
            imgsOn_[im.locRef].push_back(imgs_.size());
        else if (im.locKind == 0x02)
            imgsOn_[kRegistryA | im.locRef].push_back(imgs_.size());
        imgs_.push_back(std::move(im));
    }
}

// The image's appearance at the render clock, tweening its animate() chain (a runechat fades in from
// alpha 0) exactly as a movable's is. Its icon state animates like a movable's, as DreamSeeker animates other map atoms.
bool Assembler::imageApp(const ImgRec& im, RApp& out) {
    auto ch = w_.imageChains.find(im.id);
    bool ok;
    if (ch == w_.imageChains.end() || ch->second.steps.empty()) {
        std::set<uint32_t> v;
        ok = buildApp(im.appid, v, out);
    } else {
        ok = tweenChain(ch->second, im.appid, out);
    }
    if (ok) {
        auto st = w_.imageIconStart.find(im.id);
        applyIconAnimation(out, st == w_.imageIconStart.end() ? 0 : st->second.ns);
    }
    return ok;
}

// The images attached to one movable or screen object (its key: an obj slot bare, a mob slot with kRegistryA;
// the loc kind matters, as the two registries are separate id spaces). An image is not an atom for the mouse: a hit on it
// names its parent (the DM Reference on image mouse_opacity). Its vis_contents draw whether or not the holder itself
// resolved (a radial menu is a blank holder whose buttons are all children).
// For the mouse, an image's pixels are its parent's in every respect but shape (stock DreamSeeker 516):
//   * a click on an image's icon names the parent, with icon-x/icon-y in the image's own frame, at the parent's location;
//   * the image's own mouse_opacity and alpha are ignored: the parent's mouse_opacity governs (a parent at 0 takes nothing,
//     image pixels included);
//   * an image's maptext takes no click at all (hittest.h "maptext").
// An image's FLOAT_PLANE is its parent's, as an overlay's is: resolving it against plane 0 would hide every such image under
// the turfs. Its FLOAT_LAYER, or any negative layer, is a fixed FLY_LAYER (5) whatever the parent's layer
// (stock DreamSeeker 516): over a mob at 4, under its own parent at 6 or more. A tie at 5 goes by arrival order, the image
// right after its parent, so it beats an obj at 5 when the parent is a mob and loses to one created after its obj parent.
// It also takes its parent's colour and alpha, multiplied into its own (RESET_COLOR / RESET_ALPHA cut them), as an overlay does.
// A screen object's image takes the same layer rule (-2 or FLOAT_LAYER is 5, an explicit layer absolute, a tie 5 right after its
// parent) and the same colour and alpha.
// A KEEP_TOGETHER parent composes its image inside the group, sorted by layer with the parent's overlays and underlays (so
// the parent's alpha applies once): the image goes to `grouped` with its own colour, and its map entry becomes hit-only. A
// KEEP_APART image, one on another plane than its parent and an override image stay outside the group, drawn as above.
// Returns true when one of the drawn images is an OVERRIDE (Ap::overrides): the caller then draws the parent as nothing.
bool Assembler::imagesOn(uint32_t key, const RApp& parent, std::vector<RApp>& out, std::vector<RApp>* grouped) {
    auto it = imgsOn_.find(key);
    if (it == imgsOn_.end()) return false;
    bool overridden = false;
    for (size_t idx : it->second) {
        const ImgRec& im = imgs_[idx];
        if (hiddenFromClient(im.appid)) continue;
        RApp ra;
        if (imageApp(im, ra)) {
            ra.hitAtom = parent.hitAtom;
            ra.borrowsHit = true;
            ra.plane = resolvePlane(ra.plane, resolvePlane(parent.plane, 0));
            if (ra.layer < 0) ra.layer = kFlyLayer;
            const Ap* a = ap(im.appid);
            const bool overrides = a && a->overrides() && g_sceneMutation != 24;
            const bool joins = grouped && parent.keepTogether && parent.relay.empty() && !overrides && !ra.keepApart &&
                               ra.plane == resolvePlane(parent.plane, 0);
            if (joins) {
                RApp g = ra;
                g.hitAtom = -1;
                grouped->push_back(std::move(g));
                ra.hitOnly = true;
            }
            inheritOverlayColor(ra, parent.hasTint, parent.tr, parent.tg, parent.tb, parent.alpha);
            out.push_back(std::move(ra));
            if (overrides) overridden = true;
        }
        std::set<uint32_t> visited;
        visChildren(im.visKids, visited, out, nullptr);
    }
    return overridden;
}

// Animation sampling: the animate() chains and the glide offset. The chain and glide start instants are stamped by
// the model at ingest; here they are only sampled at clockNs_. One step's policy: a hard switch of discrete fields at
// the raw midpoint, numeric fields eased, rigid matrix interpolation, the overlay stack not animated.

namespace {

double lerpF(double a, double b, double t) { return a + (b - a) * t; }
int lerpInt(int a, int b, double t) { return int(gomath::round(lerpF(double(a), double(b), t))); }

float bitsToFloat(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

int64_t stepDurNs(const model::Atom::AnimStep& st, int64_t tickNs) {
    return int64_t(double(bitsToFloat(st.timeBits)) * double(tickNs));
}

// The stock chain evaluator, for any target, over whatever value V it composes. The chain is pruned of finished
// sequences, then the sequences are composed in order into `out`: the first is eased absolutely From -> To; a parallel
// one adds its own To - From; a head eases from what the sequences before it give (snapping to its From when that
// differs from the previous sequence's end). A sequence stops advancing when a later head starts.
// get(appid) is an appearance's value; lerp(from, to, raw, f) one step at raw fraction `raw`, eased `f`; add(out, base,
// from, to, raw, f) adds lerp(from, to) - base to `out`. False when nothing is left; `lastTo` is the last sequence's end.
template <class V, class Get, class Lerp, class Add>
bool composeChain(const model::Atom::Chain& ch, int64_t now, Get&& get, Lerp&& lerp, Add&& add, V& out,
                  uint32_t& lastTo) {
    auto stepNs = [&](size_t i) { return stepDurNs(ch.steps[i], ch.tickNs); };
    const int64_t eps = ch.tickNs / 1000;  // the evaluator's 0.001-tick tolerance
    std::vector<model::AnimSeq> seqs = model::animSequences(ch);
    std::vector<bool> alive(seqs.size(), true);
    auto nextAlive = [&](size_t i) {
        for (size_t j = i + 1; j < seqs.size(); j++)
            if (alive[j]) return j;
        return seqs.size();
    };
    auto nextHead = [&](size_t i) {
        for (size_t j = i + 1; j < seqs.size(); j++)
            if (alive[j] && seqs[j].head) return j;
        return seqs.size();
    };
    // A sequence's opening From; a head that starts with the sequence before it takes that one's.
    std::vector<uint32_t> headFrom(seqs.size());
    for (size_t i = 0; i < seqs.size(); i++) headFrom[i] = ch.steps[seqs[i].first].from;
    // The first later head that has started decides; without one, the sequence's own end does. A looping sequence a head
    // has started over plays out the pass it is in. Otherwise it is done once the head has run its first step without a
    // head after it starting first.
    auto finished = [&](size_t i) {
        model::AnimSeq& q = seqs[i];
        for (size_t j = nextHead(i); j < seqs.size(); j = nextHead(j)) {
            const model::AnimSeq& h = seqs[j];
            if (h.startNs > now) continue;
            if (q.endNs >= 0 && q.endNs <= h.startNs) return true;
            if (h.startNs - eps <= q.startNs) {
                headFrom[j] = headFrom[i];
                return true;
            }
            if (q.endNs < 0 || q.endNs != q.startNs + q.durNs) {
                if (q.durNs > 0)
                    q.startNs = h.startNs - (h.startNs - q.startNs) % q.durNs;
                else
                    q.startNs = h.startNs;
                q.endNs = q.startNs + q.durNs;
            }
            if (j == nextAlive(i) && q.first == q.last && headFrom[i] == ch.steps[q.first].to) return true;
            size_t after = nextHead(j);
            return (after < seqs.size() ? seqs[after].startNs : now) >= h.startNs + stepNs(h.first);
        }
        return q.endNs >= 0 && now >= q.endNs;
    };
    // A finished sequence goes when nothing or a head follows it; one followed by a parallel stays, held
    // at its end, until that one goes.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < seqs.size(); i++) {
            if (!alive[i] || !finished(i)) continue;
            size_t n = nextAlive(i);
            if (n < seqs.size() && seqs[n].parallel) continue;
            alive[i] = false;
            changed = true;
        }
    }
    bool first = true;
    uint32_t prevEnd = 0;
    for (size_t i = 0; i < seqs.size(); i++) {
        if (!alive[i]) continue;
        const model::AnimSeq& q = seqs[i];
        if (!first && prevEnd != headFrom[i] && (q.head || q.parallel)) {
            V a = get(prevEnd), b = get(headFrom[i]);
            if (q.parallel)
                add(out, a, a, b, 1.0, 1.0);
            else
                out = lerp(a, b, 1.0, 1.0);
        }
        // The step in flight and its fraction, the sequence held where the next head started.
        int64_t t = now;
        size_t nh = nextHead(i);
        if (nh < seqs.size() && seqs[nh].startNs < t) t = seqs[nh].startNs;
        size_t cur = q.first;
        double f = 0;
        if (q.startNs <= now) {
            // Past its end with no later head the sequence rests on its last To; otherwise the clock wraps by its length.
            if (q.durNs <= 0 || (nh == seqs.size() && q.endNs >= 0 && t >= q.endNs)) {
                cur = q.last;
                f = 1;
            } else {
                int64_t e = (t - q.startNs) % q.durNs;
                while (cur < q.last && stepNs(cur) <= e) {
                    e -= stepNs(cur);
                    cur++;
                }
                int64_t d = stepNs(cur);
                f = d > 0 ? std::min(1.0, double(e) / double(d)) : 1.0;
            }
        }
        const model::Atom::AnimStep& st = ch.steps[cur];
        const double raw = f;
        if (st.easing) f = ease(f, st.easing);
        // The head step eases from the sequence's last To once the whole sequence has played out by the next head
        // (or now), else from its own From (the first sequence, a parallel) or from `out` (a later head).
        const int64_t tref = nh < seqs.size() ? seqs[nh].startNs : now;
        const bool done = cur == q.first && q.startNs + q.durNs < tref;
        if (q.parallel && !first) {
            V base = get(headFrom[i]);
            V from = done ? get(ch.steps[q.last].to) : cur == q.first ? base : get(st.from);
            add(out, base, from, get(st.to), raw, f);
        } else {
            V from = done ? get(ch.steps[q.last].to) : cur != q.first ? get(st.from) : first ? get(headFrom[i]) : out;
            out = lerp(from, get(st.to), raw, f);
        }
        first = false;
        prevEnd = ch.steps[q.last].to;
    }
    lastTo = prevEnd;
    return !first;
}

Mat toMat(const std::array<float, 6>& x) {
    return Mat{double(x[0]), double(x[1]), double(x[2]), double(x[3]), double(x[4]), double(x[5])};
}

// One step of an appearance tween at raw fraction `raw`, eased `frac`.
RApp lerpApp(const RApp& from, const RApp& to, double raw, double frac) {
    RApp result = raw >= 0.5 ? to : from;  // discrete fields switch at the RAW midpoint
    result.px = lerpInt(from.px, to.px, frac);
    result.py = lerpInt(from.py, to.py, frac);
    result.alpha = lerpInt(from.alpha, to.alpha, frac);
    // Two flat tints lerp per channel. Any other shape mismatch switches at the eased fraction (t >= 0.5 goes to `to`).
    if (from.hasTint && to.hasTint) {
        result.hasTint = true;
        result.tr = uint8_t(lerpInt(from.tr, to.tr, frac));
        result.tg = uint8_t(lerpInt(from.tg, to.tg, frac));
        result.tb = uint8_t(lerpInt(from.tb, to.tb, frac));
    } else {
        const RApp& side = frac >= 0.5 ? to : from;
        result.hasTint = side.hasTint;
        result.tr = side.tr;
        result.tg = side.tg;
        result.tb = side.tb;
    }
    // A colour matrix and a blend mode are not interpolated; they follow the discrete switch at the midpoint.
    // transform: a side without one tweens from/to IDENTITY; rigid interpolation via lerpMatrix.
    if (from.hasXform || to.hasXform) {
        Mat fm, tm;
        if (from.hasXform) fm = toMat(from.xf);
        if (to.hasXform) tm = toMat(to.xf);
        Mat m;
        if (lerpMatrix(fm, tm, frac, m)) {
            result.xf = {float(m.a), float(m.b), float(m.c), float(m.d), float(m.e), float(m.f)};
            result.hasXform = true;
        }
    }
    return result;
}

// Adds a parallel step's lerp(from, to) - base to `out`, as the stock evaluator does: offsets, alpha and tint channels
// add (clamped), the transform composes with inverse(base) then the step's, and a discrete field the step changes is
// the step's To.
void addApp(RApp& out, const RApp& base, const RApp& from, const RApp& to, double raw, double frac) {
    RApp t = lerpApp(from, to, raw, frac);
    out.px += t.px - base.px;
    out.py += t.py - base.py;
    out.alpha = std::clamp(out.alpha + t.alpha - base.alpha, 0, 255);
    if (t.hasTint || base.hasTint) {
        auto c = [](const RApp& r, uint8_t RApp::* ch) { return r.hasTint ? int(r.*ch) : 255; };
        for (uint8_t RApp::* ch : {&RApp::tr, &RApp::tg, &RApp::tb})
            out.*ch = uint8_t(std::clamp(c(out, ch) + c(t, ch) - c(base, ch), 0, 255));
        out.hasTint = true;
    }
    if (t.hasXform || base.hasXform) {
        Mat inv, b = base.hasXform ? toMat(base.xf) : Mat{}, o = out.hasXform ? toMat(out.xf) : Mat{};
        if (invertMat(b, inv)) {
            Mat m = matMul(matMul(t.hasXform ? toMat(t.xf) : Mat{}, inv), o);  // out, then inverse(base), then the step
            out.xf = {float(m.a), float(m.b), float(m.c), float(m.d), float(m.e), float(m.f)};
            out.hasXform = true;
        }
    }
    if (from.icon != to.icon || from.state != to.state || base.icon != to.icon || base.state != to.state) {
        out.icon = to.icon;
        out.state = to.state;
        out.frame = to.frame;
    }
    if (from.blend != to.blend || base.blend != to.blend) out.blend = to.blend;
    if (from.cmat != to.cmat || base.cmat != to.cmat) out.cmat = to.cmat;
}

}  // namespace

// The appearance an animate() chain gives at the render clock. Nothing left playing is the settled appearance; else the
// chain's value, shifted by what the settled appearance has moved from the chain's end (a var set during a tween
// carries the tween with it). The overlay stack is the settled one's throughout.
bool Assembler::tweenChain(const model::Atom::Chain& ch, uint32_t settled, RApp& out) {
    std::map<uint32_t, std::pair<bool, RApp>> built;
    auto build = [&](uint32_t appid) -> const std::pair<bool, RApp>& {
        auto [it, fresh] = built.try_emplace(appid);
        if (fresh) {
            std::set<uint32_t> v;
            it->second.first = buildApp(appid, v, it->second.second);
        }
        return it->second;
    };
    const std::pair<bool, RApp>& live = build(settled);
    if (g_sceneMutation == 22 && clockNs_ < ch.startNs) {  // test-only: Go's rule, the first From as is until the start
        const std::pair<bool, RApp>& f = build(ch.steps[0].from);
        if (f.first) out = f.second;
        return f.first;
    }
    bool missing = false;
    auto get = [&](uint32_t appid) {
        const std::pair<bool, RApp>& b = build(appid);
        if (b.first) return b.second;
        missing = missing || !live.first;
        return live.second;
    };
    RApp res;
    uint32_t lastTo = 0;
    if (!composeChain(ch, clockNs_, get, lerpApp, addApp, res, lastTo) || missing) {
        if (!live.first) return false;
        out = live.second;
        return true;
    }
    if (live.first) {
        if (lastTo != settled && g_sceneMutation != 7) {
            RApp end = get(lastTo);
            addApp(res, end, end, live.second, 1.0, 1.0);
        }
        res.overlays = live.second.overlays;
    }
    out = std::move(res);
    return true;
}

// Tween + icon animation, then the flick() overlay.
bool Assembler::animatedApp(uint32_t id, const model::Atom& m, RApp& out) {
    bool ok;
    if (!m.animating || m.chain.steps.empty()) {
        std::set<uint32_t> v;
        ok = buildApp(m.appid, v, out);
    } else {
        ok = tweenChain(m.chain, m.appid, out);
    }
    if (ok) {
        applyIconAnimation(out, m.iconStartNs);
        flickOverride(id, out);
    }
    return ok;
}

// While a flick() plays, the atom shows the flicked icon_state's own frame sequence once from the
// flick's arrival, then reverts to its real appearance (BYOND: "temporarily replaced ... for the duration of the animation").
// Only icon, state and frame change; position, colour, plane and overlays keep the live appearance. Without it a door jumps
// straight from closed to open, since its opening state never plays.
void Assembler::flickOverride(uint32_t id, RApp& out) {
    auto it = w_.flicks.find(id);
    if (it == w_.flicks.end()) return;
    const model::World::Flick& f = it->second;
    if (!f.haveRes || f.iconState.empty() || f.startNs == 0 || f.iconResIndex >= w_.resources.size()) return;
    const Icon* ic = res_.iconResolved(w_.resources[f.iconResIndex].first, w_.resources);
    if (!ic) return;
    int si = ic->stateIndex(f.iconState);
    if (si < 0) return;
    const IconState& st = ic->states[size_t(si)];
    std::vector<std::pair<int, double>> steps;  // frame, delay (ds); the same sequence frameFor plays, ONCE
    auto delay = [&](int64_t i) { return (i >= 0 && size_t(i) < st.delays.size()) ? st.delays[size_t(i)] : 1.0; };
    for (int64_t i = 0; i < std::max<int64_t>(1, st.frames); i++) steps.push_back({int(i), delay(i)});
    if (st.rewind && st.frames > 2)
        for (int64_t i = st.frames - 2; i >= 1; i--) steps.push_back({int(i), delay(i)});
    double total = 0;
    for (auto& s : steps) total += s.second;
    double elapsed = double(clockNs_ - f.startNs) / 1e8;
    if (total <= 0 || elapsed < 0 || elapsed >= total) return;  // not started (clock skew) or already played out
    double acc = 0;
    int frame = steps.back().first;
    for (auto& s : steps) {
        if (elapsed < acc + s.second) {
            frame = s.first;
            break;
        }
        acc += s.second;
    }
    out.icon = ic;
    out.state = f.iconState;
    out.frame = frame;
}

// Glide offset: the armed offset (tiles) * tile size scaled by the fraction left, rounded; 0 once played out.
void Assembler::glideOffset(const model::Atom& m, int tileW, int tileH, int& px, int& py) const {
    px = py = 0;
    if (!m.gliding) return;
    const model::Atom::Glide& g = m.glide;
    double rem = model::glideLeft(g, clockNs_);
    if (rem <= 0) return;
    if (g_sceneMutation == 6) {  // test-only: truncate instead of round
        px = int(g.sx * tileW * rem);
        py = int(g.sy * tileH * rem);
        return;
    }
    px = int(gomath::round(g.sx * tileW * rem));
    py = int(gomath::round(g.sy * tileH * rem));
}

// A chain's From/To appearances carry pixel_x/y as ABSOLUTE values, while the atom's own pixel offsets repeat the settled
// ones, so those must not be added again. While the chain composes, tweenChain has already shifted it by the settled
// appearance minus the chain's end, which takes the end's offsets back out: the atom's own are then added on top, unless
// the settled appearance carries them too.
bool Assembler::animEndpointCarriesPixels(const model::Atom& m) const {
    if (g_sceneMutation == 7) return false;  // test-only: add the A6 copy on top of the endpoint's
    if (!m.animating || m.chain.steps.empty()) return false;
    auto has = [&](uint32_t appid) {
        auto it = w_.appearances.find(appid);
        return it != w_.appearances.end() && it->second.fields.hasPixelOffsets;
    };
    bool composes = false;
    uint32_t lastTo = 0;
    if (g_sceneMutation == 22 && clockNs_ < m.chain.startNs)
        return m.appid == m.chain.steps.back().to && has(m.appid);  // test-only: Go's rule
    auto lerp = [](bool, bool, double, double) { return false; };
    auto add = [](bool&, bool, bool, bool, double, double) {};
    if (composeChain(m.chain, clockNs_, [](uint32_t) { return false; }, lerp, add, composes, lastTo))
        return has(m.appid);
    return m.appid == m.chain.steps.back().to && has(m.chain.steps.back().to);
}

// client.pixel_x + pixel_w and pixel_y + pixel_z at the render clock, in world pixels: the last WorldState value plus
// what the animate(client) chain composes (composeChain) minus the last step's To, so a direct set during a tween
// shifts the tween instead of ending it.
void Assembler::clientViewOffset(int& x, int& y) {
    x = int(w_.viewPixelX) + int(w_.viewPixelW);
    y = int(w_.viewPixelY) + int(w_.viewPixelZ);
    const model::Atom::Chain& ch = w_.clientChain;
    if (ch.steps.empty()) return;
    struct Px {
        int v[4] = {0, 0, 0, 0};
    };
    auto pix = [&](uint32_t appid) {
        Px p;
        if (const Ap* a = ap(appid)) {
            p.v[0] = a->pixelX;
            p.v[1] = a->pixelY;
            p.v[2] = a->pixelW;
            p.v[3] = a->pixelZ;
        }
        return p;
    };
    auto lerp = [](const Px& from, const Px& to, double, double f) {
        Px r;
        for (int k = 0; k < 4; k++)
            r.v[k] = from.v[k] == to.v[k] ? to.v[k] : int(float(to.v[k] - from.v[k]) * float(f)) + from.v[k];
        return r;
    };
    auto add = [&](Px& out, const Px& base, const Px& from, const Px& to, double raw, double f) {
        Px t = lerp(from, to, raw, f);
        for (int k = 0; k < 4; k++) out.v[k] += t.v[k] - base.v[k];
    };
    Px out;
    uint32_t lastTo = 0;
    if (!composeChain(ch, clockNs_, pix, lerp, add, out, lastTo)) return;
    Px end = pix(lastTo);
    x += out.v[0] - end.v[0] + out.v[2] - end.v[2];
    y += out.v[1] - end.v[1] + out.v[3] - end.v[3];
}

// Tiles beyond the view, on every side, that the current shift brings into the map control.
int Assembler::ringFor(int tw, int th) const {
    if (tw <= 0 || th <= 0) return 0;
    return std::max((std::abs(shiftX_) + tw - 1) / tw, (std::abs(shiftY_) + th - 1) / th);
}

// ---- grid --------------------------------------------------------------------------------------

static bool opaqueAppearance(const Ap* a) { return a && a->opaque(); }

// Outputs the view rect (origin/extent); vx0/vy0 are unused padding for symmetry.
bool Assembler::buildGrid(int& originX, int& originY, int& winW, int& winH, int& extentW, int& extentH, int& vx0,
                          int& vy0) {
    (void)vx0;
    (void)vy0;
    // The cell grid comes from the model (model::Session::cells).
    model::CellGrid cg = s_.cells();
    if (cg.err) {
        fail("no bounds (0x3e) yet / no map grid yet");
        return false;
    }
    winW = cg.w;
    winH = cg.h;
    originX = cg.originX;
    originY = cg.originY;
    const std::vector<uint32_t>& cells = cg.appids;
    if (size_t(winW) * size_t(winH) != cells.size()) {
        fail("cells section: w*h != len(appids)");
        return false;
    }

    maxX_ = w_.bounds.maxX;
    maxY_ = w_.bounds.maxY;
    if (maxX_ <= 0 || maxY_ <= 0) {
        fail("render: empty grid");
        return false;
    }
    gridX0_ = originX;
    gridY0_ = originY;
    gridW_ = winW;
    gridH_ = winH;
    grid_.assign(size_t(winW) * size_t(winH), Tile{});
    auto tileAt = [&](int x, int y) -> Tile* { return const_cast<Tile*>(tileIn(x, y)); };

    // Visible rect: the eye, or the map centre when there is none.
    int eyeX = w_.haveEye ? w_.eyeX : (maxX_ + 1) / 2;
    int eyeY = w_.haveEye ? w_.eyeY : (maxY_ + 1) / 2;
    if (w_.bounds.viewW > 0 && w_.bounds.viewH > 0) {
        extentW = w_.bounds.viewW;
        extentH = w_.bounds.viewH;
    } else if (w_.bounds.view > 0) {
        extentW = extentH = 2 * w_.bounds.view + 1;
    } else {
        extentW = extentH = 2 * kDefaultViewRange + 1;
    }
    if (maxX_ > 0 && extentW > maxX_) extentW = maxX_;
    if (maxY_ > 0 && extentH > maxY_) extentH = maxY_;
    int viewOriginX = eyeX - extentW / 2, viewOriginY = eyeY - extentH / 2;
    if (maxX_ > 0 && extentW >= maxX_) viewOriginX = 1;
    if (maxY_ > 0 && extentH >= maxY_) viewOriginY = 1;

    // sight: skipLOS when the mob sees turfs, mobs AND objs; turfs always visible when SEE_TURFS.
    const uint32_t all = kSightMobs | kSightObjs | kSightTurfs;
    bool skipLOS = w_.haveSight && (w_.sight & all) == all;
    bool turfsAlwaysVisible = skipLOS || (w_.haveSight && (w_.sight & kSightTurfs) != 0);

    // A shifted frame (eye scroll, client.pixel_*) keeps extra LOS rings alive: the tiles the shift reveals beyond
    // the strict view edge.
    model::eyeScrollOffset(w_, clockNs_, scrollX_, scrollY_);
    int viewX, viewY;
    clientViewOffset(viewX, viewY);
    shiftX_ = scrollX_ - viewX;  // the camera moves by the offset, so the map moves the other way (y flips)
    shiftY_ = scrollY_ + viewY;
    // A movable on another z is not on this map. DD sends one there for a flick() anywhere in the world (the record, its
    // position and the flick, retracted the next tick); stock DreamSeeker draws it only on its own z.
    auto offZ = [&](const model::Atom& m) { return cg.haveZ && m.z + 1 != cg.viewZ; };
    const int losMargin = ringFor(model::iconSizeOf(w_), model::iconSizeOf(w_));
    bool haveVisible = false;
    CellSet visible;
    if (!skipLOS && w_.haveEye && winW > 0 && winH > 0) {
        int ex = eyeX, ey = eyeY;
        if (!(ex < originX || ex >= originX + winW || ey < originY || ey >= originY + winH)) {
            int rng = std::max(extentW / 2, extentH / 2) + losMargin;
            bool allKnown = true;
            for (int y = ey - rng; y <= ey + rng && allKnown; y++)
                for (int x = ex - rng; x <= ex + rng; x++) {
                    int lx = x - originX, ly = y - originY;
                    if (lx < 0 || ly < 0 || lx >= winW || ly >= winH) continue;
                    if (!w_.appearances.count(cells[size_t(ly) * size_t(winW) + size_t(lx)])) {
                        allKnown = false;
                        break;
                    }
                }
            if (allKnown) {
                std::set<std::pair<int, int>> blockers;
                for (auto& [id, m] : w_.atoms)
                    if (!offZ(m) && opaqueAppearance(ap(m.appid))) blockers.insert({m.x, m.y});
                auto opaque = [&](int x, int y) {
                    int lx = x - originX, ly = y - originY;
                    if (lx < 0 || ly < 0 || lx >= winW || ly >= winH) return true;  // an edge is an occluder
                    if (blockers.count({x, y})) return true;
                    const Ap* a = ap(cells[size_t(ly) * size_t(winW) + size_t(lx)]);
                    return a ? a->opaque() : false;
                };
                visible = visibleCells(ex, ey, rng, opaque);
                haveVisible = true;
            }
        }
    }
    auto isVisible = [&](int x, int y) { return visible.count({x, y}) != 0; };

    for (int ly = 0; ly < winH; ly++)
        for (int lx = 0; lx < winW; lx++) {
            uint32_t appid = cells[size_t(ly) * size_t(winW) + size_t(lx)];
            if (hiddenFromClient(appid)) continue;
            std::set<uint32_t> visited;
            RApp ra;
            if (!buildApp(appid, visited, ra)) {
                // an unbuildable cell that is also out of sight is remembered as hidden (blackness covers it)
                if (haveVisible && !isVisible(originX + lx, originY + ly)) hidden_.insert({originX + lx, originY + ly});
                continue;
            }
            // A turf's multi-frame icon animates in stock DreamSeeker 516, including an animated GIF on a turf.
            if (g_sceneMutation != 22)
                applyIconAnimation(ra, tileIconStart(originX + lx - 1, originY + ly - 1, cg.viewZ - 1));
            ra.hitAtom = newHitAtom(appid, kAtomTurf, 0, false);
            ra.holdsCell = true;
            Tile* t = tileAt(originX + lx, originY + ly);
            if (!t) continue;
            *t = Tile{std::move(ra)};
            // The turf's vis_contents (vg's gas overlays) draw at its cell, each on its own plane and layer.
            auto tv = w_.turfVis.find({uint16_t(originX + lx - 1), uint16_t(originY + ly - 1), uint16_t(cg.viewZ - 1)});
            if (tv == w_.turfVis.end()) continue;
            std::vector<RApp> kids;
            std::set<uint32_t> visitedV;
            visChildren(tv->second, visitedV, kids, nullptr);
            for (auto& k : kids) t->push_back(std::move(k));
        }
    if (haveVisible && !turfsAlwaysVisible) {
        for (int ly = 0; ly < winH; ly++)
            for (int lx = 0; lx < winW; lx++) {
                int x = originX + lx, y = originY + ly;
                if (isVisible(x, y)) continue;
                hidden_.insert({x, y});
                if (Tile* t = tileAt(x, y)) t->clear();
            }
    }

    // Movables in ascending id order. The glide offset uses the tile size as it stands here, before any movable is
    // appended (the most common icon size over the turf tiles), not the final size.
    int glideTW = 32, glideTH = 32;
    tileSize(glideTW, glideTH);
    collectImages();
    for (auto& [id, m] : w_.atoms) {
        if (!m.hasPos || offZ(m)) continue;
        if (m.x < originX || m.x >= originX + winW || m.y < originY || m.y >= originY + winH) continue;
        if (haveVisible && !isVisible(m.x, m.y)) continue;
        if (hiddenFromClient(m.appid)) continue;
        RApp ra;
        if (!animatedApp(id, m, ra)) continue;  // no appearance flyweight for this atom (or its chain's From) yet
        ra.hitAtom = newHitAtom(m.appid, (id & kRegistryA) ? kAtomMob : kAtomObj, id & ~kRegistryA, false);
        ra.dir = m.dir;
        for (auto& o : ra.overlays) applyOverlayDir(o, m.dir);
        markParticles(ra, id);
        // gx,gy = glide offset + the atom's own A6 pixel offsets; folded in so vis_contents children
        // drawn at this atom's tile travel with it. An animate() endpoint already carries the pixel offset
        // absolutely, so the A6 copy is taken back out of the atom's own sprite (not of its children).
        int glx, gly;
        glideOffset(m, glideTW, glideTH, glx, gly);
        int gx = glx + int(m.pixelX) + int(m.pixelW), gy = gly + int(m.pixelY) + int(m.pixelZ);
        ra.px += gx;
        ra.py += gy;
        if (animEndpointCarriesPixels(m)) {
            ra.px -= int(m.pixelX) + int(m.pixelW);
            ra.py -= int(m.pixelY) + int(m.pixelZ);
        }
        Tile* t = tileAt(m.x, m.y);
        if (!t) continue;
        std::vector<RApp> attached;
        std::set<uint32_t> visitedV;
        visContents(id, visitedV, attached, &ra);
        // client.images attached to this atom draw at its tile, with it. An OVERRIDE image replaces the atom's own appearance
        // for this viewer. Stock DreamSeeker 516: the parent's icon, its overlays and its maptext are neither drawn nor clicked
        // (the parent's opaque half answers "floor"), and the image is drawn and clicked in their place, naming the PARENT in
        // the image's frame. An override image that draws nothing (state "blank", or alpha 0, which still takes clicks)
        // leaves the atom invisible. The atom's vis_contents still draw and still answer as themselves. The atom stays in the
        // scene, drawing nothing, so the hit test still knows where it is.
        // An override image on a TURF is drawn in addition to the turf.
        std::vector<RApp> grouped;
        if (imagesOn(id, ra, attached, &grouped)) {
            ra.icon = nullptr;
            ra.state.clear();
            ra.overlays.clear();
            ra.maptext.clear();
            ra.relay.clear();
            for (RApp& a : attached) a.hitOnly = false;
        } else {
            ra.holdsCell = true;
            ra.groupImages = std::move(grouped);
        }
        t->push_back(std::move(ra));
        for (auto& a : attached) {
            a.px += gx;
            a.py += gy;
            t->push_back(std::move(a));
        }
    }
    // client.images on a TURF (loc kind 0x07) draw only on the viewport's own z. A cell the viewer cannot see draws
    // nothing (in DreamSeeker a LOS-hidden cell is blackness). A hit on one falls through to the turf.
    for (const ImgRec& im : imgs_) {
        if (im.locKind != 0x07) continue;
        if (maxX_ <= 0 || maxY_ <= 0) break;
        int perZ = maxX_ * maxY_, n = int(im.cell);
        int z = n / perZ + 1, rem = n % perZ;
        int x = rem % maxX_ + 1, y = rem / maxX_ + 1;
        if (z != cg.viewZ || hidden_.count({x, y})) continue;
        Tile* t = tileAt(x, y);
        if (!t) continue;
        int turfHit = t->empty() ? -1 : t->front().hitAtom;
        RApp ra;
        if (imageApp(im, ra)) {
            ra.hitAtom = turfHit;
            ra.borrowsHit = true;
            // FLOAT_LAYER is a fixed FLY_LAYER (5) here, whatever stands on the tile: over mobs (4) and
            // objs below 5, under one of layer 5 or more, even one created later. The draw key is a float.
            if (ra.layer < 0) ra.layer = double(std::nextafter(float(kFlyLayer), 0.0f));  // one float step under a tie
            t->push_back(std::move(ra));
        }
        std::vector<RApp> kids;
        std::set<uint32_t> visited;
        visChildren(im.visKids, visited, kids, nullptr);
        for (auto& k : kids) t->push_back(std::move(k));
    }
    st_.images += int(w_.images.size());

    originX = viewOriginX;  // from here on the caller wants the VIEW origin
    originY = viewOriginY;
    return !failed_;
}

// The size of the first icon found in an appearance or its overlays.
static bool firstIconSize(const std::vector<RApp>& as, int& w, int& h) {
    for (const RApp& a : as) {
        if (a.icon) {
            w = a.icon->width;
            h = a.icon->height;
            return true;
        }
        if (firstIconSize(a.overlays, w, h)) return true;
    }
    return false;
}

// The most common icon size over the grid tiles, ties broken toward the first found; 32x32 when there is none.
void Assembler::tileSize(int& tw, int& th) const {
    auto from = firstIconSize;
    std::map<std::pair<int, int>, int> counts;
    std::pair<int, int> first{0, 0};
    for (const Tile& t : grid_) {  // the whole-world order, restricted to the window
        if (t.empty()) continue;
        int w, h;
        if (from(t, w, h)) {
            if (first == std::pair<int, int>{0, 0}) first = {w, h};
            counts[{w, h}]++;
        }
    }
    auto best = first;
    int bestN = 0;
    for (auto& [sz, n] : counts)
        if (n > bestN || (n == bestN && sz == first)) {
            best = sz;
            bestN = n;
        }
    if (bestN == 0) {
        tw = th = 32;
        return;
    }
    tw = best.first;
    th = best.second;
}

// ---- placement -----------------------------------------------------------------------------------

// KEEP_TOGETHER: the appearance and its overlays are composed onto a transparent surface of their own first, each under its
// own blend mode, and that image is drawn as one layer under the appearance's blend mode. So a BLEND_INSET_OVERLAY overlay is
// masked by the appearance's own pixels rather than by whatever lies beneath it on the plane. The parts stay in `out` for the
// mouse (noDraw). Inside the group the parts sort by plane, then layer, ties in placement order (underlays first).
// Left out of the group: a KEEP_APART overlay at any depth (it keeps only the colour it inherited from its own parent), and an
// overlay on another plane than the appearance, which is drawn on that plane and takes the appearance's colour and alpha.
// The appearance's colour, alpha, colour matrix and TRANSFORM apply to the group: the parts are composed untransformed (each
// keeping its own transform) and the whole image is transformed about the appearance's own icon centre.
bool Assembler::placeKeptTogether(const RApp& a, int baseX, int baseY, int tileH, double parentPlane,
                                  double parentLayer, bool hasParent, std::vector<Placed>& out,
                                  const std::array<float, 6>* inheritedXf) {
    // Without a group (a relay, or nothing but overlays that leave it) the parts are placed as they would be without the flag,
    // each carrying the parent's colour and alpha as an overlay normally does.
    auto ordinary = [&] {
        RApp plain = a;
        plain.keepTogether = false;
        for (RApp& ov : plain.overlays)
            if (!ov.keepApart) inheritOverlayColor(ov, a.hasTint, a.tr, a.tg, a.tb, a.alpha);
        placeApp(plain, baseX, baseY, tileH, parentPlane, parentLayer, hasParent, out, inheritedXf);
        const double pl = resolvePlane(a.plane, hasParent ? parentPlane : 0.0);
        const double ly = (hasParent && a.layer < 0) ? parentLayer : a.layer;
        // No group to draw in: the image is a layer of its own, as without the flag.
        for (const RApp& g : a.groupImages) {
            RApp c = g;
            inheritOverlayColor(c, a.hasTint, a.tr, a.tg, a.tb, a.alpha);
            const size_t n0 = out.size();
            placeApp(c, baseX + a.px, baseY - a.py, tileH, pl, ly, true, out, inheritedXf);
            for (size_t k = n0; k < out.size(); k++) out[k].hitVisible = false;
        }
        return true;
    };
    if (!a.relay.empty()) return ordinary();
    const double plane = resolvePlane(a.plane, hasParent ? parentPlane : 0.0);
    const double layer = (hasParent && a.layer < 0) ? parentLayer : a.layer;
    const bool hasX = inheritedXf ? true : a.hasXform;
    const std::array<float, 6> xf = inheritedXf ? *inheritedXf : a.xf;
    static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
    const bool xformed =
        g_sceneMutation != 54 && hasX && xf != kIdentity;  // 54: test-only, each part takes the transform itself

    RApp inner = a;
    inner.keepTogether = false;
    inner.blend = kBlendDefault;
    inner.hasTint = false;
    inner.tr = inner.tg = inner.tb = 255;
    inner.alpha = 255;
    inner.cmat = nullptr;
    inner.overlays.clear();
    std::vector<RApp> apart;  // placed on their own after the group, with the appearance's transform
    bool together = false;
    for (const RApp& ov : a.overlays) {
        if (ov.keepApart)
            apart.push_back(ov);
        else if (g_sceneMutation != 52 &&
                 resolvePlane(ov.plane, plane) != plane) {  // 52: test-only, every overlay joins the group
            apart.push_back(ov);
            inheritOverlayColor(apart.back(), a.hasTint, a.tr, a.tg, a.tb, a.alpha);
        } else {
            inner.overlays.push_back(ov);
            together = true;
        }
    }
    if (!a.groupImages.empty()) together = true;
    if (!together) return ordinary();
    // The composed parts carry no transform of the appearance's: it applies to the image as a whole, below.
    RApp flat = inner;
    if (g_sceneMutation != 54) flat.hasXform = false;
    std::vector<Placed> parts;
    // A KEEP_APART overlay nested inside the group leaves the outermost group.
    const bool outermost = apartSink_ == nullptr;
    std::vector<Placed> sink;
    if (outermost && g_sceneMutation != 53) apartSink_ = &sink;  // 53: test-only, nested KEEP_APART stays in the group
    placeApp(flat, baseX, baseY, tileH, parentPlane, parentLayer, hasParent, parts,
             g_sceneMutation == 54 ? inheritedXf : nullptr);
    // The parent's client.images join the group after its overlays (a tie at one layer goes to the image) and answer no mouse
    // here: their own hit-only entries do.
    for (const RApp& g : a.groupImages) {
        const size_t n0 = parts.size();
        placeApp(g, baseX + a.px, baseY - a.py, tileH, plane, layer, true, parts, nullptr);
        for (size_t k = n0; k < parts.size(); k++) parts[k].hitVisible = false;
    }
    std::vector<Placed>
        hitParts;  // the same parts under the appearance's transform, answering the mouse where they are drawn
    if (xformed && !failed_) {
        std::vector<Placed> discard;
        std::vector<Placed>* saved = apartSink_;
        apartSink_ = &discard;
        placeApp(inner, baseX, baseY, tileH, parentPlane, parentLayer, hasParent, hitParts, inheritedXf);
        apartSink_ = saved;
    }
    if (outermost) apartSink_ = nullptr;
    if (failed_) return true;
    std::vector<size_t> drawn;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (size_t i = 0; i < parts.size(); i++) {
        const Placed& p = parts[i];
        if (!p.layer.relay.empty()) return ordinary();
        if (p.footprint || p.noDraw || !p.layer.sprite) continue;
        drawn.push_back(i);
        x0 = std::min(x0, p.layer.x);
        y0 = std::min(y0, p.layer.y);
        x1 = std::max(x1, p.layer.x + p.layer.sprite->w);
        y1 = std::max(y1, p.layer.y + p.layer.sprite->h);
    }
    std::stable_sort(drawn.begin(), drawn.end(), [&](size_t i, size_t j) {
        const Placed &p = parts[i], &q = parts[j];
        if (p.plane != q.plane) return p.plane < q.plane;
        if (p.sortLayer != q.sortLayer) return p.sortLayer < q.sortLayer;
        return g_sceneMutation == 51 && p.layer.blend != q.layer.blend &&
               q.layer.blend == kBlendAdd;  // 51: test-only, ADD last
    });
    Placed group;
    if (!drawn.empty()) {
        auto sp = std::make_shared<Sprite>();
        int ox = x0, oy = y0;
        if (xformed) {
            // Pad the canvas so the appearance's own icon centre is its centre, then transform about it.
            bool own = false;
            SpritePtr psp = sprite(a, own);
            if (failed_) return true;
            const int pw = own ? psp->w : tileH, ph = own ? psp->h : tileH;
            const int cx2 = 2 * (baseX + a.px) + pw, cy2 = 2 * (baseY + (tileH - ph) - a.py) + ph;
            int hx2 = std::max(cx2 - 2 * x0, 2 * x1 - cx2), hy2 = std::max(cy2 - 2 * y0, 2 * y1 - cy2);
            if (((cx2 - hx2) & 1) != 0) hx2++;
            if (((cy2 - hy2) & 1) != 0) hy2++;
            ox = (cx2 - hx2) / 2;
            oy = (cy2 - hy2) / 2;
            sp->w = hx2;
            sp->h = hy2;
        } else {
            sp->w = x1 - x0;
            sp->h = y1 - y0;
        }
        sp->px.assign(size_t(sp->w) * size_t(sp->h) * 4, 0);
        for (size_t i : drawn) {
            DrawLayer l = parts[i].layer;
            l.x -= ox;
            l.y -= oy;
            blendLayerOver(sp->px.data(), sp->w, sp->h, l);
        }
        SpritePtr img = sp;
        int gx = ox, gy = oy;
        if (xformed) {
            img = applyTransform(*sp, dmMatrixToImage(xf), Sampling::Bilinear);
            gx = ox + int(gomath::round(double(sp->w - img->w) / 2));
            gy = oy + int(gomath::round(double(sp->h - img->h) / 2));
        }
        group.layer.sprite = img;
        group.layer.x = gx;
        group.layer.y = gy;
        group.layer.blend = a.blend;
        group.layer.hasTint = a.hasTint;
        group.layer.tintR = a.tr;
        group.layer.tintG = a.tg;
        group.layer.tintB = a.tb;
        group.layer.alpha = a.alpha;
        group.layer.cmat = a.cmat;
        group.plane = plane;
        group.sortLayer = layer;
        group.atom = curAtom_;
        group.frame = curFrame_;
        group.hitVisible = false;
    }
    for (Placed& p : xformed ? hitParts : parts) {
        p.noDraw = true;
        p.seq = int(out.size());
        out.push_back(std::move(p));
    }
    if (group.layer.sprite) {
        group.seq = int(out.size());
        out.push_back(std::move(group));
    }
    // The apart parts nested in the group, then the top-level ones (into the outer group's sink when this one is nested).
    for (Placed& p : sink) {
        p.seq = int(out.size());
        out.push_back(std::move(p));
    }
    std::vector<Placed>* dest = apartSink_ ? apartSink_ : &out;
    for (const RApp& ov : apart) {
        std::vector<Placed>* saved = apartSink_;
        apartSink_ = nullptr;
        placeApp(ov, baseX + a.px, baseY - a.py, tileH, plane, layer, true, *dest, hasX ? &xf : nullptr);
        apartSink_ = saved;
        if (failed_) return true;
    }
    return true;
}

// Places an appearance (relays and maptext included). atom.transform is applied to the icon's own pixels about its own
// centre BEFORE the tile-anchor placement; the anchor is computed from the native (pre-transform) size and the grown or
// shrunk image is re-centred on it (as stock DreamSeeker does). `inheritedXf` is the parent's transform, copied over
// every overlay's own, identity included.
void Assembler::placeApp(const RApp& a, int baseX, int baseY, int tileH, double parentPlane, double parentLayer,
                         bool hasParent, std::vector<Placed>& out, const std::array<float, 6>* inheritedXf) {
    if (apartSink_ && a.keepApart && hasParent) {
        std::vector<Placed>* sink = apartSink_;
        apartSink_ = nullptr;
        placeApp(a, baseX, baseY, tileH, parentPlane, parentLayer, hasParent, *sink, inheritedXf);
        apartSink_ = sink;
        return;
    }
    if (a.keepTogether && g_sceneMutation != 50 &&
        placeKeptTogether(a, baseX, baseY, tileH, parentPlane, parentLayer, hasParent, out, inheritedXf))
        return;
    double plane = a.plane, layer = a.layer;
    double base = hasParent ? parentPlane : 0.0;
    plane = resolvePlane(plane, base);
    if (hasParent && layer < 0) layer = parentLayer;  // a FLOAT_LAYER overlay sorts with its parent
    // Test-only hook: an overlay keeps its own transform instead of taking its parent's.
    const bool hasX = (inheritedXf && g_sceneMutation != 8) ? true : a.hasXform;
    const std::array<float, 6> xf = (inheritedXf && g_sceneMutation != 8) ? *inheritedXf : a.xf;
    static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
    if (!hasParent && !a.target.empty() && a.target[0] == '*') return;  // drawn only where it is relayed
    if (!hasParent) {
        // The atom's icon frame (hittest.h HitFrame): its own icon, anchored exactly as the untransformed sprite below, plus
        // its transform. Its overlays and underlays are placed with this same frame, since stock DreamSeeker names a click on
        // any of them in the atom's frame.
        int si = 0;
        bool own = drawableState(a, si);
        HitFrame fr;
        fr.w = own ? a.icon->width : tileH;
        fr.h = own ? a.icon->height : tileH;
        fr.x = baseX + a.px;
        fr.y = baseY + (tileH - fr.h) - a.py;
        if (hasX && xf != kIdentity) {
            fr.hasXf = true;
            fr.m = dmMatrixToImage(xf);
        }
        curFrame_ = fr;
    }

    // UNDERLAYS are placed ahead of this appearance's own icon and the overlays after it, so at an
    // equal (or FLOAT_) layer the stable sort keeps an underlay beneath its parent.
    for (const RApp& ov : a.overlays) {
        if (!ov.underlay) continue;
        placeApp(ov, baseX + a.px, baseY - a.py, tileH, plane, layer, true, out, hasX ? &xf : nullptr);
        if (failed_) return;
    }
    // A relay of an atom's render_target draws that atom (placeRelayed). Any other RELAY draws a slate, not an icon, so it
    // is placed even though it has no icon at all. Its anchor is the tile's BOTTOM-left with the pixel offsets applied; the slate's bottom edge lands on bottomY.
    auto tgt = a.relay.empty() ? targets_.end() : targets_.find(a.relay);
    if (tgt != targets_.end() && relayDepth_ < 4) {
        placeRelayed(a, tgt->second, baseX, baseY, tileH, plane, layer, out);
        if (failed_) return;
    } else if (!a.relay.empty()) {
        Placed p;
        p.layer.relay = a.relay;
        p.layer.x = baseX + a.px;
        p.layer.relayBottomY = baseY + tileH - a.py;
        setFx(p.layer, a);
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.atom = curAtom_;
        p.hitVisible = false;
        out.push_back(std::move(p));
    }
    bool drawable;
    SpritePtr sp = sprite(a, drawable);
    if (failed_) return;
    if (drawable) {
        SpritePtr img = sp;
        if (hasX && xf != kIdentity) img = applyTransform(*sp, dmMatrixToImage(xf), Sampling::Bilinear);
        int dxNative = baseX + a.px;
        int dyNative = baseY + (tileH - sp->h) - a.py;  // bottom-anchored; +pixel_y is north = -Y
        Placed p;
        p.layer.sprite = img;
        if (g_sceneMutation == 4) {  // test-only: anchor the transformed image by its own corner, not its centre
            p.layer.x = dxNative;
            p.layer.y = dyNative;
        } else {
            p.layer.x = dxNative + int(gomath::round(double(sp->w - img->w) / 2));
            p.layer.y = dyNative + int(gomath::round(double(sp->h - img->h) / 2));
        }
        setFx(p.layer, a);
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.atom = curAtom_;
        p.frame = curFrame_;
        p.own = !hasParent;
        out.push_back(std::move(p));
    } else if (!hasParent && a.holdsCell && hit_ && curAtom_ >= 0 && a.relay.empty() && curFrame_.w > 0) {
        // Nothing to draw: the atom still holds its icon frame for the right-click stack.
        SpritePtr& blank = blankCache_[{curFrame_.w, curFrame_.h}];
        if (!blank) {
            auto b = std::make_shared<Sprite>();
            b->w = curFrame_.w;
            b->h = curFrame_.h;
            b->px.assign(size_t(b->w) * size_t(b->h) * 4, 0);
            blank = b;
        }
        Placed p;
        p.layer.sprite = blank;
        p.layer.x = curFrame_.x;
        p.layer.y = curFrame_.y;
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.atom = curAtom_;
        p.frame = curFrame_;
        p.footprint = true;
        out.push_back(std::move(p));
    }
    // An atom's particles draw right after its own icon, in its slot of the plane/layer sort.
    if (!hasParent && a.hasParticles && g_sceneMutation != 40) {
        int iw = drawable ? sp->w : tileH, ih = drawable ? sp->h : tileH;
        placeParticles(a, baseX + a.px, baseY + (tileH - ih) - a.py, iw, ih, plane, layer, out);
        if (failed_) return;
    }
    // MAPTEXT: in front of this appearance's own icon at the same plane and layer (it sorts with the atom,
    // and its higher seq puts it on top). The box is anchored bottom-left like a sprite, moved +maptext_x right and +maptext_y
    // up; a 0 size is the tile. Only the appearance's alpha reaches it (a runechat fades by animating
    // alpha). The text inside the box: render/maptextpaint.h. For the mouse the box is Placed::maptext: its ink never answers,
    // only mouse_opacity 2's rectangle does.
    if (!a.maptext.empty()) {
        int bw = a.mtW > 0 ? a.mtW : tileH, bh = a.mtH > 0 ? a.mtH : tileH;
        if (SpritePtr mt = paintMaptext(a.maptext, mapStyle_, bw, bh)) {
            Placed p;
            p.layer.sprite = mt;
            p.layer.x = baseX + a.px + a.mtX;
            p.layer.y = baseY + tileH - bh - a.py - a.mtY;
            p.layer.alpha = a.alpha;
            p.plane = plane;
            p.sortLayer = layer;
            p.seq = int(out.size());
            p.atom = curAtom_;
            p.frame = curFrame_;
            p.maptext = g_sceneMutation != 25;
            out.push_back(std::move(p));
        }
    }
    for (const RApp& ov : a.overlays) {
        if (ov.underlay) continue;
        placeApp(ov, baseX + a.px, baseY - a.py, tileH, plane, layer, true, out, hasX ? &xf : nullptr);
        if (failed_) return;
    }
}

// A relay of an atom render target: the source atom's image (its icon, overlays and underlays, in their own order) drawn
// at the relay's tile with its pixel offsets, as one item of the relay's plane and layer. The relay's colour and alpha
// multiply into every layer, and its blend mode replaces theirs.
void Assembler::placeRelayed(const RApp& relay, const Target& src, int baseX, int baseY, int tileH, double plane,
                             double layer, std::vector<Placed>& out) {
    RApp s = src.app;
    s.target.clear();
    if (relay.hasTint || relay.alpha != 255)
        inheritOverlayColor(s, relay.hasTint, relay.tr, relay.tg, relay.tb, relay.alpha);
    std::vector<Placed> img;
    const int atom = curAtom_;
    const HitFrame frame = curFrame_;
    relayDepth_++;
    placeApp(s, baseX + relay.px, baseY - relay.py, tileH, 0, 0, false, img);
    relayDepth_--;
    curAtom_ = atom;
    curFrame_ = frame;
    auto addRank = [](const Placed& p) { return p.layer.blend == kBlendAdd ? 1 : 0; };
    std::stable_sort(img.begin(), img.end(), [&](const Placed& a, const Placed& b) {
        if (a.plane != b.plane) return a.plane < b.plane;
        if (a.sortLayer != b.sortLayer) return a.sortLayer < b.sortLayer;
        return addRank(a) < addRank(b);
    });
    // For the mouse a relay is a client.image of its parent, named in its own frame. With PASS_MOUSE its pixels answer
    // for the source atom instead, in the source's frame, by their own alpha whatever either mouse_opacity says; vis-x/vis-y
    // then count from a screen source's screen_loc.
    const bool pass = relay.passMouse && src.app.hitAtom >= 0;
    for (Placed& p : img) {
        if (relay.blend) p.layer.blend = relay.blend;
        p.plane = plane;
        p.sortLayer = layer;
        p.seq = int(out.size());
        p.hitVisible = !p.footprint && (pass || curAtom_ >= 0);
        p.own = p.maptext = false;
        if (pass) {
            p.atom = src.app.hitAtom;
            p.passMouse = true;
            if (src.hasOrigin) {
                p.frame.hasVisOrigin = true;
                p.frame.visLeft = src.left;
                p.frame.visBottom = src.bottom;
            }
        } else {
            p.atom = curAtom_;
            p.frame = frame;
        }
        out.push_back(std::move(p));
    }
}

// Flatten (the path used for HUD objects). Overlays keep their own raw plane/layer (no float resolution on this path)
// and inherit the parent's pixel offset.
void Assembler::flatten(const RApp& a, int w, int h, std::vector<FlatLayer>& out, bool bottom) {
    bool drawable;
    SpritePtr sp = sprite(a, drawable);
    if (failed_) return;
    // Underlays ahead of this appearance's own icon, the overlays after it.
    for (RApp ov : a.overlays) {
        if (!ov.underlay) continue;
        ov.px += a.px;
        ov.py += a.py;
        flatten(ov, w, h, out, bottom);
        if (failed_) return;
    }
    if (drawable) {
        FlatLayer f;
        f.l.sprite = sp;
        f.l.x = a.px;                             // +px east, +py north (= -Y)
        f.l.y = (bottom ? h - sp->h : 0) - a.py;  // bottom: anchored at the w x h cell's bottom-left, as on the map
        setFx(f.l, a);
        if (a.cmat) {
            // The layer is a fresh TRANSPARENT canvas (w x h, or with `bottom` joined with the sprite's rectangle): a colour
            // matrix maps a blank pixel to its constant row, so the blank margin must exist as pixels.
            int cx0 = 0, cy0 = 0, cw = w, ch = h;
            if (bottom) {
                cx0 = std::min(0, f.l.x);
                cy0 = std::min(0, f.l.y);
                cw = std::max(w, f.l.x + sp->w) - cx0;
                ch = std::max(h, f.l.y + sp->h) - cy0;
            }
            auto cv = std::make_shared<Sprite>();
            cv->w = cw;
            cv->h = ch;
            cv->px.assign(size_t(cw) * size_t(ch) * 4, 0);
            for (int sy = 0; sy < sp->h; sy++) {
                int dy = f.l.y + sy - cy0;
                if (dy < 0 || dy >= ch) continue;
                for (int sx = 0; sx < sp->w; sx++) {
                    int dx = f.l.x + sx - cx0;
                    if (dx < 0 || dx >= cw) continue;
                    std::memcpy(&cv->px[(size_t(dy) * size_t(cw) + size_t(dx)) * 4],
                                &sp->px[(size_t(sy) * size_t(sp->w) + size_t(sx)) * 4], 4);
                }
            }
            f.l.sprite = cv;
            f.l.x = cx0;
            f.l.y = cy0;
        }
        f.plane = a.plane;
        f.layer = a.layer;
        out.push_back(std::move(f));
    }
    for (RApp ov : a.overlays) {
        if (ov.underlay) continue;
        ov.px += a.px;
        ov.py += a.py;
        flatten(ov, w, h, out, bottom);
        if (failed_) return;
    }
}

// FLOAT_PLANE / FLOAT_LAYER resolved against the parent, as the map does it (placeApp).
static void resolveFloat(RApp& p) {
    for (auto& o : p.overlays) {
        o.plane = resolvePlane(o.plane, p.plane);
        if (o.layer < 0) o.layer = p.layer;
        resolveFloat(o);
    }
}

// The right-click menu's icon for one atom: its appearance drawn on its own, the raw icon state plus its overlays and
// underlays, all at the canvas origin, on a canvas the size of its icon. The rules are DreamSeeker's (see scene.h); the
// shape (build appearance -> flatten -> composite sort) is the map's own.
bool Assembler::appearanceIcon(uint32_t appid, int dir, int& w, int& h, std::vector<uint8_t>& rgba, int64_t startNs) {
    std::set<uint32_t> visited;
    RApp ra;
    if (!buildApp(appid, visited, ra) || failed_) return false;
    applyIconAnimation(ra, startNs);  // the frame at clockNs_ (stat panel icons animate; at clock 0, the first frame)
    if (dir >= 0) {  // the facing the atom is drawn with on the map (a movable's own dir overrides its appearance's)
        ra.dir = dir;
        for (auto& o : ra.overlays) applyOverlayDir(o, dir);
    }
    // Stock DreamSeeker: no colour (scalar, matrix, inherited or an overlay's own), no alpha, no transform (flatten never
    // applies one), and no pixel offset, neither the atom's own nor an overlay's (an image() overlay with pixel_x = 8 sits at 0).
    std::function<void(RApp&)> raw = [&](RApp& r) {
        r.hasTint = false;
        r.tr = r.tg = r.tb = 255;
        r.alpha = 255;
        r.cmat = nullptr;
        r.px = 0;
        r.py = 0;
        for (auto& o : r.overlays) raw(o);
    };
    if (g_sceneMutation != 19) raw(ra);  // test-only: draw it the way the MAP does - colour, alpha and offsets kept
    // flatten keeps an overlay's raw layer, which sorts an image() overlay (layer FLOAT_LAYER = -1) beneath its parent.
    // Stock DreamSeeker draws such an overlay over the atom's own sprite in the menu icon.
    ra.plane = resolvePlane(ra.plane, 0);
    if (g_sceneMutation != 20) resolveFloat(ra);  // test-only: flatten's raw overlay layers
    // The canvas is the atom's own icon; an iconless atom still shows its overlays (as stock DreamSeeker does), on the first icon found.
    std::function<const Icon*(const RApp&)> firstIcon = [&](const RApp& r) -> const Icon* {
        if (r.icon) return r.icon;
        for (const RApp& o : r.overlays)
            if (const Icon* ic = firstIcon(o)) return ic;
        return nullptr;
    };
    const Icon* canvas = firstIcon(ra);
    if (!canvas || canvas->width <= 0 || canvas->height <= 0) return false;
    w = canvas->width;
    h = canvas->height;
    std::vector<FlatLayer> fl;
    flatten(ra, w, h, fl);
    if (failed_ || fl.empty()) return false;
    std::vector<size_t> ord(fl.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    // Stable by (Plane, Layer), ADD after non-ADD at a tie (the same sort as the HUD tile path).
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
        if (fl[a].layer != fl[b].layer) return fl[a].layer < fl[b].layer;
        return (fl[a].l.blend == kBlendAdd ? 1 : 0) < (fl[b].l.blend == kBlendAdd ? 1 : 0);
    });
    rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (size_t i : ord) blendLayerOver(rgba.data(), w, h, fl[i].l);
    return true;
}

// The HUD ref's own atom (its appid, plus icon animation) when it is also a movable, else the slot's appearance.
bool Assembler::screenApp(uint32_t id, uint32_t slotAppid, RApp& ra) {
    std::set<uint32_t> visited;
    auto atom = w_.atoms.find(id);
    if (atom != w_.atoms.end()) {
        if (!buildApp(atom->second.appid, visited, ra)) return false;
        applyIconAnimation(ra, hudIconStart(id));
        ra.px += int(atom->second.pixelX);
        ra.py += int(atom->second.pixelY);
    } else {
        if (!buildApp(slotAppid, visited, ra)) return false;
        applyIconAnimation(ra, hudIconStart(id));  // screen objects animate too
    }
    return true;
}

// The atom render targets this map control can relay: its screen objects', then the map atoms' in the placed region.
// The first of a name wins; a plane master's render_target is the executor's slate instead.
void Assembler::collectTargets(int cols, int rows, int tw, int th) {
    targets_.clear();
    screenTargetHit_.clear();
    for (uint32_t id : w_.hudOrder) {
        auto slotIt = w_.hud.find(id);
        if (slotIt == w_.hud.end()) continue;
        uint32_t slotAppid = slotIt->second.appid;
        const Ap* app = ap(slotAppid);
        if (!app || app->renderTarget.empty() || app->planeMaster() || app->screenLoc.empty()) continue;
        if (w_.haveSeeInvisible && app->invisibility > int(w_.seeInvisible)) continue;
        ParsedScreenLoc p;
        if (!parseScreenLoc(app->screenLoc, cols, rows, tw, th, p) || !p.mapCtrl.empty()) continue;
        RApp ra;
        if (!screenApp(id, slotAppid, ra) || ra.target.empty() || targets_.count(ra.target)) continue;
        Target t;
        t.hasOrigin = true;
        t.left = p.x;
        t.bottom = rows * th - p.y;
        ra.px = ra.py = 0;
        if (hit_) {
            ra.hitAtom = newHitAtom(ra.appid, (id & kRegistryA) ? kAtomMob : kAtomObj, id & ~kRegistryA, true);
            hit_->atoms[size_t(ra.hitAtom)].dir = ra.dir;
            screenTargetHit_[id] = ra.hitAtom;
        }
        t.app = std::move(ra);
        targets_.emplace(t.app.target, std::move(t));
    }
    for (const Tile& t : grid_)
        for (const RApp& ra : t) {
            if (ra.target.empty() || targets_.count(ra.target)) continue;
            Target c;
            c.app = ra;
            c.app.px = c.app.py = 0;
            targets_.emplace(ra.target, std::move(c));
        }
}

// Screen objects join the plane pipeline, with or without a plane master, as unclipped screen-space layers marked
// `screen`, drawn above the map within their plane and layer band; each overlay and underlay on its own resolved plane.
void Assembler::placeHud(int cols, int rows, int tw, int th, std::vector<Placed>& placed) {
    for (uint32_t id : w_.hudOrder) {
        auto slotIt = w_.hud.find(id);
        if (slotIt == w_.hud.end()) continue;
        uint32_t slotAppid = slotIt->second.appid;
        const Ap* app = ap(slotAppid);
        if (!app || app->screenLoc.empty()) continue;
        if (w_.haveSeeInvisible && app->invisibility > int(w_.seeInvisible)) continue;
        if (app->planeMaster())
            continue;  // A master's icon is never drawn; the plane pass consumes the master instead.
        if (!app->renderTarget.empty() && app->renderTarget[0] == '*') continue;  // drawn only where it is relayed
        // An iconless screen atom must not become a grey square.
        std::function<bool(uint32_t, std::set<uint32_t>&)> resolved = [&](uint32_t aid, std::set<uint32_t>& vis) {
            if (vis.count(aid)) return false;
            const Ap* a = ap(aid);
            if (!a) return false;
            vis.insert(aid);
            bool r = false;
            if (a->iconResID != kNoIcon && a->iconResID < w_.resources.size() &&
                res_.iconResolved(w_.resources[a->iconResID].first, w_.resources))
                r = true;
            for (uint32_t u : a->underlays)
                if (!r && resolved(u, vis)) r = true;
            for (uint32_t o : a->overlays)
                if (!r && resolved(o, vis)) r = true;
            vis.erase(aid);
            return r;
        };
        std::set<uint32_t> vis0;
        // A relay is iconless on purpose. An iconless atom is still in the hit sidecar, by its cell (HitHud::layers empty).
        const bool iconless = app->renderSource.empty() && !imgsOn_.count(id) && !resolved(slotAppid, vis0);
        auto vc = w_.visContents.find(id);
        if (iconless && !hit_ && (vc == w_.visContents.end() || vc->second.empty())) continue;

        RApp ra;
        auto atom = w_.atoms.find(id);
        if (!screenApp(id, slotAppid, ra)) continue;
        std::vector<RApp> attached;
        const bool overridden = imagesOn(id, ra, attached);
        if (overridden) {
            ra.icon = nullptr;
            ra.state.clear();
            ra.overlays.clear();
            ra.maptext.clear();
            ra.relay.clear();
        }
        for (RApp& a : attached) ra.overlays.push_back(std::move(a));
        markParticles(ra, id);

        static const std::array<float, 6> kIdentity{1, 0, 0, 0, 1, 0};
        const bool warp = ra.hasXform && ra.xf != kIdentity;  // placeWarpedScreenObj below
        if (!iconless && atom != w_.atoms.end() && atom->second.animating) st_.animatedAtoms++;

        ParsedScreenLoc p;
        if (!parseScreenLoc(app->screenLoc, cols, rows, tw, th, p) || !p.mapCtrl.empty()) continue;
        int spanC = std::max(p.spanCols, 1), spanR = std::max(p.spanRows, 1);
        if (warp && !iconless) {
            // A transformed screen object is not placed as an ordinary appearance; it is rendered
            // untransformed at its icon's native size, inverse-warped about the icon's own centre onto the
            // viewport canvas (placeWarpedScreenObj) and placed as ONE layer at the canvas's own origin.
            for (int cx = 0; cx < spanC; cx++)
                for (int cy = 0; cy < spanR; cy++) {
                    int baseX = p.x + cx * tw;
                    int baseY = rows * th - (p.y + cy * th) - th;
                    size_t n0 = placed.size();
                    placeWarpedScreenObj(ra, double(baseX), double(baseY + th), cols * tw, rows * th, placed);
                    if (failed_) return;
                    for (size_t k = n0; k < placed.size(); k++) placed[k].screen = true;
                }
        }
        if (!warp && !iconless) {
            // baseY is the cell's TOP edge (screen_loc's origin is the bottom-left).
            for (int cx = 0; cx < spanC; cx++)
                for (int cy = 0; cy < spanR; cy++) {
                    int baseX = p.x + cx * tw;
                    int baseY = rows * th - (p.y + cy * th) - th;
                    size_t n0 = placed.size();
                    curAtom_ = -1;
                    placeApp(ra, baseX, baseY, th, 0, 0, false, placed);
                    if (failed_) return;
                    for (size_t k = n0; k < placed.size(); k++) {
                        placed[k].hitVisible = false;
                        placed[k].screen = true;
                    }
                }
        }
        // A screen object's vis_contents (tg's parallax_home, iconless) draw at its first cell. Stock DreamSeeker 516
        // does not carry the holder's pixel offsets to them (planefilters-world, row 4).
        std::vector<RApp> kids;
        if (g_sceneMutation != 61) {
            std::set<uint32_t> visitedV;
            visContents(id, visitedV, kids, &ra);
        }
        for (RApp& k : kids) {
            const size_t n0 = placed.size();
            curAtom_ = -1;
            placeApp(k, p.x, rows * th - p.y - th, th, 0, 0, false, placed);
            if (failed_) return;
            for (size_t i = n0; i < placed.size(); i++) {
                placed[i].hitVisible = false;
                placed[i].screen = true;
            }
        }
        if (!hit_) continue;
        // The hit canvas is the cell joined with every layer's rectangle: a screen object is clicked whole, never clipped.
        RApp hr = ra;
        hr.plane = resolvePlane(hr.plane, 0);
        resolveFloat(hr);
        std::vector<FlatLayer> fl;
        if (!iconless) flatten(hr, tw, th, fl, true);
        if (failed_) return;
        // An own icon that draws nothing still holds its cell, as a map atom's does (Placed::footprint).
        int ownState = 0;
        const bool footprint = !overridden && app->renderSource.empty() && (iconless || !drawableState(hr, ownState));
        std::vector<size_t> ord(fl.size());
        for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
        std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
            if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
            const DrawKey ka = drawKey(fl[a].layer, true, -1), kb = drawKey(fl[b].layer, true, -1);
            if (ka != kb) return ka < kb;
            return (fl[a].l.blend == kBlendAdd ? 1 : 0) <
                   (fl[b].l.blend == kBlendAdd ? 1 : 0);  // ADD after non-ADD at a tie
        });
        int bx0 = 0, by0 = 0, bx1 = tw, by1 = th;
        for (const FlatLayer& f : fl) {
            if (!f.l.sprite) continue;
            bx0 = std::min(bx0, f.l.x);
            by0 = std::min(by0, f.l.y);
            bx1 = std::max(bx1, f.l.x + f.l.sprite->w);
            by1 = std::max(by1, f.l.y + f.l.sprite->h);
        }
        if (footprint) {
            bx0 = std::min(bx0, ra.px);
            by0 = std::min(by0, -ra.py);
            bx1 = std::max(bx1, ra.px + tw);
            by1 = std::max(by1, th - ra.py);
        }
        HitHud hh;
        hh.tileW = bx1 - bx0;
        hh.tileH = by1 - by0;
        for (int cx = 0; cx < spanC; cx++)
            for (int cy = 0; cy < spanR; cy++) {
                int destX = p.x + cx * tw + bx0;
                int destY = rows * th - (p.y + cy * th) - th + by0;
                if (!(destX < cols * tw && destX + hh.tileW > 0 && destY < rows * th && destY + hh.tileH > 0)) continue;
                hh.dests.push_back({destX, destY});
            }
        if (hh.dests.empty()) continue;
        // The HUD ref is named by its id; the appearance drawn answers for name/opacity.
        auto shared = screenTargetHit_.find(id);
        hh.atom = shared != screenTargetHit_.end()
                      ? shared->second
                      : newHitAtom(ra.appid, (id & kRegistryA) ? kAtomMob : kAtomObj, id & ~kRegistryA, true);
        hit_->atoms[size_t(hh.atom)].dir = ra.dir;
        hh.plane = hr.plane;  // the screen atom's own plane/layer order it in the hit test
        hh.layer = ra.layer;
        hh.originX = ra.px - bx0;
        hh.originY = th - ra.py - by0;
        if (footprint) {
            SpritePtr& blank = blankCache_[{tw, th}];
            if (!blank) {
                auto b = std::make_shared<Sprite>();
                b->w = tw;
                b->h = th;
                b->px.assign(size_t(tw) * size_t(th) * 4, 0);
                blank = b;
            }
            HitLayer l{blank, ra.px - bx0, -ra.py - by0, hh.atom, {}, true, hr.plane};
            l.footprint = true;
            l.key = drawKey(ra.layer, true, -1);
            hh.layers.push_back(std::move(l));
        }
        for (size_t i : ord) {
            HitLayer l{fl[i].l.sprite, fl[i].l.x - bx0, fl[i].l.y - by0, hh.atom, {}, false, fl[i].plane};
            l.key = drawKey(fl[i].layer, true, -1);
            hh.layers.push_back(std::move(l));
        }
        hit_->hud.push_back(std::move(hh));
    }
}

// A transformed screen object. anchorX/anchorBottomY is the screen_loc cell's bottom-left in viewport pixels; the pivot
// is the icon's own centre at its native size. The source is the atom without its top-level transform, flattened and
// sorted by (plane, layer, ADD-last), with each layer under its own blend onto a transparent ow x oh canvas. The
// destination is scanned only over the bounding box (the forward image of the source rect, padded 2 px) clipped to the
// canvas, sampling nearest with
// floor(half + d + 0.5); a fully transparent sample is left unwritten. The result is placed with alpha 255, the
// object's own blend mode and layer, plane resolvePlane(plane, 0).
void Assembler::placeWarpedScreenObj(const RApp& ra, double anchorX, double anchorBottomY, int canvasW, int canvasH,
                                     std::vector<Placed>& placed) {
    bool drawable;
    SpritePtr native = sprite(ra, drawable);
    if (failed_ || !drawable || !native || native->w <= 0 || native->h <= 0) return;
    const int ow = native->w, oh = native->h;
    const double centerX = anchorX + double(ow) / 2;
    const double centerY = anchorBottomY - double(oh) / 2;

    RApp un = ra;
    un.hasXform = false;
    un.xf = {1, 0, 0, 0, 1, 0};
    std::vector<FlatLayer> fl;
    flatten(un, ow, oh, fl);
    if (failed_) return;
    std::vector<size_t> ord(fl.size());
    for (size_t i = 0; i < ord.size(); i++) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
        if (fl[a].plane != fl[b].plane) return fl[a].plane < fl[b].plane;
        if (fl[a].layer != fl[b].layer) return fl[a].layer < fl[b].layer;
        return (fl[a].l.blend == kBlendAdd ? 1 : 0) < (fl[b].l.blend == kBlendAdd ? 1 : 0);
    });
    std::vector<uint8_t> src(size_t(ow) * size_t(oh) * 4, 0);
    for (size_t i : ord) blendLayerOver(src.data(), ow, oh, fl[i].l);

    const double a_ = double(ra.xf[0]), b_ = double(ra.xf[1]), c_ = double(ra.xf[2]);
    const double d_ = double(ra.xf[3]), e_ = double(ra.xf[4]), f_ = double(ra.xf[5]);
    const double det = a_ * e_ - b_ * d_;
    if (det == 0) return;  // degenerate: draws nothing
    const double inv00 = e_ / det, inv01 = -b_ / det, inv10 = -d_ / det, inv11 = a_ / det;
    const double tx = c_, ty = -f_;  // DM +y is up; image +y is down
    const double halfW = double(ow) / 2, halfH = double(oh) / 2;

    // The destination window: the forward image of the source rect, clipped to the canvas.
    int sx0 = 0, sy0 = 0, sx1 = canvasW, sy1 = canvasH;
    {
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        const double corners[4][2] = {{0, 0}, {double(ow), 0}, {0, double(oh)}, {double(ow), double(oh)}};
        for (const auto& c : corners) {
            double dxDm = c[0] - halfW, dyDm = -(c[1] - halfH);
            double dxDmP = a_ * dxDm + b_ * dyDm;
            double dyDmP = d_ * dxDm + e_ * dyDm;
            double x = centerX + tx + dxDmP;
            double y = centerY + ty - dyDmP;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
        if (!(std::isinf(minX) || std::isnan(minX) || std::isnan(minY) || std::isnan(maxX) || std::isnan(maxY))) {
            const int pad = 2;
            int bx0 = int(std::floor(minX)) - pad, by0 = int(std::floor(minY)) - pad;
            int bx1 = int(std::ceil(maxX)) + pad + 1, by1 = int(std::ceil(maxY)) + pad + 1;
            sx0 = std::max(bx0, 0);
            sy0 = std::max(by0, 0);
            sx1 = std::min(bx1, canvasW);
            sy1 = std::min(by1, canvasH);
        }
    }
    if (sx0 >= sx1 || sy0 >= sy1) return;  // lands entirely off-canvas

    auto out = std::make_shared<Sprite>();
    out->w = sx1 - sx0;
    out->h = sy1 - sy0;
    out->px.assign(size_t(out->w) * size_t(out->h) * 4, 0);
    for (int y = sy0; y < sy1; y++)
        for (int x = sx0; x < sx1; x++) {
            double dxImgP = double(x) - centerX - tx;
            double dyImgP = double(y) - centerY - ty;
            double dxDmP = dxImgP, dyDmP = -dyImgP;
            double dxDm = inv00 * dxDmP + inv01 * dyDmP;
            double dyDm = inv10 * dxDmP + inv11 * dyDmP;
            double dxImg = dxDm, dyImg = -dyDm;
            int lx = int(std::floor(halfW + dxImg + 0.5));
            int ly = int(std::floor(halfH + dyImg + 0.5));
            if (lx < 0 || ly < 0 || lx >= ow || ly >= oh) continue;
            const uint8_t* sp = &src[(size_t(ly) * size_t(ow) + size_t(lx)) * 4];
            if (sp[3] == 0) continue;
            std::memcpy(&out->px[(size_t(y - sy0) * size_t(out->w) + size_t(x - sx0)) * 4], sp, 4);
        }

    Placed pl;
    pl.layer.sprite = out;
    pl.layer.x = sx0;
    pl.layer.y = sy0;
    pl.layer.alpha = 255;
    pl.layer.blend = ra.blend;
    pl.plane = resolvePlane(ra.plane, 0);
    pl.sortLayer = ra.layer;
    pl.seq = int(placed.size());
    pl.atom = -1;
    pl.hitVisible = false;
    placed.push_back(std::move(pl));
}

// ---- plane masters -----------------------------------------------------------------------------------

namespace {
// The raw payload of one param bit, walking the ascending-bit layout. false when the bit
// is absent, or when an EARLIER bit has no fixed width (the 0x200 cstr) - nothing after it is addressable.
bool filterParam(const Ap::Flt& f, uint32_t bit, const uint8_t*& data, size_t& len) {
    size_t off = 0;
    for (uint32_t b = 1; b <= 0x4000; b <<= 1) {
        if (!(f.mask & b)) continue;
        int w;
        switch (b) {
            case 0x1:
            case 0x2:
            case 0x4:
            case 0x8:
            case 0x10:
            case 0x40:
            case 0x100:
            case 0x800:
            case 0x1000: w = 4; break;
            case 0x20: w = 2; break;
            case 0x400: w = 80; break;
            case 0x2000:
            case 0x4000: w = 1; break;
            default: return false;  // 0x200 (cstr) or a bit the table does not know
        }
        if (b == bit) {
            if (off + size_t(w) > f.params.size()) return false;
            data = f.params.data() + off;
            len = size_t(w);
            return true;
        }
        off += size_t(w);
    }
    return false;
}
bool filterFloat(const Ap::Flt& f, uint32_t bit, double& out) {
    const uint8_t* d;
    size_t n;
    if (!filterParam(f, bit, d, n) || n != 4) return false;
    uint32_t b = uint32_t(d[0]) | uint32_t(d[1]) << 8 | uint32_t(d[2]) << 16 | uint32_t(d[3]) << 24;
    float fl;
    std::memcpy(&fl, &b, 4);
    out = double(fl);
    return true;
}
bool filterRGBA(const Ap::Flt& f, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    const uint8_t* d;
    size_t n;
    if (!filterParam(f, 0x2, d, n) || n != 4) return false;
    r = d[0];
    g = d[1];
    b = d[2];
    a = d[3];
    return true;
}
}  // namespace

// A plane master is a client.screen object (or an overlay of one) whose
// appearance carries PLANE_MASTER; masters NEST (a master's overlay may itself be a master for its own plane), and any
// other overlay joins the enclosing master's group. Children are collected before their parent (post-order), and the
// LAST master naming a plane wins.
void Assembler::collectMasters(uint32_t appid, std::set<uint32_t>& seen) {
    if (seen.count(appid)) return;
    const Ap* a = ap(appid);
    if (!a || !a->planeMaster()) return;
    seen.insert(appid);

    MasterRec m;
    m.spec = std::make_shared<PlaneMasterSpec>();
    PlaneMasterSpec& sp = *m.spec;
    sp.renderTarget = a->renderTarget;
    if (a->hasPlane) m.plane = truePlane(a->plane);
    m.hideFromMouse = a->mouseOpacity() == kMouseOpacityNone && g_sceneMutation != 23;
    if (a->hasColor) {
        sp.hasColor = true;
        sp.r = uint8_t(a->cr);
        sp.g = uint8_t(a->cg);
        sp.b = uint8_t(a->cb);
        sp.alpha = a->ca;
    }
    sp.cmat = a->cmat;
    if (a->hasTransform) {
        sp.hasXform = true;
        sp.xf = a->transform;
    }
    if (a->hasBlend) sp.blend = blendModeOfWire(a->blend);
    for (const Ap::Flt& f : a->filters) {
        double v;
        switch (f.type) {
            case 1: {  // blur
                if (!filterFloat(f, 0x1, v)) {
                    sp.unappliedFilters++;
                    break;
                }
                FilterSpec fs;
                fs.kind = FilterSpec::Blur;
                fs.size = v;
                sp.filters.push_back(fs);
                break;
            }
            case 2: {  // outline: DM's defaults size 1, colour black
                FilterSpec fs;
                fs.kind = FilterSpec::Outline;
                fs.size = 1;
                fs.alpha = 255;
                if (filterFloat(f, 0x1, v)) fs.size = v;
                uint8_t r, g, b, al;
                if (filterRGBA(f, r, g, b, al)) {
                    fs.r = r;
                    fs.g = g;
                    fs.b = b;
                    fs.alpha = al;
                }
                const uint8_t* d;
                size_t n;
                if (filterParam(f, 0x20, d, n) && n == 2) fs.flags = uint16_t(d[0] | d[1] << 8);
                if (fs.size <= 0 || fs.alpha == 0) {
                    sp.unappliedFilters++;
                    break;
                }
                sp.filters.push_back(fs);
                break;
            }
            case 3: {  // drop_shadow: DM's defaults x=1, y=-1, size=1, offset=0, colour black
                FilterSpec fs;
                fs.kind = FilterSpec::DropShadow;
                fs.x = 1;
                fs.y = -1;
                fs.size = 1;
                fs.alpha = 255;
                if (filterFloat(f, 0x4, v)) fs.x = int(v);
                if (filterFloat(f, 0x8, v)) fs.y = -int(v);  // DM's +y is NORTH, image space's +y is DOWN
                if (filterFloat(f, 0x1, v)) fs.size = v;
                if (filterFloat(f, 0x10, v)) fs.offset = v;
                uint8_t r, g, b, al;
                if (filterRGBA(f, r, g, b, al)) {
                    fs.r = r;
                    fs.g = g;
                    fs.b = b;
                    fs.alpha = al;
                }
                sp.filters.push_back(fs);
                break;
            }
            case 7: {  // alpha, by render_source only (icon 0xffff = none); DM's +y is NORTH
                const uint8_t* d;
                size_t n;
                const bool icon =
                    filterParam(f, 0x100, d, n) && n == 4 &&
                    (uint32_t(d[0]) | uint32_t(d[1]) << 8 | uint32_t(d[2]) << 16 | uint32_t(d[3]) << 24) != 0xffff;
                if (!(f.mask & 0x200) || icon || f.str.empty()) {
                    sp.unappliedFilters++;
                    break;
                }
                FilterSpec fs;
                fs.kind = FilterSpec::AlphaMask;
                fs.source = f.str;
                if (filterFloat(f, 0x4, v)) fs.x = int(v);
                if (filterFloat(f, 0x8, v)) fs.y = -int(v);
                if (filterParam(f, 0x20, d, n) && n == 2) fs.flags = uint16_t(d[0] | d[1] << 8);
                sp.filters.push_back(fs);
                break;
            }
            case 9: {  // color, RGB space only; the matrix is 20 floats in the appearance matrix's layout
                const uint8_t* d;
                size_t n;
                if (filterParam(f, 0x20, d, n) && n == 2 && (d[0] | d[1] << 8) != 0) {
                    sp.unappliedFilters++;
                    break;
                }
                if (!filterParam(f, 0x400, d, n) || n != 80) break;  // no matrix: the identity
                auto cm = std::make_shared<ColorMatrix>();
                for (size_t k = 0; k < 20; k++) {
                    uint32_t u = uint32_t(d[4 * k]) | uint32_t(d[4 * k + 1]) << 8 | uint32_t(d[4 * k + 2]) << 16 |
                                 uint32_t(d[4 * k + 3]) << 24;
                    float fl;
                    std::memcpy(&fl, &u, 4);
                    (*cm)[k] = double(fl);
                }
                FilterSpec fs;
                fs.kind = FilterSpec::ColorMatrix;
                fs.cmat = std::move(cm);
                sp.filters.push_back(fs);
                break;
            }
            default: sp.unappliedFilters++;
        }
    }
    st_.unappliedFilters += sp.unappliedFilters;
    std::vector<uint32_t> kids = a->underlays;
    kids.insert(kids.end(), a->overlays.begin(), a->overlays.end());
    for (uint32_t oid : kids) {
        const Ap* o = ap(oid);
        if (o && o->planeMaster()) {
            collectMasters(oid, seen);
            continue;
        }
        std::set<uint32_t> vis;
        RApp ra;
        if (buildApp(oid, vis, ra)) m.overlays.push_back(std::move(ra));
    }
    masters_.push_back(std::move(m));
}

// BYOND's blackness: one opaque black tile per hidden cell on plane 0, and only when a plane-0 master
// exists. `sight = SEE_PIXELS` is not modelled: visibility is per tile here, where stock DreamSeeker computes it per pixel.
void Assembler::appendBlackness(std::vector<Placed>& placed, int originX, int originY, int extentW, int extentH, int tw,
                                int th) {
    if (hidden_.empty()) return;
    bool hasPlane0 = false;
    for (const MasterRec& m : masters_)
        if (resolvePlane(m.plane, 0) == 0) {
            hasPlane0 = true;
            break;
        }
    if (!hasPlane0) return;
    auto black = std::make_shared<Sprite>();
    black->w = tw;
    black->h = th;
    black->px.assign(size_t(tw) * size_t(th) * 4, 0);
    for (size_t i = 3; i < black->px.size(); i += 4) black->px[i] = 255;
    // The margin ring matches run()'s shifted placement: a shifted frame reveals tiles beyond the region, and
    // blackness has to cover them too or the glide exposes a bright gutter.
    const int x0 = originX - ring_, y0 = originY - ring_, ew = extentW + 2 * ring_, eh = extentH + 2 * ring_;
    const int dx = ring_ ? shiftX_ - ring_ * tw : 0, dy = ring_ ? shiftY_ - ring_ * th : 0;
    for (int vr = 0; vr < eh; vr++)
        for (int vc = 0; vc < ew; vc++) {
            if (!hidden_.count({x0 + vc, y0 + vr})) continue;
            Placed p;
            p.layer.sprite = black;
            p.layer.x = vc * tw + dx;
            p.layer.y = (eh - 1 - vr) * th + dy;  // y-flip, as run() does
            p.plane = 0;
            p.sortLayer = 0;
            p.seq = int(placed.size());
            p.hitVisible = false;
            placed.push_back(std::move(p));
        }
}

bool Assembler::run(DrawList& dl, std::string* err) {
    int originX = 0, originY = 0, winW = 0, winH = 0, extentW = 0, extentH = 0, a = 0, b = 0;
    if (!buildGrid(originX, originY, winW, winH, extentW, extentH, a, b)) {
        if (err) *err = err_;
        return false;
    }
    int tw, th;
    tileSize(tw, th);
    dl.outW = extentW * tw;
    dl.outH = extentH * th;
    dl.groundBlack = true;

    collectTargets(extentW, extentH, tw, th);
    // Row-major over the view, y flipped (top of image = highest world y). While the map is shifted (an eye scroll, a
    // client.pixel_* offset) the region is ring_ tiles wider on every side, and everything placed is moved back by the
    // ring and on by the shift.
    std::vector<Placed> placed;
    ring_ = ringFor(tw, th);
    const int rx0 = originX - ring_, ry0 = originY - ring_;
    const int rw = extentW + 2 * ring_, rh = extentH + 2 * ring_;
    for (int vr = 0; vr < rh; vr++)
        for (int vc = 0; vc < rw; vc++) {
            int x = rx0 + vc, y = ry0 + vr;
            const Tile* tp = tileIn(x, y);
            if (!tp || tp->empty()) continue;
            const Tile& t = *tp;
            int baseX = vc * tw, baseY = (rh - 1 - vr) * th;
            for (const RApp& ra : t) {
                curAtom_ = ra.hitAtom;
                if (hit_ && ra.hitAtom >= 0 && !ra.borrowsHit) {
                    HitAtom& ha = hit_->atoms[size_t(ra.hitAtom)];
                    ha.worldX = x;
                    ha.worldY = y;
                    ha.dir = ra.dir;  // the facing it is DRAWN with (a movable's own dir, not its appearance's)
                }
                const size_t n0 = placed.size();
                placeApp(ra, baseX, baseY, th, 0, 0, false, placed);
                if (failed_) {
                    if (err) *err = err_;
                    return false;
                }
                if (ra.hitOnly)
                    for (size_t k = n0; k < placed.size(); k++) placed[k].noDraw = true;
                // Its draw key's cell: its own, or the one its pixel offset reaches into to the west or south.
                const int cell = mapCell(x - originX + 1 + std::min(0, floorDivI(ra.px, tw)),
                                         y - originY + 1 + std::min(0, floorDivI(ra.py, th)));
                for (size_t k = n0; k < placed.size(); k++) placed[k].cell = cell;
            }
        }
    st_.eyeScroll = scrollX_ != 0 || scrollY_ != 0;
    if (ring_ > 0) {
        const int dx = shiftX_ - ring_ * tw, dy = shiftY_ - ring_ * th;
        for (Placed& p : placed) {
            p.layer.x += dx;
            p.layer.y += dy;
            p.layer.relayBottomY += dy;
            p.frame.x += dx;
            p.frame.y += dy;
        }
    }
    st_.layers = int(placed.size());

    // Masters are collected before anything is composited.
    {
        std::set<uint32_t> seen;
        for (uint32_t id : w_.hudOrder) {
            auto slotIt = w_.hud.find(id);
            if (slotIt == w_.hud.end()) continue;
            uint32_t appid = slotIt->second.appid;
            if (!mastersFor(appid)) continue;
            collectMasters(appid, seen);
            if (failed_) {
                if (err) *err = err_;
                return false;
            }
        }
    }
    appendBlackness(placed, originX, originY, extentW, extentH, tw, th);
    placeHud(extentW, extentH, tw, th, placed);
    if (failed_) {
        if (err) *err = err_;
        return false;
    }

    if (!compositePlanes(placed, dl, th)) {
        if (err) *err = err_;
        return false;
    }

    if (hit_) {
        hit_->tileW = tw;
        hit_->tileH = th;
        hit_->cols = extentW;
        hit_->rows = extentH;
        hit_->originX = originX;
        hit_->originY = originY;
        finishHit(dl);
    }
    return true;
}

void Assembler::finishHit(DrawList& dl) {
    for (const HitAtom& a : hit_->atoms) hit_->anyHover = hit_->anyHover || a.hover;
    dl.hit = hit_;
}

bool Assembler::compositePlanes(std::vector<Placed>& placed, DrawList& dl, int th) {
    // Group by plane (ascending), then a STABLE sort within the plane by (draw key, ADD after non-ADD).
    // A master that carries overlays or a render_target gets a plane even when no
    // sprite sits on it, and its group overlays join the plane's layers BEFORE the sort.
    std::map<double, std::vector<Placed>> byPlane;
    for (auto& p : placed) byPlane[p.plane].push_back(std::move(p));
    std::map<double, const MasterRec*> masterOf;
    for (const MasterRec& m : masters_) {
        double mp =
            resolvePlane(m.plane, 0);  // a FLOAT_PLANE-relative master plane must be resolved to match its layers
        masterOf[mp] = &m;
        if (!byPlane.count(mp) && (!m.overlays.empty() || !m.spec->renderTarget.empty())) byPlane[mp];
    }
    for (auto& [plane, ls] : byPlane) {
        auto mit = masterOf.find(plane);
        if (mit != masterOf.end()) {
            // The master's overlays are placed with the plane as their parent.
            for (const RApp& ov : mit->second->overlays) {
                std::vector<Placed> grp;
                curAtom_ = -1;
                placeApp(ov, 0, 0, th, plane, 0, true, grp);
                if (failed_) return false;
                for (Placed& g : grp) {
                    g.seq = int(ls.size());
                    g.hitVisible = false;
                    ls.push_back(std::move(g));
                }
            }
        }
        auto addRank = [](const Placed& p) { return p.layer.blend == kBlendAdd ? 1 : 0; };
        for (Placed& p : ls)
            p.key = g_sceneMutation == 41 ? drawKey(p.sortLayer, p.screen, -1) : drawKey(p.sortLayer, p.screen, p.cell);
        if (g_sceneMutation == 2) {
            // test-only: break the arrival-order tie rule (equal layers drawn in reverse arrival)
            std::stable_sort(ls.begin(), ls.end(), [&](const Placed& a, const Placed& b) {
                if (a.screen != b.screen) return b.screen;
                if (a.sortLayer != b.sortLayer) return a.sortLayer < b.sortLayer;
                return a.seq > b.seq;
            });
        } else {
            // render::drawKey: the layer bands, a screen layer over the map layers of its band, a map layer's cell tie-break.
            std::stable_sort(ls.begin(), ls.end(), [&](const Placed& a, const Placed& b) {
                if (a.key != b.key) return a.key < b.key;
                return (g_sceneMutation == 15 ? 0 : addRank(a)) < (g_sceneMutation == 15 ? 0 : addRank(b));
            });
        }
        PlanePass pp;
        pp.plane = plane;
        if (mit != masterOf.end()) pp.master = mit->second->spec;
        // A master at mouse_opacity 0 hides its whole plane from the hit test: per placed layer, not per atom. The master
        // governs the plane a pixel is drawn on, so an atom's overlay on such a plane drops out while the atom's own icon on
        // another plane still answers (MasterRec::hideFromMouse).
        const bool mouseHidden = mit != masterOf.end() && mit->second->hideFromMouse;
        for (auto& p : ls) {
            if (hit_ && p.hitVisible && p.layer.sprite && !mouseHidden)
                hit_->world.push_back(HitLayer{p.layer.sprite, p.layer.x, p.layer.y, p.atom, p.frame, p.own, plane,
                                               p.maptext, p.footprint, p.key, p.passMouse});
            if (!p.footprint && !p.noDraw) pp.layers.push_back(std::move(p.layer));
        }
        dl.planes.push_back(std::move(pp));
    }
    st_.masters = int(masters_.size());
    return true;
}

static bool ieqAscii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Does the client.screen master `appid` govern the map being drawn? Its screen_loc's map prefix must name it: the main
// map takes only masters that name no map, a secondary map only those naming it (vg's camera console copies every
// plane master this way).
// The appearance is resolved here (ap()), not read from what the line-of-sight pass happened to cache: the masters loop
// can run before a client.screen appearance is cached (LOS skipped for SEE_TURFS|SEE_MOBS|SEE_OBJS sight, or the
// appearance not yet arrived), and an uncached answer of "yes" let the camera map's LIGHTING_PLANE master black out the
// main map. An appearance that is not here governs nothing (collectMasters refuses it too).
bool Assembler::mastersFor(uint32_t appid) {
    const Ap* a = ap(appid);
    return a && ieqAscii(screenLocMap(a->screenLoc), mapName_);
}

// A secondary map control: vg's security camera console - a map created at runtime inside a browse() popup, fed by
// client.screen objects whose screen_loc names it. The rules, as stock DreamSeeker applies them:
//   * the view is the bounding box of the map's screen objects, at the world's icon size; the map control scales it to
//     fit and centres it on a black letterbox (MapWidget's auto-fit), so the assembler produces the box at native size;
//   * an iconless object counts toward the box; a vis_contents does not widen it; an object placed by a view keyword
//     ("name:CENTER": plane masters and vg's plane/dark) is left out of it;
//   * vis_contents turfs (World::visTurfs) are drawn at their relative places, the cell (min x, min y) of their bounding
//     box on the holder's cell - also when that cell is not one of them - each with what stands on it (respecting
//     see_invisible), clipped to the box; the mouse names one by its cell when it is on the default map's z, else by its
//     turf id (HitAtom::byId);
//   * lighting draws in it: the camera map's own LIGHTING_PLANE master over the remote turfs' lighting_overlays;
//   * only the masters naming this map apply (mastersFor), and the default map draws none of these objects.
// Not modelled: the first map control created while its objects were already on client.screen keeps a world.view-sized
// view until an object's screen_loc next changes; a re-created control fits at once. The box's corner is the lowest
// screen_loc.
bool Assembler::runSecondary(std::string_view mapName, DrawList& dl, std::string* err) {
    mapName_ = std::string(mapName);
    maxX_ = w_.bounds.maxX;
    maxY_ = w_.bounds.maxY;
    const int tw = model::iconSizeOf(w_), th = tw;
    int cols = 2 * kDefaultViewRange + 1, rows = cols;  // only a keyword (CENTER, EAST...) consults them
    if (w_.bounds.viewW > 0 && w_.bounds.viewH > 0) {
        cols = w_.bounds.viewW;
        rows = w_.bounds.viewH;
    } else if (w_.bounds.view > 0)
        cols = rows = 2 * w_.bounds.view + 1;
    collectImages();

    struct Obj {
        uint32_t id, appid;
        ParsedScreenLoc p;
        bool keyword;
    };
    std::vector<Obj> objs;
    std::set<uint32_t> seen;
    for (uint32_t id : w_.hudOrder) {
        auto slotIt = w_.hud.find(id);
        if (slotIt == w_.hud.end()) continue;
        uint32_t appid = slotIt->second.appid;
        const Ap* app = ap(appid);
        if (!app || !ieqAscii(screenLocMap(app->screenLoc), mapName_)) continue;
        if (app->planeMaster()) {
            collectMasters(appid, seen);
            if (failed_) break;
            continue;
        }
        if (w_.haveSeeInvisible && app->invisibility > int(w_.seeInvisible)) continue;
        Obj o{id, appid, {}, false};
        if (!parseScreenLoc(app->screenLoc, cols, rows, tw, th, o.p)) continue;
        // A position that moves with the view size (CENTER, EAST, NORTH...) is relative to the box, not part of it.
        ParsedScreenLoc wider;
        o.keyword = parseScreenLoc(app->screenLoc, cols + 10, rows + 10, tw, th, wider) &&
                    (wider.x != o.p.x || wider.y != o.p.y);
        objs.push_back(o);
    }
    if (failed_) {
        if (err) *err = err_;
        return false;
    }
    dl.groundBlack = true;
    if (hit_) {
        hit_->secondary = true;
        hit_->tileW = tw;
        hit_->tileH = th;
        hit_->cols = hit_->rows = 1;
    }
    if (objs.empty()) {  // a map with nothing on it: black
        dl.outW = tw;
        dl.outH = th;
        if (hit_) finishHit(dl);
        return true;
    }
    // The box: every object placed by numbers. A keyword object is then placed against the box's own size. Only when
    // nothing is placed by numbers do keyword objects make the box (at the world's view size).
    bool anyNumeric = false;
    for (const Obj& o : objs) anyNumeric = anyNumeric || !o.keyword;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const Obj& o : objs) {
        if (anyNumeric && o.keyword) continue;
        x0 = std::min(x0, o.p.x);
        y0 = std::min(y0, o.p.y);
        x1 = std::max(x1, o.p.x + std::max(o.p.spanCols, 1) * tw);
        y1 = std::max(y1, o.p.y + std::max(o.p.spanRows, 1) * th);
    }
    const int W = x1 - x0, H = y1 - y0;
    dl.outW = W;
    dl.outH = H;
    if (anyNumeric)
        for (Obj& o : objs) {
            if (!o.keyword) continue;
            const Ap* app = ap(o.appid);
            ParsedScreenLoc q;
            if (app && parseScreenLoc(app->screenLoc, W / tw, H / th, tw, th, q)) {
                q.x += x0;
                q.y += y0;
                o.p = q;
            }
        }

    // What stands on each turf (only built when some holder shows turfs).
    std::map<std::array<int, 3>, std::vector<uint32_t>> onTurf;
    bool anyTurfs = false;
    for (const Obj& o : objs) anyTurfs = anyTurfs || w_.visTurfs.count(o.id);
    if (anyTurfs)
        for (const auto& [aid, m] : w_.atoms)
            if (m.hasPos) onTurf[{m.x, m.y, m.z}].push_back(aid);  // ascending id, as the default map

    std::vector<Placed> placed;
    // The default map's z: a turf on any other is named by its id. CellGrid::viewZ is 1 when the refresh grid is not in
    // use, so without it every turf off z1 is named by id.
    int viewZ = 0;
    for (const Obj& o : objs) {
        RApp ra;
        std::set<uint32_t> visited;
        auto atom = w_.atoms.find(o.id);
        if (atom != w_.atoms.end()) {
            if (!buildApp(atom->second.appid, visited, ra)) continue;
            ra.px += int(atom->second.pixelX);
            ra.py += int(atom->second.pixelY);
        } else if (!buildApp(o.appid, visited, ra))
            continue;
        applyIconAnimation(ra, hudIconStart(o.id));
        const int spanC = std::max(o.p.spanCols, 1), spanR = std::max(o.p.spanRows, 1);
        const int holder = newHitAtom(ra.appid, (o.id & kRegistryA) ? kAtomMob : kAtomObj, o.id & ~kRegistryA, true);
        if (holder >= 0) hit_->atoms[size_t(holder)].dir = ra.dir;
        for (int cx = 0; cx < spanC; cx++)
            for (int cy = 0; cy < spanR; cy++) {
                curAtom_ = holder;
                placeApp(ra, o.p.x - x0 + cx * tw, H - (o.p.y - y0 + cy * th) - th, th, 0, 0, false, placed);
                if (failed_) {
                    if (err) *err = err_;
                    return false;
                }
            }
        const int hx = o.p.x - x0, hy = H - (o.p.y - y0) - th;  // the holder's (first) cell, top-left
        std::vector<RApp> kids;
        std::set<uint32_t> visitedV;
        visContents(o.id, visitedV, kids, &ra);
        for (const RApp& k : kids) {
            curAtom_ = k.hitAtom;
            placeApp(k, hx, hy, th, 0, 0, false, placed);
        }
        auto vt = w_.visTurfs.find(o.id);
        if (vt == w_.visTurfs.end() || maxX_ <= 0 || maxY_ <= 0) continue;
        const uint32_t mx = uint32_t(maxX_), my = uint32_t(maxY_);
        if (viewZ == 0) viewZ = s_.cells().viewZ;
        int minX = INT32_MAX, minY = INT32_MAX;
        for (uint32_t c : vt->second) {
            minX = std::min(minX, int(c % mx));
            minY = std::min(minY, int(c / mx % my));
        }
        for (uint32_t c : vt->second) {
            const int tx = int(c % mx), ty = int(c / mx % my), tz = int(c / (mx * my));
            const int bx = hx + (tx - minX) * tw, by = hy - (ty - minY) * th;
            if (bx >= W || by >= H || bx + tw <= 0 || by + th <= 0) continue;  // outside the box: clipped
            auto t = w_.tiles.find({uint16_t(tx), uint16_t(ty), uint16_t(tz)});
            if (t != w_.tiles.end() && !hiddenFromClient(t->second)) {
                RApp tr;
                std::set<uint32_t> v;
                if (buildApp(t->second, v, tr)) {
                    applyIconAnimation(tr, tileIconStart(tx, ty, tz));
                    curAtom_ = newHitAtom(t->second, kAtomTurf, 0, false);
                    if (curAtom_ >= 0) {
                        HitAtom& ha = hit_->atoms[size_t(curAtom_)];
                        ha.worldX = tx + 1;
                        ha.worldY = ty + 1;
                        ha.byId = tz + 1 != viewZ;
                        ha.turfId = c;
                    }
                    placeApp(tr, bx, by, th, 0, 0, false, placed);
                }
            }
            auto on = onTurf.find({tx + 1, ty + 1, tz});
            if (on == onTurf.end()) continue;
            for (uint32_t aid : on->second) {
                const model::Atom& m = w_.atoms.at(aid);
                if (hiddenFromClient(m.appid)) continue;
                RApp ma;
                if (!animatedApp(aid, m, ma)) continue;
                ma.dir = m.dir;
                for (auto& ov : ma.overlays) applyOverlayDir(ov, m.dir);
                int gx = int(m.pixelX) + int(m.pixelW), gy = int(m.pixelY) + int(m.pixelZ);
                ma.px += gx;
                ma.py += gy;
                std::vector<RApp> attached;
                std::set<uint32_t> va;
                visContents(aid, va, attached, &ma);
                curAtom_ = newHitAtom(m.appid, (aid & kRegistryA) ? kAtomMob : kAtomObj, aid & ~kRegistryA, false);
                if (curAtom_ >= 0) {
                    HitAtom& ha = hit_->atoms[size_t(curAtom_)];
                    ha.worldX = tx + 1;
                    ha.worldY = ty + 1;
                    ha.dir = m.dir;
                }
                placeApp(ma, bx, by, th, 0, 0, false, placed);
                for (auto& a : attached) {
                    a.px += gx;
                    a.py += gy;
                    curAtom_ = a.hitAtom;
                    placeApp(a, bx, by, th, 0, 0, false, placed);
                }
            }
            if (failed_) {
                if (err) *err = err_;
                return false;
            }
        }
    }
    st_.layers = int(placed.size());
    if (!compositePlanes(placed, dl, th)) {
        if (err) *err = err_;
        return false;
    }
    if (hit_) {
        hit_->cols = std::max(1, (W + tw - 1) / tw);
        hit_->rows = std::max(1, (H + th - 1) / th);
        hit_->screenX0 = floorDivI(x0, tw);
        hit_->screenY0 = floorDivI(y0, th);
        finishHit(dl);
    }
    return true;
}

}  // namespace

// ---- ResStore ------------------------------------------------------------------------------------

bool ResStore::add(ByteSpan bytes, const uint32_t* claimedId, std::string* err) {
    uint32_t id = net::resId(bytes);
    if (claimedId && *claimedId != id) {
        char b[96];
        std::snprintf(b, sizeof b, "resource named %08x hashes to %08x: refused", *claimedId, id);
        if (err) *err = b;
        return false;
    }
    bytes_[id] = Bytes(bytes.begin(), bytes.end());
    icons_.erase(id);
    bad_.erase(id);
    return true;
}

const Icon* ResStore::icon(uint32_t id) {
    auto c = icons_.find(id);
    if (c != icons_.end()) return c->second.get();
    if (bad_.count(id)) return nullptr;
    auto b = bytes_.find(id);
    if (b == bytes_.end()) {
        wanted_.insert(id);
        return nullptr;
    }
    // A DDMI recipe holds no raster: it cannot be parsed as an icon and must NOT poison bad_ (iconResolved owns it).
    if (isDdmi(b->second.data(), b->second.size())) return nullptr;
    ParseResult r = parseIcon(b->second.data(), b->second.size());
    if (!r.ok()) {
        bad_[id] = true;
        std::string head;
        for (size_t i = 0; i < std::min<size_t>(8, b->second.size()); i++) {
            char x[3];
            std::snprintf(x, sizeof x, "%02x", b->second[i]);
            head += x;
        }
        refused_[id] = "icon parse: " + r.errClass + " " + r.errDetail + " (" + std::to_string(b->second.size()) +
                       " bytes, head " + head + ")";
        return nullptr;
    }
    const Icon* p = r.icon.get();
    icons_[id] = std::move(r.icon);
    return p;
}

// Recipe resolution. PENDING (an operand whose bytes or whose ordinal has not arrived yet) and PERMANENT (a refused
// shape, a missing icon_state, an operand that is not an icon, a cycle) are different failures and only the second may poison.
ResStore::RStat ResStore::resolveOne(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals,
                                     std::set<uint32_t>& inProgress, const Icon*& out) {
    out = nullptr;
    if (auto r = recipeIcon_.find(id); r != recipeIcon_.end()) {
        out = r->second;
        return RStat::Ok;
    }
    if (const Icon* ic = (icons_.count(id) ? icons_[id].get() : nullptr)) {
        out = ic;
        return RStat::Ok;
    }
    if (bad_.count(id)) return RStat::Permanent;
    auto b = bytes_.find(id);
    if (b == bytes_.end()) {
        wanted_.insert(id);
        return RStat::Pending;
    }
    if (!isDdmi(b->second.data(), b->second.size())) {
        // An ordinary icon reached here as somebody's operand: resolve it as the plain path would.
        const Icon* ic = icon(id);
        if (ic) {
            out = ic;
            return RStat::Ok;
        }
        return bad_.count(id) ? RStat::Permanent : RStat::Pending;
    }
    if (inProgress.count(id)) {
        bad_[id] = true;
        refused_[id] = "DDMI recipe cycle";
        return RStat::Permanent;
    }  // a cycle can never be broken
    DdmiRecipe rec;
    std::string perr;
    if (!parseDdmi(b->second.data(), b->second.size(), rec, &perr)) {
        bad_[id] = true;
        refused_[id] = perr;
        return RStat::Permanent;
    }
    inProgress.insert(id);
    struct Guard {
        std::set<uint32_t>& s;
        uint32_t id;
        ~Guard() { s.erase(id); }
    } guard{inProgress, id};
    std::vector<const Icon*> ops;
    for (const DdmiRef& ref : rec.refs) {
        if (ddmiRefIsEmptyIcon(ref)) {
            ops.push_back(nullptr);
            continue;
        }
        if (ref.ordinal >= ordinals.size()) return RStat::Pending;  // the offer list GROWS mid-session
        const Icon* op = nullptr;
        switch (resolveOne(ordinals[ref.ordinal].first, ordinals, inProgress, op)) {
            case RStat::Pending: return RStat::Pending;
            case RStat::Permanent:
                bad_[id] = true;
                refused_[id] = "a DDMI operand is permanently unresolvable";
                return RStat::Permanent;
            case RStat::Ok: break;
        }
        ops.push_back(op);
    }
    DdmiResult r = evalDdmi(rec, ops);
    if (!r.icon()) {
        bad_[id] = true;
        refused_[id] = r.err;
        return RStat::Permanent;
    }
    if (r.owned) {
        recipeIcon_[id] = r.owned.get();
        recipeOwned_[id] = std::move(r.owned);
    } else
        recipeIcon_[id] = r.alias;
    out = recipeIcon_[id];
    return RStat::Ok;
}

const Icon* ResStore::iconResolved(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals) {
    std::set<uint32_t> inProgress;
    const Icon* out = nullptr;
    if (resolveOne(id, ordinals, inProgress, out) == RStat::Ok) return out;
    return nullptr;
}

std::string SceneStats::summary() const {
    std::ostringstream o;
    o << "layers=" << layers;
    auto f = [&](const char* n, int v) {
        if (v) o << " unmodeled:" << n << "=" << v;
    };
    f("maptext", maptext);
    f("animate_chain", animatedAtoms);
    f("images", images);
    if (masters) o << " masters=" << masters;
    if (unappliedFilters) o << " unapplied_filters=" << unappliedFilters;
    if (eyeScroll) o << " eye_scroll=1";
    return o.str();
}

void setSceneMutation(int m) {
    g_sceneMutation = m;
    if (m == 10 || m == 11 || m == 12 || m == 13 || m == 14 || m == 16 || m == 60) setPipelineMutation(m);
    if (m == 17 || m == 18) setDdmiMutation(m);
}

bool assembleMapControl(const model::Session& session, ResStore& res, int64_t clockNs, DrawList& out, SceneStats& stats,
                        std::string* err, bool withHit, std::string_view mapStyle, SceneCache* cache) {
    Assembler a(session, res, clockNs, stats, withHit, mapStyle, cache ? cache->impl_.get() : nullptr);
    return a.run(out, err);
}

bool assembleSecondaryMap(const model::Session& session, ResStore& res, int64_t clockNs, std::string_view mapName,
                          DrawList& out, SceneStats& stats, std::string* err, std::string_view mapStyle,
                          SceneCache* cache, bool withHit) {
    Assembler a(session, res, clockNs, stats, withHit, mapStyle, cache ? cache->impl_.get() : nullptr);
    return a.runSecondary(mapName, out, err);
}

bool appearanceIcon(const model::Session& session, ResStore& res, uint32_t appid, int dir, int& w, int& h,
                    std::vector<uint8_t>& rgba, int64_t clockNs, int64_t startNs) {
    SceneStats st;
    Assembler a(session, res, clockNs, st);
    return a.appearanceIcon(appid, dir, w, h, rgba, startNs);
}

bool statRowIcon(const model::Session& session, ResStore& res, uint32_t appid, int cell, int64_t clockNs,
                 std::vector<uint8_t>& rgba, int64_t startNs) {
    int w = 0, h = 0;
    std::vector<uint8_t> src;
    if (cell <= 0 || !appearanceIcon(session, res, appid, -1, w, h, src, clockNs, startNs)) return false;
    rgba.assign(size_t(cell) * size_t(cell) * 4, 0);
    if (w <= cell && h <= cell) {  // native size, top-left
        for (int y = 0; y < h; y++)
            std::memcpy(&rgba[size_t(y) * size_t(cell) * 4], &src[size_t(y) * size_t(w) * 4], size_t(w) * 4);
        return true;
    }
    // Shrunk into the cell: each destination pixel is the premultiplied average of the source pixels it covers. Uniform
    // scale by the larger side.
    int big = std::max(w, h);
    int dw = std::max(1, w * cell / big), dh = std::max(1, h * cell / big);
    for (int y = 0; y < dh; y++) {
        int y0 = y * h / dh, y1 = std::max(y0 + 1, (y + 1) * h / dh);
        for (int x = 0; x < dw; x++) {
            int x0 = x * w / dw, x1 = std::max(x0 + 1, (x + 1) * w / dw);
            uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++) {
                    const uint8_t* p = &src[(size_t(sy) * size_t(w) + size_t(sx)) * 4];
                    r += uint64_t(p[0]) * p[3];
                    g += uint64_t(p[1]) * p[3];
                    b += uint64_t(p[2]) * p[3];
                    a += p[3];
                    n++;
                }
            uint8_t* d = &rgba[(size_t(y) * size_t(cell) + size_t(x)) * 4];
            if (a == 0) continue;
            d[0] = uint8_t((r + a / 2) / a);
            d[1] = uint8_t((g + a / 2) / a);
            d[2] = uint8_t((b + a / 2) / a);
            d[3] = uint8_t((a + n / 2) / n);
        }
    }
    return true;
}

bool renderMapControl(const model::Session& session, ResStore& res, int64_t clockNs, L3Frame& out, std::string* err) {
    DrawList dl;
    out.stats = SceneStats{};
    if (!assembleMapControl(session, res, clockNs, dl, out.stats, err)) return false;
    out.img = execute(dl);
    return true;
}

}  // namespace fugue::render

// Scene assembly: the world model (model/World) + resources -> a DrawList (render/ir.h), and the entry point that
// executes it. What is NOT modelled is counted in SceneStats rather than silently dropped; a caller that compares against another
// renderer must check that the counters are zero for the scene.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "model/world.h"
#include "net/bytes.h"
#include "render/dmi.h"
#include "render/hittest.h"
#include "render/ir.h"

namespace fugue::render {

// Resources keyed by their VERIFIED resID (CRC-32/XFER over the bytes, net::resId). add() recomputes the
// id from the bytes, so a mislabelled file cannot lie; icons are parsed lazily and cached.
class ResStore {
public:
    // Files `bytes` under resId(bytes). If `claimedId` is non-null and differs, returns false and *err
    // names both ids (the caller refuses the file).
    bool add(ByteSpan bytes, const uint32_t* claimedId, std::string* err);
    size_t count() const { return bytes_.size(); }
    // The parsed icon for a resID, or nullptr (absent, or the bytes are not an icon the decoder accepts). A DDMI RECIPE is
    // not an icon the decoder accepts and answers nullptr here WITHOUT being marked bad: use iconResolved for those.
    const Icon* icon(uint32_t id);
    // The icon for a resource id, evaluating a DDMI recipe (render/ddmi.h) against the icons its refs name. `ordinals`
    // is the ResourceList, so a ref's ordinal -> resource id is ordinals[ordinal].first. nullptr when
    // the id is absent, a refused recipe (permanent: remembered, the atom is INVISIBLE), or a recipe whose operands
    // have not arrived yet (pending: retried on the next call, never remembered as bad).
    const Icon* iconResolved(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals);
    // Diagnostics: resources that can never draw (refused recipes AND icons that do not parse), with the reason; pending
    // recipes are not listed. An atom whose icon is here is INVISIBLE.
    const std::map<uint32_t, std::string>& refusedRecipes() const { return refused_; }
    // Resource ids a draw needed whose bytes have not arrived (plain icons and recipe operands), since the last call.
    // The session moves these to the FRONT of its declaration queue: what is on screen loads first.
    std::vector<uint32_t> takeWanted() {
        std::vector<uint32_t> v(wanted_.begin(), wanted_.end());
        wanted_.clear();
        return v;
    }
    const Bytes* bytesOf(uint32_t id) const {
        auto it = bytes_.find(id);
        return it == bytes_.end() ? nullptr : &it->second;
    }

private:
    enum class RStat { Ok, Pending, Permanent };
    RStat resolveOne(uint32_t id, const std::vector<std::pair<uint32_t, uint8_t>>& ordinals,
                     std::set<uint32_t>& inProgress, const Icon*& out);
    std::map<uint32_t, Bytes> bytes_;
    std::map<uint32_t, std::unique_ptr<Icon>> icons_;
    std::map<uint32_t, bool> bad_;
    std::map<uint32_t, std::unique_ptr<Icon>> recipeOwned_;  // evaluated recipes that built a new icon
    std::map<uint32_t, const Icon*> recipeIcon_;  // every resolved recipe (an owned icon, or its operand verbatim)
    std::map<uint32_t, std::string> refused_;
    std::set<uint32_t> wanted_;
};

// Appearance features the assembler does NOT model. Non-zero means the scene is outside what the renderer reproduces.
// render_source relays, colour matrices, blend modes, PLANE_MASTER screen objects and map glides are modelled and have
// no counter. `masters` and `unappliedFilters` are information, not gaps: the unbuilt filter types (alpha, color,
// displacement, ripple, wave, motion_blur, bloom, angular_blur, greyscale, layer, rays) are counted in `unappliedFilters`.
struct SceneStats {
    int maptext = 0;  // atom.maptext: drawn on the map (render/maptextpaint.h) with a stand-in face, but still counted
    int animatedAtoms = 0;     // HUD refs that are animating movables (drawn untweened here)
    int images = 0;            // client.images: drawn, but still counted
    int layers = 0;            // placed world layers (informational)
    int masters = 0;           // informational: plane masters collected from client.screen
    int unappliedFilters = 0;  // informational: master filters that are not built (counted, never invented)
    bool eyeScroll = false;    // an eye scroll is in flight at the render clock (modelled)
    uint64_t appearanceParses = 0, appearanceCacheHits = 0;  // distinct appearance lookups, excluding within-call reuse
    bool profilePreparation = false;                         // opt-in phase clocks; counters above are always available
    double appearanceBuildMs = 0;                            // JSON decoding and conversion to renderer fields
    bool anyUnmodeled() const { return maptext || animatedAtoms || images; }
    std::string summary() const;
};

// Optional, session-thread-owned cache of parsed appearance fields (NOT assembled scenes or resources).
// Exact JSON comparisons detect replacement/recycled IDs. Entry and payload limits bound retained history;
// defaults: 16384 appearances / 16 MiB accounted payload, excluding allocator/container overhead. A recorded vg round's
// working set is about 4100-4600 appearances and under 8 MiB; at 4096 entries the cache thrashed (51% hits).
class SceneCache {
public:
    struct Impl;
    explicit SceneCache(size_t maxBytes = 16 * 1024 * 1024, size_t maxEntries = 16384);
    ~SceneCache();
    SceneCache(const SceneCache&) = delete;
    SceneCache& operator=(const SceneCache&) = delete;
    void clear();
    size_t retainedBytes() const;
    size_t entries() const;

private:
    std::unique_ptr<Impl> impl_;
    friend bool assembleMapControl(const model::Session&, ResStore&, int64_t, DrawList&, SceneStats&, std::string*,
                                   bool, std::string_view, SceneCache*);
    friend bool assembleSecondaryMap(const model::Session&, ResStore&, int64_t, std::string_view, DrawList&,
                                     SceneStats&, std::string*, std::string_view, SceneCache*, bool);
};

struct L3Frame {
    RgbaImage img;  // straight-alpha RGBA8, alpha 255 everywhere
    SceneStats stats;
    int tileW = 32, tileH = 32;
};

// Assembles the draw list for the map control as it looks at `clockNs` (Unix ns; only multi-frame icon
// states are time dependent). false + *err when the frame cannot be built (no bounds / no map yet, an icon state with an
// unsupported dir count, a cell outside its sheet, ...).
// withHit=true also fills out.hit (render/hittest.h) from the very layers it places.
// mapStyle is the default map control's `style` param (the skin's), the sheet maptext is laid out against - the same
// one MeasureText merges in (render/dsmaptext.h). Empty: DreamSeeker's bare defaults.
bool assembleMapControl(const model::Session& session, ResStore& res, int64_t clockNs, DrawList& out, SceneStats& stats,
                        std::string* err, bool withHit = false, std::string_view mapStyle = {},
                        SceneCache* cache = nullptr);

// A secondary map control by its id (a runtime `winset parent=... type=map` control, such as tgui's ByondUi): the
// client.screen objects whose screen_loc names `mapName` ("name:x,y") and their vis_contents, under the plane masters
// assigned to that map. The list covers the objects' bounding box at the world's icon size; the map control scales it to
// fit (shell/mapwidget.h). withHit fills out.hit, a HitScene with `secondary` set (render/hittest.h).
bool assembleSecondaryMap(const model::Session& session, ResStore& res, int64_t clockNs, std::string_view mapName,
                          DrawList& out, SceneStats& stats, std::string* err, std::string_view mapStyle = {},
                          SceneCache* cache = nullptr, bool withHit = false);

// The right-click menu's icon for one atom (HitAtom::appid / dir; dir -1 = the appearance's own facing): straight RGBA at
// the size of the atom's icon. false when the appearance or every icon in it is not here yet (the row then has no icon).
// Stock DreamSeeker 516 draws the raw sprite here. atom.color (scalar or matrix), an inherited parent
// colour, an overlay's own colour, atom.alpha, atom.transform and the atom's own pixel_x/y are all ignored, while the map
// draws every one of them. Overlays are drawn over the atom's sprite, with their own pixel_x/y ignored too. An atom with
// icon = null still shows its overlays, and a carried client.screen item gets its icon. DreamSeeker shows it at 16x16, as
// Qt's QMenu does here. The facing is the map's, and blend_mode is kept.
// clockNs picks an animated state's frame (0 = its first, the menu's use); a finite-loop state plays from startNs when set.
bool appearanceIcon(const model::Session& session, ResStore& res, uint32_t appid, int dir, int& w, int& h,
                    std::vector<uint8_t>& rgba, int64_t clockNs = 0, int64_t startNs = 0);

// The INFO control's (stat panel's) icon for an atom row (a StatPanel value naming an appearance): straight RGBA, cell
// x cell. Stock DreamSeeker 516.1684 draws a statpanel row with the same raw sprite as the menu icon (appearanceIcon):
// atom.color and alpha are ignored, pixel_x is ignored, overlays are drawn, and icon = null with an overlay draws the
// overlay alone in the cell. The appearance's dir is used (dir = EAST draws the east frame). An atom in NULLSPACE draws
// the same way, though it is never on the map. The row animates: a two-frame icon_state cycles (clockNs), a finite loop
// plays once from startNs (model::World::StatLine::icon). The cell is the world's icon size (a world with icon_size 16
// shrinks a 32x32 icon to 16x16). An icon no bigger than the cell sits at its top-left at native size; a bigger one is
// shrunk to the cell by averaging (64x64 to 32x32). A non-square icon, or one larger on one axis only, is scaled
// uniformly by the larger side here. false = nothing to draw (no icon and no overlay, or not loaded yet): stock draws
// the name alone, in the cell's place.
bool statRowIcon(const model::Session& session, ResStore& res, uint32_t appid, int cell, int64_t clockNs,
                 std::vector<uint8_t>& rgba, int64_t startNs = 0);

// assembleMapControl + execute.
bool renderMapControl(const model::Session& session, ResStore& res, int64_t clockNs, L3Frame& out, std::string* err);

// Test-only hook; 0 is the shipped behaviour.
void setSceneMutation(int m);

}  // namespace fugue::render

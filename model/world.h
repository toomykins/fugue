// World model: a single-writer value type, fed frame by frame through Session::ingest. Frames are decoded by the
// proto/ decoders into the structs of proto/messages.h and applied per handler in world.cpp.
//
// snapshot() is the canonical JSON of the world.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/particles.h"
#include "net/bytes.h"
#include "proto/registry.h"

namespace fugue::model {

// A glide in flight: the drawn offset from the destination tile at startNs, in tiles (+x east, +y north), shrinking
// along its own direction to zero at startNs + durNs. A move that lands mid-glide re-arms from the offset still left
// (see armGlideOffset), so the unfinished part carries over instead of snapping. See glideMotion.
struct GlideMotion {
    double sx = 0, sy = 0;
    int64_t startNs = 0, durNs = 0;
    double rate = 0;         // base speed along the distance, tiles per ns
    bool chebyshev = false;  // LONG_GLIDE: the distance is max(|x|,|y|), else Euclidean
    bool catchUp = false;    // faster while more than a tile behind (see glideMotion)
};

// One in-view movable. All fields default to zero (what an A1 / ContentsDelta add inserts).
struct Atom {
    uint32_t appid = 0;
    int x = 0, y = 0, z = 0;
    bool hasPos = false;
    int dir = 0;
    int16_t pixelX = 0, pixelY = 0, pixelW = 0, pixelZ = 0;
    int16_t boundX = 0, boundY = 0, boundW = 0, boundH = 0;
    bool hasBounds = false;
    int16_t stepX = 0, stepY = 0;
    uint32_t stepSizeBits = 0;  // float32 bits
    bool hasStep = false;
    int visCount = 0;
    bool gliding = false;    // transient: armed by a one-tile Pos (presence only)
    bool animating = false;  // transient: an animate() chain was recorded (never cleared by time)
    // Record identity and bind, as every WorldState/InView record carries them. Not in the snapshot. A record arriving
    // for a held slot with a different Key is a different atom; the stock client tears the old record down first.
    uint16_t key = 0;
    bool haveKey = false;
    uint32_t bindOther = 0xffff;  // A3 Bind.Other; 0xffff = none (the create default), cleared by an A1
    uint32_t backlink = 0xffff;   // a record whose Bind names this one, as of the last relink
    uint64_t touchTick = 0;       // ResyncEvents tick when a record array last named it
    bool pollAnswerDue = false;   // mob only: polled as a new record, the server's MobBind not yet in

    // Render state behind the two transient flags above (not in the snapshot: wall-clock based).
    // Times are Unix ns on the ingest clock (Session::setIngestClockNs).
    using Glide = GlideMotion;
    // flags/loop/startNs: the step's Animate flags (0x4 ANIMATION_PARALLEL, 0x40000000 a chain head), its loop count,
    // and, for a step that opens a sequence, when it starts (0 = it follows the step before it).
    struct AnimStep {
        uint32_t from = 0, to = 0;
        uint32_t timeBits = 0;
        uint8_t easing = 0;
        uint32_t flags = 0;
        int32_t loop = 1;
        int64_t startNs = 0;
    };
    struct Chain {
        int64_t startNs = 0, tickNs = 0;
        std::vector<AnimStep> steps;
    };
    Glide glide;             // valid iff gliding
    uint8_t glideDir = 0;    // the direction (DM dir bits) of the move that last armed a glide
    float autoGlidePx = 0;   // the automatic glide rate (LEGACY movement mode), px per tick; 0 = not yet derived
    int64_t autoDoneNs = 0;  // when the last glide played out (0 = none yet)
    Chain chain;             // valid iff animating
    // When a non-looping icon state's frames start (Unix ns; 0 = never bound).
    int64_t iconStartNs = 0;
};

// Atom::iconStartNs for what is not an Atom.
struct IconStart {
    uint32_t appid = 0;
    int64_t ns = 0;
};

// An appearance: the decoded record, and the resync tick of its last S2C::Appearance (model/appgc.h).
struct Appearance {
    proto::Appearance fields;
    uint64_t namedTick = 0;
};

// Bounds of the world (Bounds), raw and unclamped.
struct Bounds {
    bool have = false;
    int maxX = 0, maxY = 0, view = 0, winW = 0, winH = 0, viewW = 0, viewH = 0;
};

struct SessionEnd {
    bool seen = false;
    uint16_t reason = 0;
    uint32_t version = 0;
    std::string text;
};

struct MapGrid {  // the single-frame MapGrid (Cells() fallback path)
    bool have = false;
    uint16_t x0 = 0, y0 = 0, z = 0;
    std::vector<uint32_t> cells;
};

struct World {
    // Meta, eye and self.
    std::string name;
    Bounds bounds;
    int64_t tickNanos = 0;   // 0 = never set -> DefaultTickNanos
    int64_t frameNanos = 0;  // the client's frame (1 / client.fps, else the tick); 0 = never set
    int mapViewErrs = 0, cellRefreshErrs = 0;
    uint32_t clientAppid = 0;
    bool haveClientAppid = false;
    std::string chatControl, hostMessage;
    SessionEnd sessionEnd;

    bool haveEye = false;
    int eyeX = 0, eyeY = 0;
    uint32_t eyeGlideSizeBits = 0, stepSizeBits = 0;
    bool eyeLongGlide = false;
    uint8_t seeInvisible = 0;
    bool haveSeeInvisible = false;
    uint32_t sight = 0;
    bool haveSight = false;
    // Per-axis eye tracker (WorldState carries one axis at a time).
    uint16_t trkX = 0, trkY = 0;
    bool seenX = false, seenY = false;
    // The view's scroll: one vector for both axes, re-armed by a move like an atom's glide. See eyeScrollOffset.
    bool eyeGliding = false;
    GlideMotion eyeGlide;
    uint8_t eyeGlideDir = 0;
    float eyeAutoPx = 0;
    int64_t eyeAutoDoneNs = 0;
    // world.movement_mode, from MouseSubs's second FormatFlags word (bits 2-3; 0 = LEGACY): it picks the glide re-arm
    // rule.
    uint8_t movementMode = 0;

    // client.pixel_x/y/w/z, from WorldState: the map view is offset by (x + w, y + z) world pixels; the HUD is not.
    int16_t viewPixelX = 0, viewPixelY = 0, viewPixelW = 0, viewPixelZ = 0;
    // animate(client, ...): S2C::Animate Kind 5. Its From/To are the client's own appearances, carrying the pixel
    // offsets. Chain != 0 replaces it (Count = 0 clears it), Chain = 0 appends. Transient: not in the snapshot.
    Atom::Chain clientChain;

    bool haveSelf = false;
    uint32_t selfRef = 0;

    // Tables.
    std::map<uint32_t, Appearance> appearances;
    std::map<uint32_t, Atom> atoms;  // key = id | registryA bit
    // Each tile's cell lists as the last relink left them (model/resync.cpp): its placed mobs and its placed objs, bare
    // ids, in the stock client's order. Key {x, y, z} as Atom carries them. A typed verb's tie-break reads them (srcRank).
    std::map<std::array<int, 3>, std::vector<uint32_t>> cellMobs, cellObjs;
    // flick(): the live one-shot per obj, from FlickState, keyed by its AtomLoc.ref (the obj's atoms key). startNs =
    // its ingest clock.
    struct Flick {
        std::string iconState;
        uint32_t iconResIndex = 0;
        bool haveRes = false;
        int64_t startNs = 0;
    };
    std::map<uint32_t, Flick> flicks;
    std::map<uint32_t, std::vector<uint32_t>> visContents;
    // vis_contents items that are TURFS (item Type 1), per holder key: the turf's flat cell index (x + y*maxx +
    // z*maxx*maxy, all 0-based). The turfs' looks arrive in VisTurfs (into `tiles`); what stands on them, as ordinary
    // InView objects. Drawn only on a secondary map (render/scene.h assembleSecondaryMap). Kept apart from visContents.
    std::map<uint32_t, std::vector<uint32_t>> visTurfs;
    // An atom's particle set (its `particles`), per atom key, from the WorldState/InView particle array. Mutable: the
    // assembler steps each set it draws on the render clock (advanceParticles). Not in the snapshot.
    mutable std::map<uint32_t, ParticleSet> particles;
    std::map<uint32_t, uint32_t> refBind, turfBinds;
    // usr.contents as stock DreamSeeker tracks it, from the ContentsDelta arrays: key = obj id, or mob id | kRegistryA;
    // value = the pair's appid. An obj entering arrives as an array-2 [id][appid] pair; leaving for another mob, as an
    // array-1 [id]. A mob entering arrives as an array-4 pair, and leaving as an array-3 [id], which may come late in a
    // later ContentsDelta. A del() of a carried obj is an array-1 removal. The floor (InView) never enters the set.
    // Only these ids are admitted by `set src in usr` (verbSrcCandidates). A client.mob switch to a new mob clears the
    // set: that is safe only because the server sends SelfRef before the new mob's array-2 pairs (world.cpp case
    // SelfRef).
    std::map<uint32_t, uint32_t> usrContents;
    std::vector<uint32_t> usrContentsOrder;  // the same keys in arrival order (a re-entry keeps its place)
    // The AreaGrid: the AREA index of each cell, key (x,y,z) 0-based wire. Stock DreamSeeker sends an area verb as
    // src [04][index of the player's cell]. turfBinds above is the same list.
    std::map<std::array<uint16_t, 3>, uint32_t> areaCells;
    // Appearances whose layer is 2.0 (the default-turf rule, verbmenu.cpp defaultTurfAppid), filled at ingest
    // (Appearance). layer2Indexed false means a hand-built World, which is scanned instead.
    std::set<uint32_t> layer2Appids;
    bool layer2Indexed = false;
    std::map<std::array<uint16_t, 3>, uint32_t> tiles;           // cumulative map, key (x,y,z) 0-based wire
    std::map<std::array<uint16_t, 3>, IconStart> tileIconStart;  // per `tiles` cell
    // A turf's vis_contents (VisContents tag 1), per `tiles` cell: atom keys (obj id bare, mob id | kRegistryA). vg's
    // gas overlays are shared nullspace objs held here.
    std::map<std::array<uint16_t, 3>, std::vector<uint32_t>> turfVis;
    MapGrid grid;

    // Remaining sections. Decoder-owned records are kept as decoded.
    std::map<uint32_t, proto::ImageOp> images;     // client.images, key = imageID
    std::map<uint32_t, IconStart> imageIconStart;  // per `images` id
    std::set<uint32_t> imageAnims;                 // keys of the image animation chains (existence only)
    // The image animation chains themselves (the snapshot prints only the keys above). Stamped at ingest like
    // Atom::chain and sampled by render/scene.cpp, so a client.image fades and slides. Transient: not in the snapshot.
    std::map<uint32_t, Atom::Chain> imageChains;
    std::map<uint32_t, proto::Hud::Record> hud;  // client.screen, key = ref
    std::map<uint32_t, uint32_t> hudAppid;       // hud's appid per ref, filled at ingest (Hud) for verbmenu
    std::vector<uint32_t> hudOrder;
    std::map<uint32_t, IconStart> hudIconStart;  // per `hud` ref, kept while the ref is off the screen
    std::map<uint32_t, proto::Verb> verbById;
    std::vector<uint32_t> verbOrder;

    struct StatLine {
        uint16_t ordinal = 0;
        std::string name;
        proto::StatValue value;
        IconStart icon;
    };
    struct Stat {
        std::vector<std::string> tabs;
        int selected = 0;
        uint8_t token = 0;  // only ever set by SelectingTab (client-to-server side)
        bool owns =
            false;  // the token above was chosen by this client (statpanel.h selectStatTab); the gate only bites then
        int frames = 0, errors = 0, stale = 0;
        std::vector<StatLine> lines;
    } stat;

    struct Browser {
        std::string params, html;
    };
    std::map<std::string, Browser> browsers;       // open browse windows (Browse), key = window name
    std::vector<std::string> browserOrder;         // arrival order
    std::map<std::string, uint64_t> browserCalls;  // page-JS call counts, never reset by a close

    std::vector<std::pair<uint32_t, uint8_t>> resources;  // ResourceList availability list, arrival order

    // Filled only by noteResource(): file bytes reduced to {len, sha256} (the snapshot never embeds bytes), keyed by the
    // verified resID. Only the verified key is filed.
    struct RecvRes {
        uint64_t len = 0;
        std::string sha;
    };
    std::map<uint32_t, RecvRes> received;
    std::map<uint32_t, std::string> resNames;

    std::string skinDmf;
    uint32_t skinResID = 0xFFFFFFFFu;  // no skin resource
    int skinSeq = 0;

    std::map<std::string, std::string> controls;  // winset-applied props, key = control

    std::vector<std::string> chat, chatHtml;  // scrollback rings (plain / markup), capped at kChatCap
    uint64_t chatTotal = 0;

    // Inputs to the atom-table resync poll (model/resync.h): written by ingest, drained by ResyncPoller, not in the
    // snapshot. teardowns counts records freed (a Key change on a held slot, S2C::MobGone/ObjGone); each arms the
    // sweep. newMobs/newObjs are ids just built and still marked; the client asks the server about each (C2S::ResyncMob
    // / ResyncObj). relinkObjs/relinkMobs arm the rebuild of the cell lists (World::cellMobs / cellObjs) and back-links
    // after a WorldState/InView parse.
    struct ResyncEvents {
        uint64_t teardowns = 0;
        std::set<uint32_t> newMobs, newObjs;
        bool relinkObjs = false, relinkMobs = false;
        bool haveOrigin = false;
        uint16_t originX = 0, originY = 0;  // the last MapRefreshFull / MapRefresh / MapGrid window origin
        uint64_t tick = 0;                  // the poller's client-tick count
        uint64_t appearances =
            0;  // S2C::Appearance frames; each builds an appearance record (the stock GC's 500 trigger)
        // Records the stock client's GC would have freed (model/resync.h). The state is kept, since the server does not
        // know they were freed and sends no fresh copy; the next record naming one is built anew: marked, unbound,
        // with no back-link.
        std::set<uint32_t> freed;
    } resync;
};

constexpr size_t kChatCap = 500;
constexpr size_t kMaxStatLines = 4096;

constexpr uint32_t kRegistryA = 0x80000000u;
constexpr int64_t kDefaultTickNanos = 100'000'000;
constexpr int kGlideThresholdTiles = 1;
constexpr int kDefaultIconSize = 32;
// The world's icon size: the eye's step_size, since a movable's default step_size is world.icon_size.
// Falls back to kDefaultIconSize.
int iconSizeOf(const World& w);

// The glide rate of a glide_size of 0, px per tick.
constexpr float kAutoGlidePx = 4;
// Glide duration: distance in px over the glide px-per-tick, times the tick, truncated to whole ns.
// glidePx <= 0 means kAutoGlidePx.
int64_t glideDurationNs(int dx, int dy, float glidePx, int iconSize, int64_t tickNs, bool longGlide);
// Arms a glide from offset (sx, sy) tiles at glidePx per tick. With catchUp, while more than one tile is left on an axis
// the speed is that rate times (tiles left on the longer axis + 1/16), so a glide re-armed before it finishes catches up
// instead of falling ever further behind.
GlideMotion glideMotion(double sx, double sy, float glidePx, int iconSize, int64_t tickNs, bool longGlide,
                        int64_t startNs, bool catchUp);
// The fraction of the armed offset still left at nowNs: 1 up to the start, 0 once played out.
double glideLeft(const GlideMotion& g, int64_t nowNs);
// The offset a one-tile move (mx, my = from - to, tiles) re-arms a glide with, given the glide in flight (null: none)
// and the direction of the move that armed it, which is updated. false: the sprite snaps instead.
bool armGlideOffset(const World& w, const GlideMotion* inFlight, int64_t nowNs, int mx, int my, uint8_t& lastDir,
                    double& sx, double& sy);
// The rate a glide_size-0 atom glides at after a move. TILE or PIXEL mode: always kAutoGlidePx. LEGACY mode: a move
// landing while the sprite is still gliding the way it moves adds 2 px per tick (to icon/2); one landing at rest
// derives it from the previous rate and the ticks since the last glide played out. prev/doneNs are the atom's
// autoGlidePx/autoDoneNs; the result is the rate to hand to glideMotion.
float autoGlideRate(const World& w, float prev, int64_t doneNs, const GlideMotion* inFlight, int64_t nowNs, int mx,
                    int my);
// Whole tiles of offset on either axis that a glide never exceeds.
constexpr double kGlideMaxTiles = 2;
// The sub-tile scroll, in image pixels, that the map layers are shifted by while an eye glide is in flight: +x draws
// map content right, +y draws it down (image space). Zero with no glide or once the glide has played out. It starts
// at the scroll the move left (a whole tile from rest, undoing the snap of the view origin) and slides to zero.
void eyeScrollOffset(const World& w, int64_t nowNs, int& sx, int& sy);
// One sequence of an animate() chain: the step that opens it and the steps that follow on, run in turn. Sequences run
// concurrently, each from its own start; `loop` repeats the whole sequence (< 1 = forever, endNs = -1).
struct AnimSeq {
    size_t first = 0, last = 0;
    int64_t startNs = 0, durNs = 0, endNs = 0;
    bool head = false, parallel = false;
};
std::vector<AnimSeq> animSequences(const Atom::Chain& c);
// Selects an alternative rule for test controls; 0 is the shipped behaviour.
void setModelMutation(int m);
// Test control: a glide_size of 0 glides at the icon size per tick, as the Go model does.
void setGoAutoGlide(bool on);

// Sections the snapshot does not model, in schema order. Empty: every section is modelled.
const std::vector<std::string>& unimplementedSections();

struct CellGrid {
    bool err = true;
    int w = 0, h = 0, originX = 0, originY = 0;
    int viewZ = 1;       // the refresh grid's z (1-based) when in use, else 1
    bool haveZ = false;  // viewZ is the refresh grid's
    std::vector<uint32_t> appids;
};

class Session {
public:
    Session() = default;
    // The instant "now" for time-stamped state armed by ingested frames (a glide's start, an animate() chain's start).
    // The default is far in the future, which pins every glide and chain at its start for any render clock below it.
    // A test that scripts the clock sets it before each frame.
    void setIngestClockNs(int64_t ns) { ingestNs_ = ns; }
    int64_t ingestClockNs() const { return ingestNs_; }
    // Feeds one frame. Never throws: a decoder failure drops the frame. Resource must NOT be fed.
    void ingest(uint16_t typ, ByteSpan body);
    // Resource availability: call once per file the resource pump
    // has verified and stored. Idempotent per id. Same single-writer rule as ingest().
    void noteResource(uint32_t id, ByteSpan data, const std::string& name);

    // The canonical JSON of the "world" object.
    std::string snapshot() const;
    // The same sections, each as its own JSON text, in schema order.
    std::vector<std::pair<std::string, std::string>> sections() const;
    // The "cells" section as data: the viewport window of turf appids, row-major from the bottom-left origin
    // (1-based world coords). err = no bounds or no grid yet (then w = h = 0 and appids is empty).
    CellGrid cells() const;

    const World& world() const { return w_; }
    // The single writer's handle for client-side state changes (statpanel.h selectStatTab). Session thread only.
    World& worldMut() { return w_; }
    int ingestErrors() const { return decodeErrors_; }  // frames a decoder rejected (informational)
    // output() text addressed to a bare control name ("win.ctl", no ":function") is chat only when this answers true
    // for it; stock DreamSeeker prints nothing for a browser control or a control that does not exist. Unset: chat.
    void setTextTarget(std::function<bool(const std::string& control)> f) { textTarget_ = std::move(f); }

private:
    void applyObjectArrays(const proto::ObjectArrays& m, uint32_t reg);
    Atom* record(uint32_t id, uint16_t key, bool fresh);                 // get-or-create by {id, Key} (world.cpp)
    void bindAppearance(Atom& a, uint32_t appid, uint32_t id = 0);       // sets appid, stamping Atom::iconStartNs
    void stampIcon(int64_t& startNs, uint32_t from, uint32_t to) const;  // Atom::iconStartNs's rule
    void stampIcon(IconStart& s, uint32_t appid) const {
        stampIcon(s.ns, s.appid, appid);
        s.appid = appid;
    }
    void setTile(const std::array<uint16_t, 3>& key, uint32_t appid);
    void addChat(const std::string& plain, const std::string& markup);

    World w_;
    mutable proto::State st_;  // snapshot() reads the map-refresh machine through get<>()
    int decodeErrors_ = 0;
    int64_t ingestNs_ = (INT64_MAX / 4);
    std::function<bool(const std::string&)> textTarget_;
};

}  // namespace fugue::model

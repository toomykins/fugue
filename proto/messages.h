// Decoded server-to-client messages, one struct per message, fields in wire order. The wire layout of each is written
// at the top of its decoder (proto/dec_s2c_<type>_<name>.cpp), which defines decode<Name>() and toJson().
//
// decode<Name>() throws DecodeError (or another std::exception) for a malformed body. toJson() renders a decoded message
// as its canonical JSON, the form the decoder registry (proto/registry.h), the development tools and the parity tests use;
// the client itself reads the structs.
//
// Floats are kept as their IEEE-754 bits (the *Bits fields): the model compares and re-times with the exact values sent.
// Strings are the raw wire bytes: text-macro markers and non-UTF-8 bytes are kept.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "net/bytes.h"
#include "proto/reader.h"
#include "tools/canon_json/canon_json.h"

namespace fugue::proto {

class State;

// ---- shared pieces

// A record's identity as WorldState and InView carry it: the slot id and its Key.
struct RecordRef {
    uint32_t id = 0;
    uint16_t key = 0;
    bool operator==(const RecordRef&) const = default;
};

// One vis_contents item: Type 1 turf (a flat cell index), 2 obj, 3 mob, and the id.
struct VisItem {
    uint8_t type = 0;
    uint32_t value = 0;
    bool operator==(const VisItem&) const = default;
};

// An atom named by kind and ref (FlickState, FlickTarget).
struct AtomLoc {
    uint8_t kind = 0;
    uint32_t ref = 0;
    uint16_t aux = 0;
};

// One particle generator: [type u8], then unless 0xff [len u16][rand u8] and a body keyed on type & 0x7f. The high bit
// marks a constant (one operand). Types: 0 num, 1 vector (one point on the line a..b), 0x20 box, 0x21 circle/sphere/
// square/cube (sub 0..3, radius a..b), 2 colour, 3 colour matrix, 4 an operation on two generators, 5 weighted icon
// list, 6 weighted string list. Floats are the f32 values as sent.
struct ParticleGenerator {
    uint8_t type = 0xff;                              // 0xff = none
    uint8_t rand = 0;                                 // UNIFORM_RAND, NORMAL_RAND, LINEAR_RAND, SQUARE_RAND
    uint16_t sub = 0;                                 // 0x21: the shape; 4: the operator
    std::vector<float> a, b;                          // operands (1, 3 or 20 floats); b is empty for a constant
    std::vector<uint32_t> colors;                     // type 2: the operand colours, RGBA bytes packed little-endian
    std::vector<std::pair<uint32_t, float>> ids;      // type 5: (resource ordinal, weight)
    std::vector<std::pair<std::string, float>> strs;  // type 6: (string, weight)
    std::vector<ParticleGenerator> kids;              // type 4: the two operands
    bool present() const { return type != 0xff; }
};

// A particle record's fixed block and generators, decoded. The fixed block is [w u16][h u16][count u16][spawning f32]
// [bound1 3f][bound2 3f][gravity 3f][n u8][n f32 transform][stops u16], then, when stops > 0, the stops (pos f32,
// rgba u32) and [loop u8][space u8]. Bytes after it are not read.
struct ParticleDef {
    struct Stop {
        float pos;
        uint32_t rgba;
    };
    uint16_t width = 0, height = 0, count = 0;
    float spawning = 0;
    std::array<float, 3> bound1{}, bound2{}, gravity{};
    std::vector<float> transform;  // the n cells as sent
    std::vector<Stop> gradient;    // in wire order
    bool gradientLoop = false;     // sent only with a gradient
    uint8_t gradientSpace = 0;
    // The generators by key, in arrival order; one of an unknown kind reads as none.
    std::vector<std::pair<uint16_t, ParticleGenerator>> gens;
};

// The ten counted object arrays WorldState and InView share (proto/g3_inview.cpp), in wire order.
struct ObjectArrays {
    struct Pos {
        RecordRef rec;
        uint16_t x = 0, y = 0, z = 0;  // 0-based; x = y = 0xffff is the off-view sentinel
    };
    struct Bind {
        RecordRef rec;
        uint32_t other = 0;
    };
    struct Dir {
        RecordRef rec;
        uint8_t dir = 0;
    };
    struct AppearanceBind {
        RecordRef rec;
        uint32_t appearance = 0;
    };
    struct Pixel {
        RecordRef rec;
        int16_t x = 0, y = 0, w = 0, z = 0;
    };
    struct Bounds {
        RecordRef rec;
        int16_t x = 0, y = 0, w = 0, h = 0;
    };
    struct Step {
        RecordRef rec;
        int16_t x = 0, y = 0;
        uint32_t sizeBits = 0;  // step_size, f32 bits
    };
    struct VisContents {
        RecordRef rec;
        std::vector<VisItem> items;
    };
    struct ParticleGen {
        uint16_t key = 0;
        uint8_t type = 0;  // 0xff: no payload
        Bytes payload;
    };
    struct Particles {
        RecordRef rec;
        uint32_t particleID = 0;  // 0xffff detaches the set (no fixed block, no generators)
        Bytes fixed;
        std::vector<ParticleGen> gens;
        std::optional<ParticleDef> def;  // fixed and gens decoded; nullopt when either does not parse
    };
    std::vector<RecordRef> created;  // array 1: records built (or retracted) this frame
    std::vector<Pos> pos;
    std::vector<Bind> bind;
    std::vector<Dir> dir;
    std::vector<AppearanceBind> appearance;
    std::vector<Pixel> pixel;  // this and the arrays below are optional on the wire: absent = empty
    std::vector<Bounds> bounds;
    std::vector<Step> step;
    std::vector<VisContents> visContents;
    std::vector<Particles> particles;
    bool hasTail = false;
    uint8_t tail = 0;
};

// ---- the messages, by type number

struct SessionEnd {  // S2C::SessionEnd
    uint16_t reason = 0;
    uint32_t requiredVersion = 0;
};

struct WorldState {  // S2C::WorldState
    struct Eye {
        uint8_t kind = 0, eyeType = 0;
        uint32_t eyeValue = 0;
        uint8_t viewMode = 0;
        uint32_t flags32 = 0, flags32b = 0;  // flags32 >> 21 is the eye's appearance_flags
        uint32_t glideSizeBits = 0, stepSizeBits = 0;
        uint16_t viewX = 0, viewY = 0;
    };
    struct Vision {
        uint8_t sub = 0;  // which of the fields below were sent
        uint8_t seeInDark = 0, seeInvisible = 0;
        uint32_t sightWide = 0;
        std::array<uint16_t, 4> rect{};          // sub & 0x08
        std::array<uint16_t, 2> viewOrigin{};    // sub & 0x10: client.pixel_x/y
        std::array<uint16_t, 4> screenOffset{};  // sub & 0x20: client.pixel_x/y/w/z
        std::array<uint16_t, 4> mapBounds{};     // sub & 0x40
    };
    uint8_t flags = 0;
    bool hasX = false;
    uint16_t mapX = 0, eyeX = 0;
    bool hasY = false;
    uint16_t mapY = 0, eyeY = 0;
    std::optional<Eye> eye;  // present when either axis is
    bool hasSight = false;
    uint8_t sight = 0;
    Bytes dmValue;
    std::optional<Vision> vision;
    ObjectArrays objects;
};

struct InView {  // S2C::InView
    uint8_t flags = 0;
    bool hasExtra1 = false, hasExtra2 = false;
    uint16_t extra1 = 0, extra2 = 0;
    ObjectArrays objects;
};

struct RefBind {  // S2C::MobBind, S2C::ObjBind
    uint32_t ref = 0, appid = 0;
};

struct ContentsDelta {  // S2C::ContentsDelta
    struct Pair {
        uint32_t id = 0, appearance = 0;
    };
    std::vector<uint32_t> addA;     // array 1: objs leaving usr.contents
    std::vector<Pair> bindA;        // array 2: objs entering it
    std::vector<uint32_t> removeB;  // array 3: mobs leaving it
    std::vector<Pair> bindB;        // array 4: mobs entering it
};

struct ResourceList {  // S2C::ResourceList
    struct Entry {
        uint32_t id = 0;
        uint8_t type = 0;
    };
    std::vector<Entry> entries;
};

struct Verb {  // S2C::Verb
    struct Arg {
        uint32_t typeMask = 0;
        uint8_t filter = 0, filterKind = 0;
    };
    uint32_t id = 0;  // u16 on the wire unless the handshake negotiated wide verb ids
    std::string name, desc, category;
    uint8_t head = 0, src = 0, flags = 0;
    uint32_t flagsExt = 0;  // flags & 0x80
    uint8_t extByte = 0;
    std::vector<Arg> args;
};

struct MapRefresh {  // S2C::MapRefreshFull, S2C::MapRefresh, and the refresh half of S2C::MapGrid
    struct Cell {
        uint16_t x = 0, y = 0;  // 0-based
        uint32_t appid = 0;
    };
    uint16_t originX = 0, originY = 0, z = 0;
    bool full = false;
    std::vector<Cell> cells;
};

struct AreaGrid {  // S2C::AreaGrid
    struct Cell {
        uint16_t x = 0, y = 0;
        uint32_t index = 0;
    };
    struct Bind {
        uint32_t index = 0, appid = 0;
    };
    std::vector<Cell> cells;  // on the z of the refresh that armed it
    std::vector<Bind> binds;
};

struct Appearance {  // S2C::Appearance
    struct Slot {
        uint8_t tag = 0;
        bool has = false;  // tag == 2: an id follows
        uint32_t id = 0;
        bool operator==(const Slot&) const = default;
    };
    struct Var {  // one vars_mask payload, as sent
        uint32_t bit = 0;
        Bytes data;
        bool operator==(const Var&) const = default;
    };
    struct Filter {
        uint8_t type = 0, flags = 0;
        uint32_t id = 0, paramMask = 0;
        Bytes params;  // the payloads of the set paramMask bits, as sent
        std::string str;
        bool operator==(const Filter&) const = default;
    };
    uint32_t appid = 0;
    std::string name, suffix;
    uint32_t iconResID = 0xFFFFFFFFu;  // a resource ordinal; 0xFFFFFFFF = no icon
    std::string iconState, text, maptext;
    uint16_t maptextW = 0, maptextH = 0;
    int16_t maptextX = 0, maptextY = 0;
    uint8_t dir = 0;
    uint32_t appearanceFlags = 0;  // low byte: client flags; >> 21: DM appearance_flags
    std::array<Slot, 3> idSlots{};
    uint32_t iconRef = 0;
    bool wideVerbIds = false;  // the width the verbs were sent in
    std::vector<uint32_t> verbs;
    std::vector<uint32_t> overlays, underlays;
    uint32_t layerBits = 0;
    std::string screenLoc;
    uint32_t varsMask = 0;
    std::vector<Var> vars;  // every payload-carrying set bit, ascending
    // Decoded from vars: absent ones keep these defaults.
    std::vector<Filter> filters;             // 0x800
    std::string renderSource, renderTarget;  // 0x1000
    bool hasColor = false;                   // 0x40: r, g, b, a
    std::array<uint8_t, 4> color{};
    bool hasPlane = false;  // 0x200
    int16_t plane = 0;
    bool hasPixelOffsets = false;  // 0x8: pixel_x, pixel_y, pixel_w, pixel_z
    int16_t pixelX = 0, pixelY = 0, pixelW = 0, pixelZ = 0;
    bool hasGlideSize = false;  // 0x10
    uint32_t glideSizeBits = 0;
    bool hasTransform = false;  // 0x20: a, b, c, d, e, f
    std::array<uint32_t, 6> transformBits{};
    bool hasInvisibility = false;  // 0x4
    uint8_t invisibility = 0;
    bool hasBlend = false;  // 0x100: blend_mode as sent
    uint8_t blend = 0;
    bool hasColorMatrix = false;  // 0x400: the 20 cells, row by row
    std::array<uint32_t, 20> colorMatrixBits{};

    bool longGlide() const { return ((appearanceFlags >> 21) & 1) != 0; }  // DM appearance_flags LONG_GLIDE
    // animate_movement NO_STEPS: bits 14-15 of the flags word hold the var rotated (FORWARD 0, SLIDE 1, SYNC 2, NO 3).
    bool noSteps() const { return ((appearanceFlags >> 14) & 3) == 3; }
    bool operator==(const Appearance&) const = default;
};

struct ImageOp {      // S2C::ImageCreate, S2C::ImageDelete, S2C::ImageModify
    uint16_t op = 0;  // the message type
    uint32_t imageID = 0, appid = 0;
    uint8_t locKind = 0;  // 0x01 obj, 0x02 mob, 0x07 turf
    uint32_t cell = 0;    // locKind 0x07: the turf's flat cell index
    uint32_t locRef = 0;  // any other locKind
    std::vector<VisItem> visContents;
    std::vector<uint32_t> moreIDs;  // ImageDelete: the ids after the first
};

struct ClientAppearance {  // S2C::ClientAppearance
    uint32_t appid = 0;
    std::array<uint16_t, 4> fields{};  // absent trailing values are 0
    uint8_t nFields = 0;               // how many were sent
};

struct MouseSubs {       // S2C::MouseSubs
    uint32_t flags = 0;  // the FormatFlags word
    uint32_t extra = 0;  // the second word (flags bit 31); bits 2-3 are world.movement_mode
};

struct Bounds {  // S2C::Bounds
    uint16_t maxX = 0, maxY = 0, winW = 0, winH = 0;
    uint16_t view = 0, viewW = 0, viewH = 0;
    Bytes rest;
};

struct ExpansionReply {  // S2C::ExpansionReply
    std::vector<std::string> choices;
};

struct MapGrid {  // S2C::MapGrid, the grid half
    uint16_t x0 = 0, y0 = 0, z = 0;
    std::vector<uint32_t> cells;
};

struct InputPrompt {  // S2C::Input
    std::string title, defaultValue, message;
    uint32_t typeMask = 0;
    uint8_t filter = 0, filterKind = 0;
    uint32_t id = 0;
};

// A stat row's value (a DM value, reduced to what a row shows).
struct StatValue {
    uint8_t kind = 0;
    std::string text;       // kind 0x06
    double num = 0;         // kind 0x2a
    bool hasAppid = false;  // kinds 0x01-0x04: an atom
    uint32_t appid = 0, head = 0;
    uint16_t rowWord = 0;  // kinds 0x02/0x03: opaque row data, kept so a changed row invalidates its icon
    bool operator==(const StatValue&) const = default;
};

struct StatPanel {  // S2C::StatPanel
    struct Row {
        std::string name;
        StatValue value;
        uint16_t ordinal = 0;
    };
    uint8_t flags = 0, token = 0;
    bool hasTabs = false;
    std::vector<std::string> tabs;
    bool hasSelected = false;
    uint16_t selected = 0;
    std::vector<Row> rows;
    std::vector<uint16_t> clears;
};

struct FlickState {  // S2C::FlickState
    uint8_t variant = 0;
    std::string iconState;
    AtomLoc loc;
    uint32_t iconResIndex = 0;
};

struct Sound {  // S2C::Sound
    uint32_t resID = 0;
    uint8_t repeat = 0, wait = 0, channel = 0;
    uint16_t volume = 0;
    bool hasMarker = false;
    uint8_t marker = 0;
    uint16_t pan = 0;
    uint32_t frequencyBits = 0;
    uint8_t priority = 0, status = 0;
    int8_t environment = 0;
    uint32_t xBits = 0, yBits = 0, zBits = 0, falloffBits = 0;
    uint16_t channelWide = 0;
    Bytes unresolved;  // a marker block that does not frame: everything after the head, as sent
};

struct SelfRef {  // S2C::SelfRef
    uint32_t value = 0;
};

struct Hud {  // S2C::Hud
    struct Record {
        uint8_t op = 0;  // 1 remove (index is a list position), 2/3 add or update (index is the ref)
        uint8_t kind = 0;
        uint32_t index = 0;
        uint16_t ver = 0;
        uint32_t appid = 0;
        uint16_t flags = 0;
    };
    std::vector<Record> records;
};

struct Browse {  // S2C::Browse
    std::string html, params;
    bool close = false;
};

struct Output {  // S2C::Output
    std::string value, control;
};

struct Winset {  // S2C::Winset
    std::string control, props;
};

struct Winget {  // S2C::Winget
    std::string control, prop;
    uint32_t reqID = 0;
    bool hasFlag = false;
    uint8_t flag = 0;
};

struct SkinText {  // S2C::SkinText
    std::string dmf, hub;
    bool hasHub = false;
    bool hasFlags = false;
    uint16_t flags = 0;
};

struct FlickTarget {  // S2C::FlickTarget
    AtomLoc loc;
    bool hasAppid = false;
    uint32_t appid = 0;
};

struct VisContents {  // S2C::VisContents
    struct Record {
        uint8_t tag = 0;               // 1 a turf, 2 an obj, 3 a mob
        uint16_t x = 0, y = 0, z = 0;  // tag 1
        uint32_t ref = 0;              // other tags
        uint32_t appid = 0;            // tag 1: the turf's appearance
        std::vector<VisItem> contents;
    };
    std::vector<Record> records;
};

struct SkinResource {  // S2C::SkinResource
    uint32_t resID = 0;
    std::string hub;
    uint16_t flags = 0;
};

struct Animate {  // S2C::Animate
    struct Step {
        uint32_t from = 0, to = 0;
        uint32_t timeBits = 0;
        int32_t loop = 0;
        uint8_t easing = 0;
        uint32_t flags = 0;  // 0x4 ANIMATION_PARALLEL, 0x40000000 a chain head
        uint16_t tag = 0;
        bool hasDelay = false;
        uint32_t delayBits = 0;
        bool hasBase = false;
        uint32_t timeBaseBits = 0;
        std::string str;
        bool hasExt = false;
        uint32_t extBits = 0;
    };
    uint8_t kind = 0;  // 2 obj, 3 mob, 5 the client, 13 an image
    uint32_t ref = 0;
    uint16_t ver = 0;  // the target record's Key
    uint32_t delayBits = 0;
    uint8_t chain = 0;
    std::vector<Step> steps;
};

struct MeasureText {  // S2C::MeasureText
    std::string text, style;
    uint16_t width = 0;
    uint32_t id = 0;
};

struct BrowseRsc {  // S2C::BrowseRsc
    struct Record {
        uint32_t resID = 0;
        std::string name;
    };
    std::vector<Record> records;
};

// ---- messages the client reads that have no canonical JSON, and so no registry entry

struct Text {  // S2C::Text, S2C::WorldName, S2C::HostMessage: a cstring (to the NUL, or the end of the body)
    std::string text;
};

struct Timing {  // S2C::Timing: [tick ms u32][client frame ms u32], each optional
    std::optional<uint32_t> tickMs, frameMs;
};

struct Gone {  // S2C::MobGone, S2C::ObjGone: [id u32]...; a trailing partial id is ignored
    std::vector<uint32_t> ids;
};

struct Link {  // S2C::Link: [url\0], nothing after
    std::string url;
};

struct ProbeEcho {  // S2C::ProbeEcho: empty (the echo of DreamDaemon's idle probe), or [tag u16][value u32]
    bool hasValue = false;
    uint16_t tag = 0;
    uint32_t value = 0;
};

struct Alert {  // S2C::Alert: [id u32][message\0][title\0][count u16][button\0]*count
    uint32_t id = 0;
    std::string message, title;
    std::vector<std::string> buttons;  // as sent, empty ones included
};

struct PromptWithdraw {  // S2C::PromptWithdraw: [id u32][kind u8]
    uint32_t id = 0;
    uint8_t kind = 0;  // 0 an input(), 1 an alert()
};

struct VisTurfs {                 // S2C::VisTurfs
    std::vector<uint32_t> cells;  // flat turf cell indices (x + y*maxx + z*maxx*maxy, 0-based)
    std::vector<uint32_t> looks;  // per cell: the turf's appearance
    std::vector<uint32_t> areas;  // per cell: its turf-registry (AreaGrid) index
    std::vector<AreaGrid::Bind> binds;
};

// ---- decoders. Stateful ones read or advance the cross-frame State (proto/registry.h).

SessionEnd decodeSessionEnd(ByteSpan body);
WorldState decodeWorldState(ByteSpan body);
InView decodeInView(ByteSpan body);
RefBind decodeMobBind(ByteSpan body);
ContentsDelta decodeContentsDelta(ByteSpan body);
ResourceList decodeResourceList(State& st, ByteSpan body);
Verb decodeVerb(State& st, ByteSpan body);
// MapRefreshFull / MapRefresh / MapGrid; advances the map view only when the whole frame decodes.
MapRefresh decodeMapRefresh(State& st, uint16_t typ, ByteSpan body);
AreaGrid decodeAreaGrid(State& st, ByteSpan body);  // consumes the footprint the preceding refresh armed
RefBind decodeObjBind(ByteSpan body);
Appearance decodeAppearance(State& st, ByteSpan body);
ImageOp decodeImageCreate(ByteSpan body);
ImageOp decodeImageDelete(ByteSpan body);
ImageOp decodeImageModify(ByteSpan body);
ClientAppearance decodeClientAppearance(ByteSpan body);
MouseSubs decodeMouseSubs(ByteSpan body);
Bounds decodeBounds(State& st, ByteSpan body);  // records the bounds the map decoders need
ExpansionReply decodeExpansionReply(ByteSpan body);
MapGrid decodeMapGrid(ByteSpan body);  // the grid only: the refresh half is decodeMapRefresh(st, S2C::MapGrid, ...)
InputPrompt decodeInputPrompt(ByteSpan body);
StatPanel decodeStatPanel(ByteSpan body);
FlickState decodeFlickState(ByteSpan body);
Sound decodeSound(ByteSpan body);
SelfRef decodeSelfRef(ByteSpan body);
Hud decodeHud(ByteSpan body);
Browse decodeBrowse(ByteSpan body);
Output decodeOutput(ByteSpan body);
Winset decodeWinset(ByteSpan body);
Winget decodeWinget(ByteSpan body);
SkinText decodeSkinText(ByteSpan body);
FlickTarget decodeFlickTarget(ByteSpan body);
VisContents decodeVisContents(ByteSpan body);
SkinResource decodeSkinResource(ByteSpan body);
Animate decodeAnimate(ByteSpan body);
MeasureText decodeMeasureText(ByteSpan body);
BrowseRsc decodeBrowseRsc(ByteSpan body);

Text decodeText(ByteSpan body);      // never throws
Timing decodeTiming(ByteSpan body);  // never throws
Gone decodeGone(ByteSpan body);      // never throws
VisTurfs decodeVisTurfs(ByteSpan body);
Link decodeLink(ByteSpan body);
ProbeEcho decodeProbeEcho(ByteSpan body);
Alert decodeAlert(ByteSpan body);
PromptWithdraw decodePromptWithdraw(ByteSpan body);

// ---- canonical JSON

void addJson(canon::Object& o, const ObjectArrays& a);  // the array fields, flattened into o
canon::Json toJson(const SessionEnd& m);
canon::Json toJson(const WorldState& m);
canon::Json toJson(const InView& m);
canon::Json toJson(const RefBind& m);
canon::Json toJson(const ContentsDelta& m);
canon::Json toJson(const ResourceList& m);
canon::Json toJson(const Verb& m);
canon::Json toJson(const MapRefresh& m);
canon::Json toJson(const AreaGrid& m);
canon::Json toJson(const Appearance& m);
canon::Json toJson(const ImageOp& m);
canon::Json toJson(const ClientAppearance& m);
canon::Json toJson(const MouseSubs& m);
canon::Json toJson(const Bounds& m);
canon::Json toJson(const ExpansionReply& m);
canon::Json toJson(const MapGrid& m);
canon::Json toJson(const InputPrompt& m);
canon::Json toJson(const StatValue& v);
canon::Json toJson(const StatPanel& m);
canon::Json toJson(const FlickState& m);
canon::Json toJson(const Sound& m);
canon::Json toJson(const SelfRef& m);
canon::Json toJson(const Hud::Record& r);
canon::Json toJson(const Hud& m);
canon::Json toJson(const Browse& m);
canon::Json toJson(const Output& m);
canon::Json toJson(const Winset& m);
canon::Json toJson(const Winget& m);
canon::Json toJson(const SkinText& m);
canon::Json toJson(const FlickTarget& m);
canon::Json toJson(const VisContents& m);
canon::Json toJson(const SkinResource& m);
canon::Json toJson(const Animate& m);
canon::Json toJson(const MeasureText& m);
canon::Json toJson(const BrowseRsc& m);

}  // namespace fugue::proto

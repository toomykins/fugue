// model/verbmenu.h: the right-click verb menu. Which verbs a clicked atom offers the player, and which of them
// DreamDaemon would actually run.
//
// Each verb's `set src` clause, as the stock client encodes it (Src = the clause kind, Head = its range argument):
//
//     clause              Src   Head  admits
//     set src in view(3)  0x01  0x03  Head is the range, inclusive, square metric
//     set src in view()   0x01  0x7d  0x7d = "use world.view"
//     set src in oview(3) 0x02  0x03  as view, but excludes usr itself
//     set src = usr.contents 0x08     carried objs only
//     set src in world    0x10        unbounded (a HUD atom included)
//     set src = usr       0x20        identity; never a popup row
//     (no clause)         0x00        per class: obj -> in contents, mob -> never a row, turf -> view(0)
//
// DreamDaemon silently rejects an invocation its clause does not admit, so a menu row it would reject is a dead row.
// An unknown Src bit hides the verb.
//
// The o-forms exclude only usr and what usr carries, not usr's turf or what stands on it. A client.screen obj (no cell,
// not carried) is admitted by `in world` alone. Its `in view()` and `in usr` verbs are not offered by stock DreamSeeker.
//
// Verbs the player carries are offered for an atom by its first argument (examine, pull, pick up). Those use the same
// (Filter, FilterKind) vocabulary, with the type mask (mob 1, obj 2, turf 0x20). FilterKind 0x40 is a runtime list
// (`in view(src)`, `in src.list`, `in global_list`, whatever the expression). Such a list is offered for every atom of
// its type, members or not, and a pick of one is resolved by the server (c2s 0x0039, net::InputModel::atomArgument),
// which refuses a non-member.
//
// Differences from the DM reference, where stock DreamSeeker differs: a carried atom under view / range
// (as src and as argument), Src 0x03 = usr.loc, a runtime-list argument offered for every atom of its type, and the
// submenu order (category, then name).
//
// The model reads the single-writer model::World on the session thread, and parses a verb's dictionary JSON on demand.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "model/world.h"

namespace fugue::model {

struct VerbArgSpec {
    uint32_t typeMask = 0;
    uint8_t filter = 0, filterKind = 0;
};

struct VerbDef {
    uint16_t id = 0;
    std::string name, desc, category;
    uint8_t head = 0xff, src = 0, flags = 0;
    uint32_t flagsExt = 0;
    std::vector<VerbArgSpec> args;
    // VerbFlagWord: the flags varint. The head byte alone loses every bit once 0x80 is set.
    uint32_t flagWord() const { return (flags & 0x80) ? flagsExt : uint32_t(flags); }
};

// The canonical 0x0011 JSON the model stores in World::verbById. Name, desc and category come back without
// text-macro markers.
bool parseVerbDef(const std::string& json, VerbDef& out);

constexpr uint8_t kVerbSrcDefault = 0x00, kVerbSrcInView = 0x01, kVerbSrcInOView = 0x02, kVerbSrcUsrLoc = 0x03, kVerbSrcRangeMod = 0x04,
                  kVerbSrcUsrContents = 0x08, kVerbSrcInWorld = 0x10, kVerbSrcUsrOnly = 0x20;
constexpr uint32_t kVerbFlagHidden = 0x01, kVerbFlagNoPopup = 0x40;

// Wire ref kinds (proto::enc::RefKind numbering).
constexpr uint8_t kTargetNone = 0, kTargetObj = 1, kTargetMob = 2, kTargetTurf = 3;

struct AtomTarget {
    uint8_t kind = kTargetNone;
    uint32_t ref = 0;        // obj/mob wire ref (registry bit stripped); turf: unused
    int x = 0, y = 0;        // 1-based DM cell; 0,0 = no cell (a HUD atom)
    bool inContents = false; // the atom is in usr.contents (World::usrContents). Being on client.screen does not imply it.
    bool isSelfMob = false;
    bool targetable() const { return kind != kTargetNone; }
};

bool verbAdmitsTarget(const VerbDef& e, const AtomTarget& t, int px, int py, bool havePos, int view);
bool verbAdmitsAsArgument(const VerbDef& e, const AtomTarget& t, int px, int py, bool havePos, int view);

struct MenuVerb {
    VerbDef verb;
    bool viaArgument = false;   // true: a verb the PLAYER carries, taking this atom as its first argument
};

// Player position (own mob's cell, else the eye) and the view range (bounds.view, 5 before a 0x003e).
bool playerPos(const World& w, int& x, int& y);
int worldView(const World& w);

// The menu for one atom: its own verb section (verbAdmitsTarget), then the player's own verbs by argument
// (verbAdmitsAsArgument), de-duplicated, in DreamSeeker's order: by category ("" sorts as "Commands"), then by name,
// raw bytes.
std::vector<MenuVerb> atomMenu(const World& w, uint32_t appid, AtomTarget target);

// What a hit-test result names. kind: 0 turf, 1 obj, 2 mob (render::Hit numbering). appid is the clicked atom's
// appearance (0 = unknown: the menu then has only argument verbs).
struct HitTarget {
    AtomTarget target;
    uint32_t appid = 0;
};
HitTarget targetFromHit(const World& w, uint8_t hitKind, uint32_t ref, int worldX, int worldY, bool screen);

// The srcs a verb invoked by NAME (typed line, macro, argument dialog) can run on, as they go on the wire in c2s 0x0002
// [kind][ref] and the same 5 bytes of 0x003f/0x0039:
//
//     verb owner                         kind  ref on the wire
//     /client verb                       0x08  0 (local DreamDaemon; a hub-hosted server sends a non-zero ref, not modelled)
//     the player's own /mob verb         0x02  the own-mob ref
//     /obj, set src in view(1) / in usr  0x01  that obj's ref
//     /turf, set src in view(1)          0x03  wire cell (x-1) | (y-1) << 16
//     /area verb, default src            0x04  the area's ordinal, the index the 0x0020 grid carries for the player's cell
//     a mob in usr.contents              0x02  its ref
//     a client.screen obj, src in world  0x01  its ref (but `set src in usr` does not admit a screen obj)
//
// Candidates: the client appearance, the own mob, every movable whose `set src` clause admits it (popup flags are
// ignored: a hidden or no-popup verb is still a command), usr.contents, client.screen, turfs in view, and the own area.
// With no candidates nothing is sent. With several of different names the client asks in a listbox, in the order
// returned here. Order: mobs, then objs (by ascending ref, carried and floor objs interleaved), then turfs by ascending
// turf id, then areas. Several of one name, or a mob and an obj of one name, collapse to the first (the mob). A pick in
// that dialog sends just the 0x0002 (no 0x003f). everyAtom keeps every atom, of any name (a typed line reads them all).
//
// Each candidate carries stock DreamSeeker's rank of it as a src (srcRank): `dist`, the tile gap between the atom's
// footprint and usr's (-1 off the map: the client, usr, what usr carries; 127 out of sight), and two tie-breaks, `cell`
// (its place in its cell's list: the cell's mobs, then its objs, each in the relinks' Bind order (World::cellMobs /
// cellObjs), then the turf) and `carried` (its place in usr.contents: objs in the order they arrived, then mobs). A footprint is the cell widened by bound_x/y + step_x/y and
// bound_width/height against world.icon_size; pixel_x/y and the icon's size play no part. usr's footprint is drawn at
// the eye.
struct VerbSrc { uint8_t kind = 0; uint32_t ref = 0; };
struct VerbSrcRank { int dist = 127, cell = -1, carried = -1; };
struct VerbSrcCandidate { VerbSrc src; std::string name; VerbSrcRank rank = {}; };
std::vector<VerbSrcCandidate> verbSrcCandidates(const World& w, uint16_t verbId, bool everyAtom = false);
VerbSrcRank srcRank(const World& w, VerbSrc src);
// The verb ids of the atoms around the player (map movables, usr.contents, client.screen, the turfs in view, the player's
// own area) whose `set src` clause admits them where they stand: the verbs stock DreamSeeker lists in its panel besides
// the client's and the own mob's. A verb whose dictionary entry has not arrived yet is left out.
std::vector<uint16_t> srcAdmittedVerbIds(const World& w);
// A typed ATOM argument DreamSeeker resolves itself (FilterKind view / oview / range / orange / usr, an atom-only mask,
// or `as anything` / `as text|anything` without a list, read as mob|obj|turf): the atoms it accepts, in its bare
// listbox's order (objs by ascending ref, carried ones interleaved; mobs first and turfs last; usr's own turf first among
// turfs). Names are the plain atom names. How a typed token matches one is net::atomTyped (net/input.cpp). Other
// arguments yield an empty list, and argResolvesLocally is false.
bool argResolvesLocally(const VerbArgSpec& a);
std::vector<VerbSrcCandidate> verbArgCandidates(const World& w, const VerbArgSpec& a);
// The atom's DM \ref number, as a runtime-list menu pick names it in its c2s 0x0039 (net::atomArgumentText).
uint32_t dmRefOf(const World& w, const AtomTarget& t);
// The src of a verb the PLAYER carries (a right-click row offered by argument, from ownVerbIds): the client when its
// appearance holds the id, else the own mob (A-keyed atom, else its 0x0009 bind). False: neither holds it.
bool ownVerbSrc(const World& w, uint16_t verbId, VerbSrc& out);

}  // namespace fugue::model

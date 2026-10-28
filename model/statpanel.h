// model/statpanel.h: DreamSeeker's INFO control as data. The tab strip (Stat() tabs plus one tab per verb category),
// the rows of the selected tab, how a row's text reads, and what clicking a row or selecting a tab puts on the wire.
//
// Tabs are the Stat() tabs plus one per verb category, concatenated and ASCII-sorted (vg's "OOC" sorts before
// "Object"), with "verbs" as the wire name of every category tab. The tab-select body is encoded as 0x006b.
// The model keeps the Stat() side (World::stat). Everything below is a pure function of a const World.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "model/verbmenu.h"
#include "model/world.h"
#include "proto/enc_c2s_0002_input.h"

namespace fugue::model {

constexpr uint16_t kStatTabMsg = 0x006b;
constexpr uint32_t kVerbFlagNullCategory = 0x20;
inline const char* const kDefaultVerbCategory = "Commands";
inline const char* const kVerbTabWireName = "verbs";

// One decoded row value (World::StatLine::valueJson).
struct StatValue {
    uint8_t kind = 0;
    std::string text;
    double num = 0;
    uint32_t appid = 0, head = 0;
    bool hasAppid = false;
    bool isAtom() const { return hasAppid && kind >= 1 && kind <= 4; }
};
bool parseStatValue(const std::string& valueJson, StatValue& out);

std::string plainAtomName(const std::string& s);   // drops BYOND's 0xFF text-macro marker and its selector byte
// The value column as stock DreamSeeker prints it: text, a trimmed number, an atom's name, else "".
std::string statText(const World& w, const World::StatLine& l);

// The player's verb scope (client appearance verbs UNION the own mob's UNION srcAdmittedVerbIds). false = not knowable yet: callers use the dictionary.
bool verbScope(const World& w, std::vector<uint16_t>& ids);
// The verb tabs of the scope above, computed once: a caller that needs the strip and a tab's rows builds this per tick.
struct VerbPanel {
    std::vector<std::string> categories;                          // ASCII-sorted, distinct
    std::map<std::string, std::vector<VerbDef>> byCategory;       // by name, byte order
};
VerbPanel verbPanel(const World& w);
std::vector<std::string> verbCategories(const World& w);
std::vector<VerbDef> verbsInCategory(const World& w, const std::string& label);
// The strip: Stat() tab names in wire order, then the verb categories, as shown (without text-macro markers).
std::vector<std::string> statTabs(const World& w);
std::vector<std::string> statTabs(const World& w, const VerbPanel& p);
// The name a tab select sends: a Stat() tab's name as the server sent it, markers and all. "" = out of range.
std::string statTabWireName(const World& w, int index);
// The strip index -> verb category; false for a Stat() tab or an out-of-range index.
bool verbTabCategory(const World& w, int index, std::string& label);
bool verbTabCategory(const World& w, const VerbPanel& p, int index, std::string& label);

// A clickable row: turf ([07][its id] = the row's head), obj ([01][head]), mob ([02][head]). AREA stays refused.
bool statRowRef(const World& w, const StatValue& v, proto::enc::AtomRef& out);
// One mouse event on that row, as stock DreamSeeker reports it (proto::enc::encodeInfoClick has the frame).
struct StatRowClick {
    uint8_t sub = proto::enc::kMouseClick;   // kMouseClick, or kMouseDblClick for the second press of a double click
    std::string button = "left";             // "left" / "middle"; a right click sends nothing
    proto::enc::Mods mods;
    std::string control;                     // the INFO control's id, "<window>.<control>" (stock: "infowindow.info")
    bool icon = false;                       // the row draws an icon: params then lead with icon-x/icon-y (infoClickParams)
    int iconX = 0, iconY = 0;                // in that icon cell, as the shell widget reports the click
};
// The c2s 0x0002 body: location = the SELECTED stat tab's name, control and params as stock sends them. No screen-loc
// and no fabricated loc turf; icon-x/icon-y only for a row that draws an icon (see infoClickParams).
Bytes statRowClickBody(const World& w, const proto::enc::AtomRef& ref, const StatRowClick& c);

// 0x006b body: [u16 index LE][u8 token][name\0].
Bytes encodeStatTabBody(uint16_t index, uint8_t token, const std::string& name);

// What selecting a tab does to the model: bumps the token (never 0), takes ownership, clears the rows. Single-writer;
// the caller is the session thread. Returns false for an out-of-range index.
struct StatTabSelect { uint16_t index = 0; uint8_t token = 0; std::string name; };
bool selectStatTab(World& w, int index, StatTabSelect& out);

}  // namespace fugue::model

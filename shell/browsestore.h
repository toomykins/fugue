// BrowseRscStore: the browse() file cache. A page references files such as `common.css` by name. The bytes arrive as
// ordinary resources (Resource) and the name arrives separately: from BrowseRsc, from the offer's own
// filename, or from a `##action=browse_file;options=file=ALIAS` directive. This class joins the two.
//
// The session thread writes (declare, put, alias); the GUI thread's local origin reads (get, shouldWait). One mutex,
// held only for map work; nothing blocks.
//
// Name resolution: a name maps through the BrowseRsc ordinal to the ResourceList's id, and falls back to the transfer's
// filename by its basename, since a bundle record carries a full path while a page references the basename. A miss
// waits only for a name the world announced, or while an unnamed record is outstanding.
// Game-only DMI/DMF resources are not held. Audio/video are ordinary browser assets;
// Chromium decides codec support. The per-file byte cap also applies to media.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace fugue::shell {

class BrowseRscStore {
public:
    // BrowseRsc resolved to the ResourceList's id. An empty name is an unnamed record, whose name comes from the
    // transfer. A named record's (name, id) is remembered even before its bytes land.
    void declare(const std::string& name, uint32_t id);
    // A verified file landed (the resource observer). `name` is the offer's filename, or "".
    void put(uint32_t id, const std::string& name, const std::string& bytes);
    // `##action=browse_file;options=file=ALIAS`: file the bytes of `id` under ALIAS.
    void alias(const std::string& aliasName, uint32_t id);

    bool get(const std::string& name, std::string& bytes) const;
    // Whether a request for `name` is worth holding open. Announced: the world named it and the bytes are not here.
    // Unnamed: the name is unknown but an unnamed record is outstanding, so it may turn out to be it. That hold is
    // short, because most records on vg are unnamed (LocalOrigin bounds it).
    enum class Wait { No, Announced, Unnamed };
    Wait waitFor(const std::string& name) const;
    bool shouldWait(const std::string& name) const { return waitFor(name) != Wait::No; }
    size_t files() const;

    // An icon a page loads by resource reference, "[0xc000020]?state=crowbar&dir=2&movement=false&frame=1" (tgui's
    // DmIcon; stock DreamSeeker's browser answers it with that state's picture). The GUI thread asks (wantIcon), the
    // session thread takes the requests (takeWantedIcons), cuts the picture and answers (putIcon: a PNG, or nothing
    // when there is no such picture); icon() is true once it is answered.
    void wantIcon(const std::string& key);
    std::vector<std::string> takeWantedIcons();
    void putIcon(const std::string& key, std::string png);
    bool icon(const std::string& key, std::string& png) const;

    static bool blind(const std::string& name);  // an extension that is never held
    static std::string baseName(const std::string& s);

private:
    mutable std::mutex mu_;
    std::map<uint32_t, std::string> byId_;
    std::map<uint32_t, std::string> idName_;    // the transfer's own name
    std::map<std::string, uint32_t> nameToId_;  // BrowseRsc names and aliases
    std::map<std::string, uint32_t> byBase_;    // basename of the transfer's name -> id (latest wins)
    std::set<uint32_t> anonPending_;            // unnamed records whose bytes have not landed
    std::map<std::string, std::string> icons_;  // answered icon requests, by key
    std::set<std::string> iconsAsked_;
    std::vector<std::string> iconsWanted_;
};

}  // namespace fugue::shell

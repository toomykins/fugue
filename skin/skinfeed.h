// skin/skinfeed.h - wires the s2c skin frames (0x00af text, 0x00ef resource id + the .dmf resource, 0x00a8
// winset, 0x00a7 output to a winclone()d window, 0x00b1 winclone, 0x00b2 winshow) into a LiveSkin and answers 0x00a9 winget queries from it. Pure C++, session-thread use.
//
// Reply rules:
//   * every winget gets a reply (the DM proc is parked on it); the empty string is the refusal encoding.
//   * a query that arrives before the resolver is ready (no skin, or no client size yet) is held, then answered once it
//     is ready, or empty when its deadline passes. Answering at once races a skin that is still loading: vg's
//     chatOutput asks winexists("browseroutput") exactly once, on the first tick after login.
//   * winexists is the flagged query. Any non-zero flag byte selects the type arm (the prop is not read); a flag of 0
//     is the same as no flag (the named-property arm). Only winexists() adds a flag byte. A wildcard control (* ?) is
//     not answerable by winexists. A browse() popup answers MAIN once a skin is loaded (a name the skin declares answers
//     that control's type instead), and even with no skin once the hold's deadline passes (liveskin.h setPopups).
//   * the skin text wins over the skin resource. A 0x00af with an empty body or an empty dmf is "this world has no
//     skin" (stock applies a null skin there): the default skin (setDefaultSkin), unless the 0x00ef before it named a
//     skin resource (inferred: stock never sends that pair, and its 0x00af handler applies a null skin regardless).
//   * the skin a session brings (the world's, or the default one when it sends none) is compared with the loaded one.
//     The same skin, sent again by a later session (a reconnect, a reboot, a link() to any world), changes nothing:
//     stock DreamSeeker keeps every control's state, runtime window and macro across it, so vg's credits keep rolling.
//     A different skin replaces all of it, so a link() from a world with a skin to one without brings the default skin
//     back, and the default skin already loaded is not reloaded (nor signalled).
//   * the disclosure answer: winget(src, null, "fugue-version") answers kFugueVersionValue at once, with no skin needed.
//     It is the one winget this client answers that stock DreamSeeker does not. Stock answers the empty string for that
//     query, for an unknown control's type, and for wildcards asking for it. A mixed list names it with an empty value
//     (`fugue-version=;dpi=1`).
// Stock answers a null or "" control with the client-wide wingets (LiveSkin::wingetGlobal), and "<X>.*" with a window,
// a popup, or a built menu or macro set (LiveSkin::wingetWildcard). Both are held for a skin like any other. A
// null-control query that times out without a skin still answers what needs no skin (dpi, hwmode, url, ...).
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <set>
#include "fugue_version.h"
#include "net/bytes.h"
#include "skin/liveskin.h"

namespace fugue::skin {

struct WingetQuery {
    std::string control, prop;
    uint32_t reqId = 0;
    bool hasFlag = false;
    uint8_t flag = 0;
};

// body of s2c 0x00a9: [control\0][prop\0][reqid u32][flag u8]?  false = malformed.
bool parseWingetBody(ByteSpan body, WingetQuery& q);

constexpr int64_t kWingetDeferBudgetMs = 3000;   // the winget hold deadline

// The disclosure parameter (client-wide: asked with a null control) and this build's answer to it, the same text
// `fugue-qt --version` prints. A FUGUE_RELEASE session worker overrides it with setClientVersion(kVersionRelease).
constexpr const char* kFugueVersionParam = "fugue-version";
constexpr const char* kFugueVersionValue = kVersionDev;

class SkinFeed {
public:
    using Reply = std::function<void(uint32_t reqId, const std::string& value)>;
    explicit SkinFeed(std::shared_ptr<LiveSkin> live) : live_(std::move(live)) {}

    void onFrame(uint16_t type, ByteSpan body);                                   // 0x00af 0x00ef 0x00a8 0x00a7 0x00b1 0x00b2
    void onResource(uint32_t id, const std::string& name, ByteSpan data);          // every verified resource
    // Browse windows currently open (names), for winexists / popup winsets. Cheap; call each tick.
    void setBrowseNames(std::set<std::string> names) { live_->setPopups(std::move(names)); }
    // What winget(src, null, "fugue-version") answers (see the header). Default kFugueVersionValue.
    void setClientVersion(std::string v) { version_ = std::move(v); }
    // The skin for a world that sends none (app/defaultskin.h). Empty = such a world leaves the loaded skin alone.
    void setDefaultSkin(std::string dmf) { defaultSkin_ = std::move(dmf); }

    // Answer now when ready, else hold. `reply` is invoked synchronously for an immediate answer.
    void submitWinget(const WingetQuery& q, int64_t nowMs, const Reply& reply);
    // Flush held queries that can be answered (ready) or have timed out (empty). Call every loop iteration.
    void pump(int64_t nowMs, const Reply& reply);
    size_t held() const { return held_.size(); }

    // The answer for a query against the live skin, without holding.
    std::string answer(const WingetQuery& q) const;

    struct Counters { uint64_t winsets = 0, wingets = 0, heldTotal = 0, timedOut = 0, skinLoads = 0; };
    const Counters& counters() const { return c_; }
    LiveSkin& live() { return *live_; }
    const std::string& lastLoadError() const { return loadErr_; }

private:
    struct Held { WingetQuery q; int64_t deadline; };
    void loadText(const std::string& text);
    std::shared_ptr<LiveSkin> live_;
    std::deque<Held> held_;
    std::map<uint32_t, std::string> dmfRes_;   // small .dmf resources by verified id
    uint32_t skinResId_ = 0xffffffffu;
    bool textLoaded_ = false, resLoaded_ = false;
    std::string loadErr_;
    std::string version_ = kFugueVersionValue;
    std::string defaultSkin_;
    Counters c_;
};

}  // namespace fugue::skin

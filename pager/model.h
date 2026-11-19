// The pager's model: everything the window draws, as plain data and pure functions, so the readiness rules are testable
// without a widget. State = account, key.txt and identity-cache status, client presence, servers (pager.json), and live
// listing entries (display-only: a hub world reference is not joinable; see listing.h and target.h).
// renderList produces the `fugue-qt pager -list` report. No sockets, no network: the live listing is supplied by the caller.
// Known limit: the [warm D Mon] date is formatted in UTC.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "account/store.h"
#include "pager/listing.h"
#include "net/rscstore.h"

namespace fugue::pager {

struct CheckOptions {
    bool selfIsClient = false;  // the binary also holds the client: a join re-executes `<self> client`
    std::string selfExe;        // for the self-client entry and the exe-dir candidate
};

struct Status {
    std::string account;
    std::string keyTxtPath;
    std::optional<std::string> keyTxtErr;
    std::vector<std::string> keys;
    std::string identityPath;
    bool hasIdentity = false;
    std::string clientDesc;
    std::optional<std::string> clientErr;

    bool hasKey() const;
};

// Gather the status for cfg.account (empty = guest). Reads $HOME-relative state only.
Status check(const account::Config& cfg, const CheckOptions& opt = {});
// The reason joining srv would fail, or nullopt (same order of checks and same sentences as checklist()).
std::optional<std::string> blocker(const Status& st, const account::Server& srv);
// Plain-words version for the simple view. One sentence a player can act on: no file names, flags or protocol terms.
std::optional<std::string> plainBlocker(const Status& st, const account::Server& srv);
// The checklist lines, without the 2-space indent.
std::vector<std::string> checklist(const Status& st);
// The lines as the pager window shows them (Details > Setup). The window signs in itself, so the advice to run a login
// command reads "Sign in". Nothing else differs. checklist() and `-list` keep the command-line wording.
std::vector<std::string> windowChecklist(const Status& st);
// The window's wording of a shared status/blocker line: points at this pager's own Sign in instead of the command-line tool.
std::string windowWording(std::string line);

// The argv PREFIX a join runs: explicit/$FUGUE_CLIENT, else {selfExe,"client"} when selfIsClient, else a `fugue` binary next to selfExe or on $PATH
bool resolveClientArgv(const std::string& explicitIn, const CheckOptions& opt, std::vector<std::string>* argv,
                       std::string* err);

struct CacheStats {
    long long files = 0, bytes = 0, capBytes = net::RscStore::kDefaultCapBytes;
};  // the client's cap
CacheStats scanRscCache(const std::string& dir);  // the cache scan's rule: <2-hex>/<8-hex> files
std::string rscDir();                             // ~/.fugue/rsc

// `fugue-qt pager -list` text, exactly.
std::string renderList(const account::Config& cfg, const Status& st);

// Display-only row for the Live games tab.
struct LiveRow {
    std::string title, detail, id;
    int players = 0;
    long long version = 0;
};
std::vector<LiveRow> liveRows(const std::vector<LiveWorld>& worlds);

// ---- warm-up ("Download resources"): the pager runs the client's headless `--prefetch` pass (app/prefetchmode.cpp) and
// reads its output. Progress lines: "[prefetch] prefetch: D/T resolved, C cached, P outstanding" (net/prefetch.cpp); the
// last line: "PREFETCH ok=1 offered=N cached=C declared=D stored=S unverified=U bytes=B seconds=X[ error=...]".
struct WarmProgress {
    bool summary = false;  // the PREFETCH line: the pass is over
    bool ok = false;       // summary only
    uint64_t done = 0, total = 0, cached = 0,
             outstanding = 0;                     // progress lines (total excludes what the cache already held)
    uint64_t offered = 0, stored = 0, bytes = 0;  // summary
    std::string error;
};
bool parseWarmLine(const std::string& line, WarmProgress& out);  // false = not a warm-up line
std::string plainWarmProgress(
    const WarmProgress& p);  // "Downloading resources: 1200 of 9421 files" / the summary sentence
// The game tab's note about the resource cache for this server (lastPrefetch).
std::string plainWarmState(const account::Server& s);

// Closing the pager window never ends a running game: the window hides to its tray icon, or minimizes where the desktop
// has no tray. With no game running, closing quits.
enum class CloseAction { Quit, HideToTray, Minimize };
CloseAction closeAction(bool gameRunning, bool trayAvailable);

}  // namespace fugue::pager

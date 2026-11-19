// The pager's on-disk state under ~/.fugue:
//   identity-<ckey>  (identity=hex / install_id=0x.. / inner_key=0x..)   0600, dir 0700
//   byond-sid        (24 hex + \n; a real BYOND install's cfg/byond.txt sid wins and is copied here)
//   sid              (24 hex; machine-identity seed for the computer_id watermark; $XDG_STATE_HOME/fugue/sid when that is set)
//   pager.json       (account, keytxt, client, servers[...]; unknown keys preserved; atomic write)
//   key.txt discovery (explicit, $FUGUE_KEYTXT, ~/.fugue/key.txt, ~/.byond/{cfg/,}key.txt, ~/Documents/BYOND/..., Wine globs)
// Env overrides: FUGUE_PAGER_CONFIG, FUGUE_IDENTITY, FUGUE_BYOND_SID, FUGUE_KEYTXT, XDG_STATE_HOME (for `sid`).
// C++-only state belongs in ~/.fugue/qt/ (qtStateDir), never in pager.json.
// POSIX only (glob(3), rename(2)).
#pragma once
#include <QJsonObject>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "net/bytes.h"

namespace fugue::account {

std::optional<std::string> homeDir();              // $HOME, or %USERPROFILE% on Windows when HOME is absent
std::string fugueDir(std::string* err = nullptr);  // ~/.fugue
std::string qtStateDir();                          // ~/.fugue/qt  (C++-only state)

// Atomic 0600 write (temp file in the same dir, fsync, rename); creates missing parent dirs 0700.
bool atomicWrite(const std::string& path, const std::string& data, std::string* err = nullptr);

// ---- identity-<ckey> ----
struct Account {
    fugue::Bytes identity;
    uint32_t installId = 0;
    uint32_t innerKey = 0;
};
std::string identityPath(const std::string& account, std::string* err = nullptr);
bool saveAccount(const std::string& account, const Account& a, std::string* err = nullptr);
std::optional<Account> loadAccount(const std::string& account, std::string* err = nullptr);
std::string formatIdentityFile(const Account& a);  // install_id / inner_key written as 0x + 8 digits (0x%08x)

// ---- sid files ----
std::optional<std::string> byondConfigSid();                        // borrow from ~/.byond/cfg/byond.txt etc.
std::optional<std::string> pagerSid(std::string* err = nullptr);    // byond-sid
std::string installSidPath();                                       // XDG_STATE_HOME/fugue/sid else ~/.fugue/sid
std::optional<std::string> installSid(std::string* err = nullptr);  // read-or-create the machine-identity seed

// ---- pager.json ----
struct Timestamp {
    int64_t sec = -62135596800LL;  // the zero time, 0001-01-01T00:00:00Z
    int32_t nsec = 0;
    bool isZero() const { return sec == -62135596800LL && nsec == 0; }
    bool operator==(const Timestamp& o) const { return sec == o.sec && nsec == o.nsec; }
    static Timestamp now();
};
std::optional<Timestamp> parseRfc3339(const std::string& s);
std::string formatRfc3339(const Timestamp& t);  // UTC, fraction trimmed

struct Server {
    std::string name, addr;
    bool hub = false;
    std::vector<std::string> flags;
    Timestamp lastPlayed, lastPrefetch;
    QJsonObject extra;  // keys this build does not know: carried through a save
};
struct Config {
    std::string account, keytxt, client;
    std::vector<Server> servers;
    QJsonObject extra;  // unknown top-level keys

    int find(const std::string& addr) const;  // case-insensitive, -1 if absent
    void remember(Server s);
    void markPrefetched(Server s);
    void clearPrefetched();
    void remove(const std::string& addr);
    std::vector<Server> sorted() const;
};
std::string configPath(std::string* err = nullptr);
std::vector<Server> defaultServers();
std::optional<Config> loadConfig(std::string* err = nullptr);
bool saveConfig(const Config& c, std::string* err = nullptr);
std::string serializeConfig(const Config& c);

// ---- key.txt discovery ----
std::vector<std::string> keyTxtCandidates(const std::string& explicitPath);
// Prefers a file holding a block for `account` (case-insensitive); else the first existing; else error naming every path tried.
std::optional<std::string> resolveKeyTxt(const std::string& explicitPath, const std::string& account,
                                         std::string* err = nullptr);
std::string keyTxtWithBlock(const std::string& existing, const std::string& block);

}  // namespace fugue::account

// Clear cache: the only code that deletes cached sign-in state. Logout keeps ~/.fugue/identity-<ckey>.
// Removes exactly:
//   ~/.fugue/identity-*        every cached sign-in (account/store.h identityPath)
//   ~/.fugue/qt/pager-guest    the remembered "Play as guest" choice (launch.h guestChoicePath)
//   ~/.fugue/rsc               the shared resource cache, by RscStore::clear's rule (the client's default cache)
//   ~/.fugue/web               the webview profile's storage (localStorage/IndexedDB/cache); its contents, not the dir.
//                              Refused (with an error) while a live client holds <web>/.lock.
//   ~/.fugue/cauth.txt         the login-certificate cache (net/cauth.h); the next login mints a fresh key
// Kept: pager.json, pager-no-autosignin, byond-sid and sid (the computer_id source), client.log, macros, everything
// else under ~/.fugue, and ~/.byond. An explicit $FUGUE_IDENTITY is not scanned: it is not our cache.
// Qt-free; $HOME-relative only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fugue::pager {

struct CacheItem {
    std::string what;    // text shown in the Clear cache confirmation
    std::string path;    // the file, or the rsc dir
    uint64_t files = 0;  // 1 for a single file
    uint64_t bytes = 0;
};
struct CachePlan {
    std::vector<CacheItem>
        items;  // identity files (sorted by name), the guest flag, the rsc cache, the cauth cache; absent ones omitted
    uint64_t files() const;
    uint64_t bytes() const;
    bool empty() const { return items.empty(); }
};

// The <ckey> of every regular ~/.fugue/identity-<ckey>, sorted. Signing in to one needs no password: the identity is the sign-in.
std::vector<std::string> savedSignInNames();
// What Clear cache would remove right now (reads only).
CachePlan planClearCache();
// The confirmation text: one line per item with its size, then what is kept.
std::string describeClearCache(const CachePlan& p);
// "12.3 MiB" / "512 bytes" style.
std::string humanBytes(uint64_t n);
// Removes what planClearCache() finds now, re-scanned so nothing outside the rule is touched. *err receives the first
// failure; removal continues past it.
CachePlan clearCache(std::string* err = nullptr);

}  // namespace fugue::pager

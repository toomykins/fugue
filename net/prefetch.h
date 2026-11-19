// Warm-up prefetch: connect, log in, pull the world's whole offered resource list into the shared on-disk store,
// and return with no window. A later join then declares almost nothing. It runs as its own short-lived pass and
// never shares a thread with play.
#pragma once
#include <chrono>
#include <functional>
#include <string>
#include "net/session.h"

namespace fugue::net {

struct PrefetchResult {
    bool ok = false;  // list seen AND everything resolved (or the cache already held it)
    std::string error;
    uint64_t offered = 0;   // availability-list entries
    uint64_t cached = 0;    // served from the store: never declared
    uint64_t declared = 0;  // declarations sent
    uint64_t stored = 0;    // new verified files written
    uint64_t unverified = 0;
    uint64_t bytes = 0;  // bytes newly written
    double seconds = 0;
};

// session must already be connectAndLogin()ed. waitList: how long to wait for the list. stall: abort when nothing
// resolves for this long. A server can go silent for tens of seconds mid-list on a large world,
// so use >= 60 s there.
PrefetchResult runPrefetch(Session& s, std::chrono::milliseconds waitList, std::chrono::milliseconds stall,
                           const std::function<void(const std::string&)>& progress = {});

}  // namespace fugue::net

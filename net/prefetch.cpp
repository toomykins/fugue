#include "net/prefetch.h"
#include <cstdio>

namespace fugue::net {

PrefetchResult runPrefetch(Session& s, std::chrono::milliseconds waitList, std::chrono::milliseconds stall,
                           const std::function<void(const std::string&)>& progress) {
    using clock = std::chrono::steady_clock;
    PrefetchResult r;
    auto t0 = clock::now();
    auto finish = [&] {
        const ResourceStats& st = s.resourceStats();
        r.offered = st.listEntries;
        r.cached = st.cacheServed;
        r.declared = st.declared;
        r.stored = st.verified;
        r.unverified = st.unverified;
        r.bytes = st.bytesStored;
        r.seconds = std::chrono::duration<double>(clock::now() - t0).count();
        return r;
    };
    // The list rides the login window; poll (the tail can still grow at runtime, so there is no "complete" signal).
    while (s.resources().listSize() == 0) {
        if (s.stats().serverClosed) {
            r.error = "server closed before offering any resources";
            return finish();
        }
        if (clock::now() - t0 > waitList) {
            r.error = "the world offered no resources within the wait";
            return finish();
        }
        s.run(std::chrono::milliseconds(100));
    }
    size_t lastList = 0, lastProgress = 0;
    auto lastChange = clock::now(), lastReport = clock::now();
    auto quietSince = clock::time_point{};
    for (;;) {
        s.run(std::chrono::milliseconds(100));
        const ResourceStats& st = s.resourceStats();
        size_t list = s.resources().listSize();
        size_t pending = s.resources().pending();
        size_t prog = size_t(st.verified + st.alreadyHeld + st.unverified + st.declExpired) + list;
        auto now = clock::now();
        if (prog != lastProgress || list != lastList) {
            lastProgress = prog;
            lastList = list;
            lastChange = now;
        }
        if (progress && now - lastReport > std::chrono::seconds(1)) {
            lastReport = now;
            progress("prefetch: " + std::to_string(st.verified + st.alreadyHeld) + "/" +
                     std::to_string(list - st.cacheServed) + " resolved, " + std::to_string(st.cacheServed) +
                     " cached, " + std::to_string(pending) + " outstanding");
        }
        if (pending == 0) {
            if (quietSince == clock::time_point{}) quietSince = now;
            // the list is stable and nothing is outstanding for a beat: done (a late ResourceList tail restarts the
            // clock)
            if (now - quietSince > std::chrono::milliseconds(1500) &&
                now - lastChange > std::chrono::milliseconds(1500)) {
                r.ok = true;
                break;
            }
        } else
            quietSince = clock::time_point{};
        if (s.stats().serverClosed) {
            r.error = "server closed during prefetch";
            break;
        }
        if (now - lastChange > stall) {
            r.error =
                "stalled: no resource resolved in the stall window (the store keeps what arrived; re-run resumes)";
            break;
        }
    }
    return finish();
}

}  // namespace fugue::net

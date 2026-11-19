// The client's atom-table resync poll: C2S::ResyncMob (mob ids) and ResyncObj (obj ids). The server answers each id
// with MobBind / ObjBind (still there, with its appearance) or MobGone / ObjGone (gone: free it).
//
// A marked record goes out in the next emission, ascending, one frame per table (ResyncMob before ResyncObj), then is
// unmarked. Marks come from a new record (World::resync.newMobs / newObjs; an obj loses its mark to its own appearance
// binding), from a record the stock GC freed and named again (World::resync.freed; FUGUE_RESYNC_GC=1 only), and from the
// sweep (FUGUE_RESYNC_SWEEP=1 only). The sweep marks placed, unbound, unlinked records whose tile cell held a record of
// their table at the last relink, and runs on the fifth client tick after a teardown. A record retracted by an A1 has no
// position and is not swept.
#pragma once
#include <cstdint>
#include <set>
#include <vector>

#include "model/world.h"
#include "net/bytes.h"
#include "proto/msg.h"

namespace fugue::model {

constexpr uint64_t kGcAppearances = 500;  // collect on the 501st appearance record since the last collection
constexpr size_t kGcMinFree = 32;         // a table frees its unreachable records only past this many of them
constexpr int kResyncSweepTicks = 5;      // the sweep fires on the tick its counter, set to 1 by a teardown, passes 4

struct ResyncFrame {
    uint16_t type = 0;
    Bytes body;  // [id u32 LE] x n, n >= 1
};

// The stock relinks (resync.cpp): run by world.cpp where the stock parser runs them.
void relinkObjs(World& w);
void relinkMobs(World& w);
// The tail of an InView (objs) / WorldState (mobs) parse: arrays = the frame's object arrays (model/world.cpp).
void resyncAfterArrays(World& w, const proto::ObjectArrays& arrays, bool mobs);

class ResyncPoller {
public:
    // Drains w.resync (new mobs, teardowns) and returns the frames due at nowNs (any monotonic clock, the same one on
    // every call). Call right after every ingest (the immediate marks) and at least every 100 ms (the sweep).
    // Session thread.
    std::vector<ResyncFrame> poll(World& w, int64_t nowNs);
    bool sweepArmed() const { return counter_ != 0; }
    void setSweep(bool on) { sweep_ = on; }
    void setGc(bool on) { gc_ = on; }
    static bool sweepFromEnv();  // FUGUE_RESYNC_SWEEP
    static bool gcFromEnv();     // FUGUE_RESYNC_GC
    // What the sweep would mark in w right now (the selection only: no clock, no emission).
    static void sweepSet(const World& w, std::set<uint32_t>& mobs, std::set<uint32_t>& objs);

private:
    void gc(World& w);
    bool sweep_ = sweepFromEnv();
    bool gc_ = gcFromEnv();
    uint64_t gcAppearances_ = 0;  // World::ResyncEvents::appearances at the last collection
    uint64_t seenTeardowns_ = 0;
    int counter_ = 0;  // 0 = disarmed, 1..5 = ticks since the teardown
    bool clockStarted_ = false;
    int64_t nextTickNs_ = 0;  // the next client tick boundary
    std::set<uint32_t> markMob_, markObj_;
};

}  // namespace fugue::model

// The stock client frees appearances and tells the server which. DreamSeeker's collector (run every 1024th tick and as
// the 501st S2C::Appearance since the last run is built) frees an appearance record that nothing holds and that has
// been idle a few multiples of a tick-scaled window, and sends the freed ids in one C2S::AppearancesFreed (u32 LE each,
// in descending order). The server recycles an id only once it has been freed that way and sends a fresh Appearance
// when it next binds it: a stock client on a world that mints 2000 appearances a second holds under 7000 ids, one that
// never sends AppearancesFreed is given ever-new ids and keeps every definition.
//
// The model does the same: an appearance no held reference reaches, last defined at least kAppIdleTicks ago, is erased
// (past kAppGcMinFree of them) and reported in an AppearancesFreed. A held reference whose appearance is absent on two
// scans running (a root the scan missed) is asked for with C2S::AppearancePoll, which the server answers with the
// Appearance whether or not it believes the client holds it.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <unordered_set>
#include <vector>

#include "model/resync.h"
#include "model/world.h"
#include "proto/msg.h"

namespace fugue::model {

constexpr size_t kAppFreedPerFrame = 8192;  // ids in one AppearancesFreed (the c2s length is a u16)
constexpr uint64_t kAppIdleTicks =
    100;  // stock frees after about 60 on a light client; the margin covers a root the scan misses
constexpr size_t kAppGcMinFree = 32;     // collect only past this many evictable appearances (the stock `0x20 <`)
constexpr uint64_t kAppMinGcGap = 20;    // ticks between two collections, whatever the triggers
constexpr uint64_t kAppScanTicks = 5;    // how often held references are checked against the erased set
constexpr uint64_t kAppReaskTicks = 50;  // an AppearancePoll unanswered this long is sent again
constexpr int kAppGiveUp = 3;            // ... up to this many times; then the id is taken as gone

// Every appearance id a held reference names, closed over the overlays / underlays of the appearances that are present.
// Over-approximates on purpose: an id named by a reference this misses would be erased and never asked for.
void collectAppearanceRoots(const Session& s, std::unordered_set<uint32_t>& roots);

class AppearanceGc {
public:
    static bool fromEnv();  // FUGUE_APPEARANCE_GC, on unless 0
    void setEnabled(bool on) { on_ = on; }
    // After ResyncPoller::poll (its tick count is the clock). Returns the AppearancesFreed and AppearancePoll frames
    // due. Session thread.
    std::vector<ResyncFrame> step(Session& s);
    size_t erasedTotal() const { return erasedTotal_; }
    size_t pending() const { return asked_.size(); }
    size_t asksTotal() const { return asksTotal_; }

private:
    void collect(Session& s, std::vector<ResyncFrame>& out);
    bool on_ = fromEnv();
    uint64_t lastGcTick_ = 0, lastScanTick_ = 0, gcApps_ = 0;
    size_t erasedTotal_ = 0, asksTotal_ = 0;
    std::unordered_set<uint32_t> suspect_;                // referenced and absent at the last scan
    std::set<uint32_t> goneIds_;                          // asked kAppGiveUp times and never answered
    std::map<uint32_t, std::pair<uint64_t, int>> asked_;  // id -> tick of the last AppearancePoll, times sent
};

}  // namespace fugue::model

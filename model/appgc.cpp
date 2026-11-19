#include "model/appgc.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <set>
#include <vector>

#include "model/anomaly.h"
#include "model/statpanel.h"

namespace fugue::model {

namespace {
void chainAppids(const Atom::Chain& c, std::unordered_set<uint32_t>& out) {
    for (const Atom::AnimStep& st : c.steps) {
        out.insert(st.from);
        out.insert(st.to);
    }
}
}  // namespace

void collectAppearanceRoots(const Session& s, std::unordered_set<uint32_t>& roots) {
    const World& w = s.world();
    for (const auto& [k, a] : w.atoms) {
        roots.insert(a.appid);
        if (!a.chain.steps.empty()) chainAppids(a.chain, roots);
    }
    for (const auto& [k, id] : w.tiles) roots.insert(id);
    for (const auto& [k, id] : w.refBind) roots.insert(id);
    for (const auto& [k, id] : w.turfBinds) roots.insert(id);
    for (const auto& [k, id] : w.usrContents) roots.insert(id);
    for (const auto& [k, id] : w.hudAppid) roots.insert(id);
    for (const auto& [k, im] : w.images) roots.insert(im.appid);
    for (const auto& [k, slot] : w.hud) roots.insert(slot.appid);
    for (const auto& [k, c] : w.imageChains) chainAppids(c, roots);
    chainAppids(w.clientChain, roots);
    if (w.haveClientAppid) roots.insert(w.clientAppid);
    for (const World::StatLine& l : w.stat.lines)
        if (l.value.hasAppid) roots.insert(l.value.appid);
    for (uint32_t id : s.cells().appids) roots.insert(id);
    for (uint32_t id : w.grid.cells) roots.insert(id);
    // Close over the overlays and underlays of what is present.
    std::vector<uint32_t> work(roots.begin(), roots.end());
    while (!work.empty()) {
        uint32_t id = work.back();
        work.pop_back();
        auto it = w.appearances.find(id);
        if (it == w.appearances.end()) continue;
        for (const auto* list : {&it->second.fields.overlays, &it->second.fields.underlays})
            for (uint32_t c : *list)
                if (roots.insert(c).second) work.push_back(c);
    }
}

bool AppearanceGc::fromEnv() {
    const char* e = std::getenv("FUGUE_APPEARANCE_GC");
    return !(e && e[0] == '0');
}

void AppearanceGc::collect(Session& s, std::vector<ResyncFrame>& out) {
    World& w = s.worldMut();
    const uint64_t tick = w.resync.tick;
    lastGcTick_ = tick;
    gcApps_ = w.resync.appearances;
    std::unordered_set<uint32_t> roots;
    collectAppearanceRoots(s, roots);
    std::vector<uint32_t> dead;
    for (const auto& [id, a] : w.appearances)
        if (!roots.count(id) && tick - a.namedTick >= kAppIdleTicks) dead.push_back(id);
    if (dead.size() <= kAppGcMinFree) return;
    for (uint32_t id : dead) {
        w.appearances.erase(id);
        w.layer2Appids.erase(id);
        asked_.erase(id);
    }
    erasedTotal_ += dead.size();
    anomaly::note("gc-erase", std::to_string(dead.size()) + " ids");
    std::sort(dead.begin(), dead.end(), std::greater<uint32_t>());
    for (size_t i = 0; i < dead.size(); i += kAppFreedPerFrame) {
        ResyncFrame f;
        f.type = C2S::AppearancesFreed;
        for (size_t j = i; j < std::min(dead.size(), i + kAppFreedPerFrame); j++) putLe32(f.body, dead[j]);
        out.push_back(std::move(f));
    }
}

std::vector<ResyncFrame> AppearanceGc::step(Session& s) {
    std::vector<ResyncFrame> out;
    if (!on_) return out;
    World& w = s.worldMut();
    const uint64_t tick = w.resync.tick;
    if (tick - lastGcTick_ >= kAppMinGcGap && (tick - lastGcTick_ >= 1024 || w.resync.appearances - gcApps_ > 500))
        collect(s, out);
    if (erasedTotal_ == 0 || tick - lastScanTick_ < kAppScanTicks) return out;
    lastScanTick_ = tick;
    // A held reference whose appearance is absent, on two scans running, is built blank in stock and polled. (One scan
    // would also poll a bind that arrives a frame ahead of its Appearance.)
    std::unordered_set<uint32_t> roots;
    collectAppearanceRoots(s, roots);
    std::set<uint32_t> missing;
    for (auto it = goneIds_.begin(); it != goneIds_.end();)
        it = w.appearances.count(*it) ? goneIds_.erase(it) : std::next(it);
    for (uint32_t id : roots)
        if (!w.appearances.count(id) && !goneIds_.count(id) && suspect_.count(id)) missing.insert(id);
    suspect_.clear();
    for (uint32_t id : roots)
        if (!w.appearances.count(id)) suspect_.insert(id);
    for (auto it = asked_.begin(); it != asked_.end();)
        it = roots.count(it->first) && !w.appearances.count(it->first) ? std::next(it) : asked_.erase(it);
    ResyncFrame f;
    f.type = C2S::AppearancePoll;
    for (uint32_t id : missing) {
        auto& [at, n] = asked_[id];
        if (n > 0 && tick - at < kAppReaskTicks) continue;
        if (n >= kAppGiveUp) {
            goneIds_.insert(id);
            asked_.erase(id);
            continue;
        }
        at = tick;
        n++;
        putLe32(f.body, id);
        asksTotal_++;
        anomaly::note("gc-poll", "#" + std::to_string(id) + " attempt " + std::to_string(n));
    }
    if (!f.body.empty()) out.push_back(std::move(f));
    return out;
}

}  // namespace fugue::model

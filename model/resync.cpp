#include "model/resync.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace fugue::model {

namespace {
Atom* record(World& w, uint32_t id, bool mobs) {
    auto a = w.atoms.find(id | (mobs ? kRegistryA : 0u));
    return a == w.atoms.end() || w.resync.freed.count(a->first) ? nullptr : &a->second;
}

// One cell list put in Bind order, in place. Below 51 records, each record in turn moves to just before the nearest
// earlier record that is its back-link. From 51 the client runs another pass, which also WRITES back-links: from each
// place it follows the binders up the list to the last one, swaps that in as the chain's head, and lays the chain out
// after it along the Binds. The two put a chain in opposite orders.
void chainOrder(World& w, std::vector<uint32_t>& l, bool mobs) {
    const int n = int(l.size());
    if (n <= 50) {
        for (int i = 0; i < n; i++) {
            const Atom* a = record(w, l[i], mobs);
            if (!a) continue;
            for (int j = i - 1; j >= 0; j--)
                if (l[j] == a->backlink) {
                    std::rotate(l.begin() + j, l.begin() + i, l.begin() + i + 1);
                    break;
                }
        }
        return;
    }
    std::unordered_map<uint32_t, int> pos;          // id -> its place in l
    std::unordered_map<uint32_t, uint32_t> binder;  // bound id -> its binder nearest the front
    for (int k = n - 1; k >= 0; k--) {
        pos[l[k]] = k;
        if (const Atom* a = record(w, l[k], mobs); a && a->bindOther != 0xffff) binder[a->bindOther] = l[k];
    }
    int at = 0;  // carried between places, as the client carries it
    for (int i = 0; i < n;) {
        const uint32_t first = l[i];
        uint32_t cur = first;
        for (;;) {
            auto b = binder.find(cur);
            if (b == binder.end() || b->second == first) break;
            auto p = pos.find(b->second);
            if (p == pos.end()) break;
            at = p->second;
            if (at <= i) break;
            if (Atom* a = record(w, cur, mobs)) a->backlink = b->second;
            cur = b->second;
        }
        if (Atom* a = record(w, cur, mobs)) a->backlink = 0xffff;
        if (cur != first) {
            l[i] = cur;
            l[at] = first;
            pos[cur] = i;
            pos[first] = at;
        }
        int last = i;
        for (uint32_t u = l[i];;) {
            const Atom* a = record(w, u, mobs);
            if (!a || a->bindOther == 0xffff) break;
            const uint32_t y = a->bindOther;
            auto q = pos.find(y);
            if (q == pos.end()) break;
            at = q->second;
            if (at <= last) break;
            last++;
            if (at > last) {
                l[at] = l[last];
                pos[l[at]] = at;
                l[last] = y;
                pos[y] = last;
            }
            u = y;
        }
        i = last + 1;
    }
}

// The cell lists of one table, refilled from every placed record by descending id, then put in Bind order.
void refillCells(World& w, bool mobs) {
    auto& cells = mobs ? w.cellMobs : w.cellObjs;
    cells.clear();
    for (auto it = w.atoms.rbegin(); it != w.atoms.rend(); ++it)
        if (bool(it->first & kRegistryA) == mobs && it->second.hasPos && !w.resync.freed.count(it->first))
            cells[{it->second.x, it->second.y, it->second.z}].push_back(it->first & ~kRegistryA);
    for (auto& [cell, l] : cells) chainOrder(w, l, mobs);
}
}  // namespace

void relinkObjs(World& w) {
    // Back-links first: an obj's back-link is the lowest id of the objs whose Bind names it, placed or not, and 0xffff
    // when none does. Only the relinks write it, so between them it is stale.
    for (auto& [key, a] : w.atoms)
        if (!(key & kRegistryA)) a.backlink = 0xffff;
    for (auto it = w.atoms.rbegin(); it != w.atoms.rend(); ++it) {
        if (it->first & kRegistryA || it->second.bindOther == 0xffff || w.resync.freed.count(it->first)) continue;
        auto t = w.atoms.find(it->second.bindOther);
        if (t != w.atoms.end() && !w.resync.freed.count(t->first)) t->second.backlink = it->first;
    }
    refillCells(w, false);
    w.resync.relinkObjs = false;
}

void relinkMobs(World& w) {
    // The mob cell lists, ordered by the back-links as they stand: a full mob relink does not recompute them
    // (relinkBindsOnly writes them).
    refillCells(w, true);
    w.resync.relinkMobs = false;
}

// Back-links from the placed binders only, then every cell list of that table put in Bind order where it stands: the
// relink for a frame that carried Bind entries but no New or Pos ones. The one place a mob's back-link is computed.
static void relinkBindsOnly(World& w, bool mobs) {
    for (auto& [key, a] : w.atoms)
        if (bool(key & kRegistryA) == mobs) a.backlink = 0xffff;
    for (auto it = w.atoms.rbegin(); it != w.atoms.rend(); ++it) {
        if (bool(it->first & kRegistryA) != mobs || !it->second.hasPos || it->second.bindOther == 0xffff) continue;
        if (w.resync.freed.count(it->first)) continue;
        auto t = w.atoms.find(it->second.bindOther | (mobs ? kRegistryA : 0u));
        if (t != w.atoms.end() && !w.resync.freed.count(t->first)) t->second.backlink = it->first & ~kRegistryA;
    }
    for (auto& [cell, l] : mobs ? w.cellMobs : w.cellObjs) chainOrder(w, l, mobs);
}

void resyncAfterArrays(World& w, const proto::ObjectArrays& m, bool mobs) {
    // The InView tail relinks objs when the window moved or the frame had a New or Pos entry, else rebuilds their
    // back-links alone when it had a Bind entry. The WorldState tail does the same for mobs and also runs a pending obj
    // relink.
    bool moved = !m.created.empty() || !m.pos.empty(), bound = !m.bind.empty();
    if (mobs && w.resync.relinkObjs) relinkObjs(w);
    if (mobs ? w.resync.relinkMobs : w.resync.relinkObjs) moved = true;
    if (moved)
        (mobs ? relinkMobs : relinkObjs)(w);
    else if (bound)
        relinkBindsOnly(w, mobs);
}

void ResyncPoller::sweepSet(const World& w, std::set<uint32_t>& mobs, std::set<uint32_t>& objs) {
    // Per held record: placed, its tile's cell held a record of its table at the last relink, bound to nothing, not
    // back-linked, and a mob not still awaiting its creation poll's MobBind.
    for (const auto& [key, a] : w.atoms) {
        if (!a.hasPos || a.bindOther != 0xffff || w.resync.freed.count(key)) continue;
        std::array<int, 3> cell{a.x, a.y, a.z};
        if (key & kRegistryA) {
            if (!a.pollAnswerDue && a.backlink == 0xffff && w.cellMobs.count(cell)) mobs.insert(key & ~kRegistryA);
        } else if (a.backlink == 0xffff && w.cellObjs.count(cell)) {
            objs.insert(key);
        }
    }
}

// FUGUE_RESYNC_SWEEP=1 enables the sweep (default off); see resync.h.
bool ResyncPoller::sweepFromEnv() {
    const char* e = std::getenv("FUGUE_RESYNC_SWEEP");
    return e && e[0] && e[0] != '0';
}

// FUGUE_RESYNC_GC=1 enables the GC shadow (gc() below; default off); see resync.h.
bool ResyncPoller::gcFromEnv() {
    const char* e = std::getenv("FUGUE_RESYNC_GC");
    return e && e[0] && e[0] != '0';
}

void ResyncPoller::gc(World& w) {
    if (!gc_) return;
    // The record half of the stock GC. A record is kept when placed, back-linked, named in the current tick, or reached
    // from a kept root: the player's mob, usr.contents, a turf's vis_contents, and recursively the vis_contents of a kept
    // record. A table frees its unkept records only when there are more than 32 of them. Each free is a teardown, which
    // arms the sweep.
    gcAppearances_ = w.resync.appearances;
    std::set<uint32_t> kept;
    std::vector<uint32_t> work;
    auto keep = [&](uint32_t key) {
        if (w.atoms.count(key) && !w.resync.freed.count(key) && kept.insert(key).second) work.push_back(key);
    };
    for (const auto& [key, a] : w.atoms)
        if (a.hasPos || a.backlink != 0xffff || a.touchTick == w.resync.tick) keep(key);
    if (w.haveSelf) keep(w.selfRef | kRegistryA);
    for (const auto& [key, appid] : w.usrContents) keep(key);
    for (const auto& [cell, kids] : w.turfVis)
        for (uint32_t key : kids) keep(key);
    while (!work.empty()) {
        uint32_t key = work.back();
        work.pop_back();
        auto vc = w.visContents.find(key);
        if (vc != w.visContents.end())
            for (uint32_t child : vc->second) keep(child);
    }
    for (bool mobs : {true, false}) {
        std::vector<uint32_t> dead;
        for (const auto& [key, a] : w.atoms)
            if (bool(key & kRegistryA) == mobs && !kept.count(key) && !w.resync.freed.count(key)) dead.push_back(key);
        if (dead.size() <= kGcMinFree) continue;
        for (uint32_t key : dead) {
            w.resync.freed.insert(key);
            (mobs ? markMob_ : markObj_).erase(key & ~kRegistryA);
        }
        counter_ = 1;
    }
}

std::vector<ResyncFrame> ResyncPoller::poll(World& w, int64_t nowNs) {
    int64_t tick = w.tickNanos > 0 ? w.tickNanos : kDefaultTickNanos;
    // The client tick is free-running, anchored at the first call. The counter steps on each boundary crossed, and the
    // sweep runs on the fifth, so it fires four to five ticks after the teardown.
    if (!clockStarted_) {
        clockStarted_ = true;
        nextTickNs_ = nowNs + tick;
    }
    // Ticks that passed since the last call run first: they came before whatever the caller just ingested.
    while (nowNs >= nextTickNs_) {
        nextTickNs_ += tick;
        if (counter_ != 0 && sweep_) {
            if (counter_ <= kResyncSweepTicks - 1)
                counter_++;
            else {
                counter_ = 0;
                sweepSet(w, markMob_, markObj_);
            }
        }
        if ((w.resync.tick++ & 0x3ff) == 0) gc(w);  // every 1024th tick collects
    }
    // A collection also runs when the 501st appearance record since the last one is built.
    if (w.resync.appearances - gcAppearances_ > kGcAppearances) gc(w);
    if (w.resync.teardowns != seenTeardowns_) {  // a teardown (re)arms the counter to 1, not an increment
        seenTeardowns_ = w.resync.teardowns;
        counter_ = 1;
    }
    markMob_.insert(w.resync.newMobs.begin(), w.resync.newMobs.end());
    w.resync.newMobs.clear();
    markObj_.insert(w.resync.newObjs.begin(), w.resync.newObjs.end());
    w.resync.newObjs.clear();
    std::vector<ResyncFrame> out;
    auto emit = [&](uint16_t type, std::set<uint32_t>& ids) {
        if (ids.empty()) return;
        ResyncFrame f;
        f.type = type;
        for (uint32_t id : ids) putLe32(f.body, id);
        ids.clear();
        out.push_back(std::move(f));
    };
    emit(C2S::ResyncMob, markMob_);
    emit(C2S::ResyncObj, markObj_);
    return out;
}

}  // namespace fugue::model

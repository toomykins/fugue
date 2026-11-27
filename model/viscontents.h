// Worker-owned ordered vis memberships. References carry no child key on the wire.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace fugue::model {
struct VisReference {
    uint8_t kind = 0; // wire: 1 turf, 2 object, 3 mob; NOT image location kinds
    uint32_t ref = 0;
    bool operator==(const VisReference&) const = default;
};
// Local binding only: unseen slot reuse cannot be detected. A missing child binds
// once to the first observed record, unless a deletion was observed in between.
class VisContents {
public:
    static constexpr size_t maxMembers = 65536, maxPerHolder = 4096;
    static constexpr uint64_t turfBit = uint64_t(1) << 63;
    static constexpr uint64_t imageBit = uint64_t(1) << 62;
    static uint64_t image(uint32_t id) { return imageBit | id; }
    struct Member {
        VisReference reference;
        uint64_t childIdentity = 0;
    };
    struct List {
        uint64_t identity = 0, holderIdentity = 0;
        std::vector<Member> members;
    };
    std::map<uint64_t, List> lists;
    static uint64_t turf(uint16_t x, uint16_t y, uint16_t z) {
        return turfBit | uint64_t(z) << 32 | uint64_t(y) << 16 | x;
    }
    static bool movable(const VisReference& r) { return r.kind == 2 || r.kind == 3; }
    static uint32_t atom(const VisReference& r) { return r.ref | (r.kind == 3 ? 0x80000000u : 0); }
    void erase(uint64_t holder) {
        auto found = lists.find(holder);
        if (found == lists.end()) return;
        for (size_t i = 0; i < found->second.members.size(); ++i) {
            const auto& m = found->second.members[i];
            if (!movable(m.reference) || m.childIdentity) continue;
            auto pending = pending_.find(atom(m.reference));
            if (pending == pending_.end()) continue;
            pending->second.erase({holder, i});
            if (pending->second.empty()) pending_.erase(pending);
        }
        retained_ -= found->second.members.size(); lists.erase(found);
    }
    template<class Identity>
    bool replace(uint64_t holder, uint64_t holderIdentity, const std::vector<VisReference>& refs, Identity identity) {
        // Exact retransmission retains the membership token, including pinned stale
        // children. A remove/re-add or changed list obtains a new token.
        auto old = lists.find(holder);
        if (old != lists.end() && old->second.holderIdentity == holderIdentity && old->second.members.size() == refs.size()) {
            bool same = true;
            for (size_t i = 0; i < refs.size(); ++i) same &= old->second.members[i].reference == refs[i];
            if (same) return true;
        }
        erase(holder);
        if (refs.empty()) return true;
        if (refs.size() > maxPerHolder || refs.size() > maxMembers - retained_) return false;
        auto& list = lists[holder]; list.identity = ++next_; list.holderIdentity = holderIdentity;
        list.members.reserve(refs.size());
        for (const auto& ref : refs) {
            uint64_t token = movable(ref) ? identity(atom(ref)) : 0;
            if (movable(ref) && !token) pending_[atom(ref)].insert({holder, list.members.size()});
            list.members.push_back({ref, token});
        }
        retained_ += refs.size(); return true;
    }
    void observed(uint32_t child, uint64_t identity) {
        auto pending = pending_.find(child); if (pending == pending_.end()) return;
        for (const auto& [holder, index] : pending->second) lists.at(holder).members[index].childIdentity = identity;
        pending_.erase(pending);
    }
    void deleted(uint32_t child) { erase(child); observed(child, UINT64_MAX); }
    size_t retained() const { return retained_; }
    size_t pending() const { size_t n = 0; for (const auto& [id, refs] : pending_) n += refs.size(); return n; }
private:
    std::map<uint32_t, std::set<std::pair<uint64_t, size_t>>> pending_;
    size_t retained_ = 0;
    uint64_t next_ = 0;
};
}

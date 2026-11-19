#include "net/respump.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "net/inflate.h"
#include "net/resource.h"
#include "proto/msg.h"

namespace fugue::net {
namespace {
// Declarations are batched: DD answers each ResourceDeclare with one load_rsc offer and then sends the whole answer, so
// a batch costs one round trip plus its bytes. Stock DreamSeeker declares all icon/sound ordinals (ResourceList kind !=
// 0) in one ResourceDeclare and gets one gzip bundle back.
//
// Batches are sized by bytes, not ordinals. DD's queue is serial and only one background batch waits behind the one
// being answered, so the batch length follows an EWMA of the bytes per ordinal that recent bundles carried:
// kTargetBatchBytes divided by that, clamped to 1..kBatch.
//
// An offer carries no ordinal, so it is matched to the oldest unanswered batch (DD answers in order). The guess is
// settled when the bytes land: each verified record names its ResourceList id, which names the batch. A batch past
// kDeclExpiryMs keeps its place, because DD still answers it.
constexpr size_t kBatch = 32;                      // most ordinals per ResourceDeclare
constexpr size_t kFirstBatch = 8;                  // before any bundle has been measured
constexpr double kTargetBatchBytes = 1024 * 1024;  // what one background bundle should carry
constexpr size_t kMaxBatchesInFlight =
    1;  // background batches declared and unanswered (DD holds it behind the one it sends)
constexpr size_t kMaxUrgentInFlight =
    4;  // prioritize()d batches have their own window, so a demand never waits for a background slot
constexpr uint64_t kDeclExpiryMs = 30000;  // declaration expiry; frees the window slot only
constexpr size_t kMaxUnsettled = 64;       // unsettled batches kept for matching; the oldest beyond it is given up

std::string cstr(ByteSpan b, size_t& p) {
    std::string s;
    while (p < b.size() && b[p]) s.push_back(char(b[p++]));
    if (p < b.size()) p++;  // terminator
    return s;
}
}  // namespace

ResourcePump::ResourcePump(std::string storeDir, Send send, Log log)
    : dir_(std::move(storeDir)), send_(std::move(send)), log_(std::move(log)) {
    rs_ = std::make_unique<RscStore>(dir_);
    if (!rs_->ok()) log_("resource: cannot open the store at " + dir_ + " - running without a cache");
}

uint64_t ResourcePump::nowMs() const {
    if (clock_) return clock_();
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::string ResourcePump::pathFor(const std::string& dir, uint32_t id) { return RscStore::pathFor(dir, id); }

void ResourcePump::onResList(ByteSpan body) {
    // Ordinal = index into the concatenation of the list.
    for (size_t p = 0; p + 5 <= body.size(); p += 5) {
        uint32_t id = le32(&body[p]);
        uint32_t ord = uint32_t(list_.size());
        list_.push_back(id);
        known_.insert(id);
        st_.listEntries++;
        // A cached, named entry is served from disk and never declared. A cached but nameless entry is declared anyway:
        // a browse_rsc asset is resolved by name, and the name exists only in the Resource offer, which the server
        // sends only for a declaration. The offer re-teaches the name, so the next join serves the entry from disk. The
        // list alone cannot tell an unnamed browse asset from an unnamed icon, so this errs toward declaring, never
        // toward serving bytes the page cannot look up.
        if (rs_->ok() && rs_->has(id)) {
            if (rs_->hasName(id)) {
                if (stored_.emplace(id, "").second) {}
                st_.cacheServed++;
                continue;
            }
            st_.cacheNameless++;
        }
        declQ_.push_back({ord, id, false});
    }
    pump();
}

size_t ResourcePump::openBatches(bool urgent) const {
    size_t n = 0;
    for (const Batch& b : out_)
        if (!b.answered && !b.expired && b.urgent == urgent) n++;
    return n;
}

size_t ResourcePump::inFlight() const {
    size_t n = 0;
    for (const Batch& b : out_)
        if (!b.answered && !b.expired) n += b.decls.size();
    return n;
}

size_t ResourcePump::nextBackgroundBatch() const {
    if (bytesPerOrd_ <= 0) return kFirstBatch;
    double n = kTargetBatchBytes / bytesPerOrd_;
    return n >= double(kBatch) ? kBatch : n < 1 ? 1 : size_t(n);
}

// Put declarations back at the head of the queue, in order, without letting background ones jump a demand: pump() only
// takes a batch of the same urgency as declQ_.front(), so background declarations in front of an urgent one would hold
// the demand to the background window.
void ResourcePump::requeueFront(const std::vector<Decl>& decls) {
    if (decls.empty()) return;
    auto at = declQ_.begin();
    if (!decls.front().urgent)
        while (at != declQ_.end() && at->urgent) ++at;
    declQ_.insert(at, decls.begin(), decls.end());
}

void ResourcePump::pump() {
    for (;;) {
        if (declQ_.empty()) return;
        bool urgent = declQ_.front().urgent;
        // Urgent ones sit at the front, so a full urgent window also holds back the background fill behind them.
        if (openBatches(urgent) >= (urgent ? kMaxUrgentInFlight : kMaxBatchesInFlight)) return;
        const size_t n = urgent ? kBatch : nextBackgroundBatch();
        Batch b;
        b.urgent = urgent;
        Bytes body;
        while (!declQ_.empty() && b.decls.size() < n && declQ_.front().urgent == urgent) {
            b.decls.push_back(declQ_.front());
            putLe32(body, declQ_.front().ordinal);
            declQ_.pop_front();
        }
        b.sentMs = nowMs();
        if (!send_(C2S::ResourceDeclare, body)) {  // put them back: a later pump retries
            requeueFront(b.decls);
            log_("resource: declare send failed");
            return;
        }
        st_.declared += b.decls.size();
        st_.declFrames++;
        b.seq = ++seq_;
        out_.push_back(std::move(b));
    }
}

void ResourcePump::prioritize(const std::vector<uint32_t>& ids) {
    if (ids.empty() || declQ_.empty()) return;
    std::unordered_set<uint32_t> want(ids.begin(), ids.end());
    std::deque<Decl> front, rest;
    for (Decl& d : declQ_) {
        if (want.count(d.id) || d.urgent) {
            d.urgent = true;
            front.push_back(d);
        } else
            rest.push_back(d);
    }
    if (front.empty()) return;
    for (Decl& d : rest) front.push_back(d);
    declQ_.swap(front);
    pump();
}

void ResourcePump::tick() {
    uint64_t now = nowMs();
    for (Batch& b : out_)
        if (!b.answered && !b.expired && b.sentMs + kDeclExpiryMs < now) {
            b.expired = true;
            st_.declExpired += b.decls.size();
        }
    // Bounded memory: a batch this far behind is never coming. An unanswered one is declared again (to the back of the queue).
    while (out_.size() > kMaxUnsettled) {
        Batch& b = out_.front();
        if (!b.answered) {
            if (b.expired) st_.declExpired -= b.decls.size();
            st_.declSkipped += b.decls.size();
            for (const Decl& d : b.decls) declQ_.push_back({d.ordinal, d.id, false});
        }
        out_.pop_front();
    }
    pump();
}

void ResourcePump::onFrame(ByteSpan p) {
    // Header: rid u32 | ffffffff | pkt u32 | total u32 | offset u32 ...
    if (p.size() < 20) {
        st_.protocolErrors++;
        return;
    }
    uint32_t rid = le32(&p[0]), pkt = le32(&p[8]), total = le32(&p[12]);
    if (pkt == 0) {
        // OFFER: ... | 01 | size u32 @17 | kind u8 @21 | name\0 @22 | params\0
        if (p.size() < 23) {
            st_.protocolErrors++;
            return;
        }
        Offer of;
        of.size = le32(&p[17]);
        of.kind = p[21];
        size_t q = 22;
        of.name = cstr(p, q);
        of.params = cstr(p, q);
        st_.offers++;
        // A load_rsc offer answers one whole ResourceDeclare, and DD answers in order, so the guess is the oldest batch
        // not yet answered, expired or not. When the bytes land, every batch unsettled then is a candidate too
        // (finish); settle() decides by content which one it was.
        if (of.params.find("load_rsc") != std::string::npos) {
            Batch* g = nullptr;
            for (Batch& b : out_)
                if (!b.answered) {
                    g = &b;
                    break;
                }
            if (g) {
                g->answered = true;
                of.guess = g->seq;
                Cand c{g->seq, {}};
                for (const Decl& d : g->decls) c.ids.push_back(d.id);
                of.cands.push_back(std::move(c));
            }
        }
        Xfer x;
        x.of = std::move(of);
        x.total = total;
        xfers_[rid] = std::move(x);
        // Answer immediately: pull packet 1. 13 bytes: rid | ffffffff | 00 | next u32.
        Bytes req;
        putLe32(req, rid);
        putLe32(req, 0xffffffffu);
        req.push_back(0);
        putLe32(req, 1);
        if (send_(C2S::Resource, req))
            st_.pulls++;
        else
            log_("resource: pull send failed");
        pump();
        return;
    }
    st_.dataFrames++;
    uint32_t offset = le32(&p[16]);
    auto it = xfers_.find(rid);
    if (it == xfers_.end()) {  // data with no offer on this end (raced): make a shell, total from the header
        it = xfers_.emplace(rid, Xfer{}).first;
        it->second.total = total;
    }
    Xfer& x = it->second;
    bool bad = false;
    if (total > 0 && pkt >= total)
        bad = true;
    else if (x.total != 0 && total != 0 && x.total != total)
        bad = true;
    else if (auto d = x.parts.find(pkt);
             d != x.parts.end() &&
             (d->second.size() != p.size() - 20 || !std::equal(d->second.begin(), d->second.end(), p.begin() + 20)))
        bad = true;
    if (bad) {
        st_.protocolErrors++;
        log_("resource " + hexEncode(ByteSpan(p.data(), 4)) + ": inconsistent packet " + std::to_string(pkt) + "/" +
             std::to_string(total) + ", transfer dropped");
        Offer of = std::move(x.of);
        xfers_.erase(it);
        settle(of, 0, 0);  // its batch is not coming: free the slot. Its files are lost.
        pump();
        return;
    }
    if (x.total == 0) x.total = total;
    x.parts[pkt] = Bytes(p.begin() + 20, p.end());
    x.offsets[pkt] = offset;
    // Complete when packets 1..total-1 have all arrived (reassembly is by BYTE OFFSET, not arrival order).
    bool done = x.total == 0 ? !x.parts.empty() : x.parts.size() == x.total - 1;
    if (!done) return;
    Xfer fin = std::move(x);
    xfers_.erase(it);
    finish(rid, fin);
    pump();
}

bool ResourcePump::verifiedForm(ByteSpan payload, uint32_t rid, const Offer& of, Bytes& out, uint32_t& idOut) const {
    if (payload.empty()) return false;
    auto accept = [&](ByteSpan cand, uint32_t& id) {
        uint32_t h = resId(cand);
        if (known_.count(h) || h == rid) {
            id = h;
            return true;
        }
        return false;
    };
    uint32_t id = 0;
    if (accept(payload, id)) {
        out.assign(payload.begin(), payload.end());
        idOut = id;
        return true;
    }
    std::vector<uint32_t> keys{of.want(), rid};
    // A one-ordinal batch answered by a whole file: its key is that batch's id. Past the guess, each other candidate's first id.
    for (size_t k = 1; k < of.cands.size(); k++)
        if (!of.cands[k].ids.empty()) keys.push_back(of.cands[k].ids[0]);
    for (uint32_t key : keys) {
        if (!key) continue;
        Bytes dec = decodeResource(payload, key);
        if (accept(dec, id)) {
            out = std::move(dec);
            idOut = id;
            return true;
        }
    }
    return false;
}

// Writes through the content-addressed store (put re-verifies resId == id, temp+rename). The session observer receives
// the bytes via readStored(). If the store is unusable, the file is still reported from memory for this run.
bool ResourcePump::storeFile(uint32_t id, ByteSpan data, const std::string& name) {
    bool had = rs_->ok() && rs_->has(id);
    if (had) {
        st_.alreadyHeld++;
        rs_->put(id, name, data);
        stored_[id] = name;
        return true;
    }
    if (!rs_->put(id, name, data)) {
        log_("resource: could not store " + hexEncode(ByteSpan(reinterpret_cast<const uint8_t*>(&id), 4)));
        return false;
    }
    st_.bytesStored += data.size();
    st_.verified++;
    stored_[id] = name;
    return true;
}

void ResourcePump::declare(uint32_t ordinal, uint32_t id) { declQ_.push_back({ordinal, id, false}); }

bool ResourcePump::readStored(uint32_t id, Bytes& data) {
    std::string nm;
    if (rs_->ok() && rs_->get(id, data, &nm)) {
        auto it = stored_.find(id);
        if (it != stored_.end() && it->second.empty())
            it->second = nm;  // a cache hit's name fills a blank, never overwrites the world's
        return true;
    }
    // Vanished or failed verification (now deleted): declare it again so the wire path fetches it.
    stored_.erase(id);
    st_.cacheRedeclared++;
    for (uint32_t ord = 0; ord < list_.size(); ord++)
        if (list_[ord] == id) declare(ord, id);
    pump();
    return false;
}

// A bundle is a gzip stream of records: [reclen u32][1 byte], then reclen bytes: [1][id u32][...][size u32 @13]
// [name\0 @17][file bytes]. Every record is verified by hash like a whole file, so a wrong guess about the header
// shows up as "unverified", not as a bad file on disk.
bool ResourcePump::storeBundle(ByteSpan gz, const Offer& of, std::map<uint64_t, int>& hits) {
    Bytes u;
    if (!gunzip(gz, u)) return false;
    st_.bundles++;
    // id -> the candidate batch that declared it (the guess wins a tie: it is listed first).
    std::unordered_map<uint32_t, size_t> candOf;
    for (size_t c = 0; c < of.cands.size(); c++)
        for (uint32_t id : of.cands[c].ids) candOf.emplace(id, c);
    size_t primary = 0;  // the batch the last verified record came from (the guess until a record says otherwise)
    size_t p = 0, recIndex = 0;
    while (p + 5 <= u.size()) {
        uint32_t rl = le32(&u[p]);
        if (rl < 18 || p + 5 + rl > u.size()) {
            st_.protocolErrors++;
            log_("bundle: bad record framing at " + std::to_string(p));
            return true;
        }
        ByteSpan r(u.data() + p + 5, rl);
        uint32_t recId = le32(&r[1]);
        uint32_t size = le32(&r[13]);
        size_t q = 17;
        std::string name = cstr(r, q);
        if (r.size() - q < size) {
            st_.protocolErrors++;
            log_("bundle: record truncated");
            return true;
        }
        ByteSpan file = r.subspan(q, size);
        Bytes out;
        uint32_t id = 0;
        auto accept = [&](ByteSpan c) {
            uint32_t h = resId(c);
            if (known_.count(h) || h == recId) {
                id = h;
                return true;
            }
            return false;
        };
        auto tryKey = [&](uint32_t key) {
            if (!key) return false;
            Bytes dec = decodeResource(file, key);
            if (!accept(dec)) return false;
            out = std::move(dec);
            return true;
        };
        bool ok = false;
        if (accept(file)) {
            out.assign(file.begin(), file.end());
            ok = true;
        }
        if (!ok) ok = tryKey(recId);
        // A ciphered record's key is its own ResourceList id, which the record header does not carry. A batch's bundle
        // holds one record per declared id, so every id of the batch is a candidate, the record's own position first,
        // then the other unsettled batches in case the positional guess was wrong. Each candidate still has to hash to
        // a listed id, so a wrong key can never store a wrong file.
        for (size_t step = 0; !ok && step < of.cands.size(); step++) {
            size_t c = step == 0 ? primary : (step <= primary ? step - 1 : step);
            const auto& ids = of.cands[c].ids;
            for (size_t k = 0; !ok && k < ids.size(); k++) ok = tryKey(ids[(recIndex + k) % ids.size()]);
        }
        recIndex++;
        if (ok) {
            if (auto it = candOf.find(id); it != candOf.end()) {
                primary = it->second;
                hits[of.cands[it->second].seq]++;
            }
            storeFile(id, out, name);
        } else {
            st_.unverified++;
            if (unverifiedLogged_++ < 5)
                log_("bundle record unverified: id=" + hexEncode(ByteSpan(r.data() + 1, 4)) + " name=" + name +
                     " size=" + std::to_string(size) + " flag=" + hexEncode(ByteSpan(&u[p + 4], 1)));
        }
        p += 5 + rl;
    }
    return true;
}

// The transfer answered batch `matched` (0: its content named none; keep the guess). A batch answered past the guess
// means DD skipped every older unanswered one (its queue is serial), so those are declared again rather than waited on.
void ResourcePump::settle(const Offer& of, uint64_t matched, size_t payloadBytes) {
    if (!of.guess && !matched) return;
    const uint64_t m = matched ? matched : of.guess;
    if (matched && of.guess && matched != of.guess) {
        st_.offerMismatch++;
        if (unverifiedLogged_++ < 5)
            log_("resource: a load_rsc offer answered batch " + std::to_string(matched) + ", not the guessed " +
                 std::to_string(of.guess));
    }
    for (auto it = out_.begin(); it != out_.end();) {
        if (it->seq == m) {
            if (it->expired) st_.declExpired -= it->decls.size();
            if (!it->urgent && payloadBytes) {  // a demand's bytes say nothing about the next stretch of the list
                double obs = double(payloadBytes) / double(std::max<size_t>(1, it->decls.size()));
                bytesPerOrd_ = bytesPerOrd_ <= 0 ? obs : 0.5 * obs + 0.5 * bytesPerOrd_;
            }
            it = out_.erase(it);
            continue;
        }
        if (matched && it->seq == of.guess)
            it->answered = false;                       // still owed its answer (or skipped: handled just below)
        if (matched && it->seq < m && !it->answered) {  // DD answered past it: it is never coming
            if (it->expired) st_.declExpired -= it->decls.size();
            st_.declSkipped += it->decls.size();
            requeueFront(it->decls);
            it = out_.erase(it);
            continue;
        }
        ++it;
    }
}

void ResourcePump::finish(uint32_t rid, Xfer& x) {
    st_.transfersDone++;
    if (x.of.params.find("load_rsc") != std::string::npos)  // the guess first, then every batch unsettled now
        for (const Batch& b : out_) {
            if (b.seq == x.of.guess) continue;
            Cand c{b.seq, {}};
            for (const Decl& d : b.decls) c.ids.push_back(d.id);
            x.of.cands.push_back(std::move(c));
        }
    std::vector<uint32_t> order;
    for (auto& kv : x.parts) order.push_back(kv.first);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return x.offsets[a] < x.offsets[b]; });
    Bytes payload;
    for (uint32_t k : order) payload.insert(payload.end(), x.parts[k].begin(), x.parts[k].end());

    Bytes plain;
    uint32_t id = 0;
    if (verifiedForm(payload, rid, x.of, plain, id)) {
        if (x.of.params.rfind("##", 0) == 0) {
            params_[id] = x.of.params;
            repushed_.push_back(id);
            repushCount_++;
        }
        storeFile(id, plain, x.of.name);
        uint64_t matched = 0;
        for (const Cand& c : x.of.cands)
            if (std::find(c.ids.begin(), c.ids.end(), id) != c.ids.end()) {
                matched = c.seq;
                break;
            }
        settle(x.of, matched, payload.size());
        return;
    }
    // Not a whole file whose hash we know. A load_rsc offer may carry a gzip BUNDLE; magic bytes only pick
    // which form to TRY gunzipping - every record inside is still hash-verified before it is stored.
    Bytes forms[3] = {payload, x.of.want() ? decodeResource(payload, x.of.want()) : Bytes(),
                      decodeResource(payload, rid)};
    for (auto& f : forms) {
        std::map<uint64_t, int> hits;
        if (f.size() > 2 && f[0] == 0x1f && f[1] == 0x8b && storeBundle(f, x.of, hits)) {
            uint64_t matched = 0;
            int best = 0;
            for (auto& [seq, n] : hits)
                if (n > best || (n == best && seq == x.of.guess)) {
                    best = n;
                    matched = seq;
                }
            settle(x.of, matched, payload.size());
            return;
        }
    }
    settle(x.of, 0, payload.size());
    st_.unverified++;
    if (unverifiedLogged_++ < 5)
        log_("resource unverified: rid=" + hexEncode(ByteSpan(reinterpret_cast<uint8_t*>(&rid), 4)) +
             " name=" + x.of.name + " size=" + std::to_string(payload.size()) + " params=" + x.of.params);
}

}  // namespace fugue::net

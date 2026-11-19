// Resource pump: the ResourceList, ResourceDeclare declarations, Resource offer/pull/data, reassembly, decipher,
// VERIFY-by-resID, and a content-addressed on-disk store.
//
// The pump must answer from the first frame. DreamDaemon's resource channel is a per-connection serial queue advanced
// by the client's pull, and one unanswered Resource offer wedges it for the rest of the session. So onFrame() answers
// every offer immediately, solicited or not, before any slow work.
//
// Not thread-safe; one owner (the session's reader thread). It blocks on nothing but local disk I/O. Sends go through
// the Send callback on the same thread.
//
// Cipher and resID are as in resource.h. Offers are matched to declarations by content (respump.cpp). A layout
// disagreement surfaces as an unverified resource, never as accepted bytes.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <memory>
#include <vector>
#include "net/bytes.h"
#include "net/rscstore.h"

namespace fugue::net {

struct ResourceStats {
    uint64_t listEntries = 0;
    uint64_t declared = 0;    // ordinals declared (sent in ResourceDeclare frames)
    uint64_t declFrames = 0;  // ResourceDeclare frames sent (each batches 1..32 ordinals, sized by bytes)
    uint64_t declExpired =
        0;  // ordinals whose declaration is past the expiry and still unanswered (a late answer subtracts again)
    uint64_t declSkipped =
        0;  // ordinals of batches DD answered PAST (content said a later batch was answered): declared again
    uint64_t offerMismatch = 0;  // load_rsc offers whose content answered a batch other than the positional guess
    uint64_t offers = 0;         // Resource offers seen (pkt 0)
    uint64_t pulls = 0;          // pulls sent (must equal offers - the wedge rule)
    uint64_t dataFrames = 0;
    uint64_t transfersDone = 0;
    uint64_t bundles = 0;         // transfers that were gzip bundles
    uint64_t verified = 0;        // distinct files whose resID verified and were stored
    uint64_t alreadyHeld = 0;     // verified but already on disk (content-addressed)
    uint64_t unverified = 0;      // refused: no candidate decipher hashed to an id this world offered
    uint64_t protocolErrors = 0;  // bad packet numbers, total mismatch, conflicting resend
    uint64_t bytesStored = 0;
    uint64_t cacheServed = 0;      // list entries served from the on-disk cache, never declared
    uint64_t cacheRedeclared = 0;  // cached entries that failed verification on read: deleted, then declared after all
    uint64_t cacheNameless =
        0;  // cached but nameless entries declared anyway (the offer is the only source of the name)
};

class ResourcePump {
public:
    using Send = std::function<bool(uint16_t type, ByteSpan plainBody)>;
    using Log = std::function<void(const std::string&)>;
    ResourcePump(std::string storeDir, Send send, Log log);

    // ResourceList body: concatenated 5-byte [id u32 LE][kind u8]. Appends ordinals and queues declarations.
    void onResList(ByteSpan body);
    // Resource body (already decrypted under K_login).
    void onFrame(ByteSpan body);
    // Housekeeping: expire stale declarations and refill the window. Call at least ~1/s.
    void tick();

    const ResourceStats& stats() const { return st_; }
    const std::string& storeDir() const { return dir_; }
    RscStore& store() { return *rs_; }
    // Verified read of a stored/cached file (re-hashes; a bad file is deleted). On failure it is queued for declaration again.
    bool readStored(uint32_t id, Bytes& data);
    // Not yet resolved: queued + in-flight declarations + open transfers (a prefetch is done when this is 0 and the list is stable).
    size_t pending() const { return declQ_.size() + inFlight() + xfers_.size(); }
    // id -> name for everything stored this run (names come from offers / bundle records).
    const std::map<uint32_t, std::string>& storedThisRun() const { return stored_; }
    // The offer's params string for a whole-file transfer that verified under `id` (e.g.
    // "##action=browse_file;options=window%3dbrowseroutput"), "" when none. Lets the client act on a directive that
    // rides a file push, as browse(file(...)) does.
    const std::string& offerParams(uint32_t id) const {
        static const std::string none;
        auto it = params_.find(id);
        return it == params_.end() ? none : it->second;
    }
    // Ids whose whole-file offer carried a "##action=..." directive this run. The session reports them again even if a
    // warm join already reported the cached bytes, so the directive is not lost. Drains the list.
    std::vector<uint32_t> takeRepushed() {
        std::vector<uint32_t> v;
        v.swap(repushed_);
        return v;
    }
    uint64_t repushCount() const { return repushCount_; }
    size_t storedCount() const { return stored_.size(); }
    size_t listSize() const { return list_.size(); }
    size_t inFlight() const;                                   // ordinals declared, not yet answered, not expired
    size_t outstandingBatches() const { return out_.size(); }  // declared batches not yet settled (incl. expired ones)
    size_t nextBackgroundBatch() const;  // ordinals the next background ResourceDeclare would carry (byte-targeted)
    // Tests only: replace the steady clock (ms) so the declaration expiry can be driven without sleeping.
    void setClockForTest(std::function<uint64_t()> f) { clock_ = std::move(f); }
    size_t queued() const { return declQ_.size(); }
    // Moves the queued declarations for these ids to the front of the queue, in their own batches with their own window
    // (kMaxUrgentInFlight). Unknown or already-sent ids are ignored. DD serves them after the bundle it is streaming and
    // one byte-bounded background batch (respump.cpp).
    void prioritize(const std::vector<uint32_t>& ids);

    // Pure helpers, public for tests.
    // The sharded layout: <dir>/<2 hex>/<8 hex>.
    static std::string pathFor(const std::string& dir, uint32_t id);

private:
    struct Decl {
        uint32_t ordinal, id;
        bool urgent;
    };
    // One ResourceDeclare frame. `answered`: a load_rsc offer was positionally guessed onto it (its bundle is
    // streaming); `expired`: past kDeclExpiryMs unanswered - it no longer holds a window slot, but it STAYS in order,
    // because DD still answers it.
    struct Batch {
        uint64_t seq = 0;
        std::vector<Decl> decls;
        uint64_t sentMs = 0;
        bool urgent = false, expired = false, answered = false;
    };
    struct Cand {
        uint64_t seq;
        std::vector<uint32_t> ids;
    };
    // cands: the positional guess, then (filled in finish) every batch still unsettled when the bytes landed. Which one
    // the offer really answered is decided by content when the transfer completes (settle), never by position alone.
    struct Offer {
        std::string name, params;
        uint8_t kind = 0;
        uint32_t size = 0;
        uint64_t guess = 0;
        std::vector<Cand> cands;
        uint32_t want() const { return cands.empty() || cands[0].ids.empty() ? 0 : cands[0].ids[0]; }
    };
    struct Xfer {
        Offer of;
        uint32_t total = 0;
        std::map<uint32_t, Bytes> parts;       // packet number -> payload
        std::map<uint32_t, uint32_t> offsets;  // packet number -> byte offset
    };
    void pump();
    void finish(uint32_t rid, Xfer& x);
    bool verifiedForm(ByteSpan payload, uint32_t rid, const Offer& of, Bytes& out, uint32_t& idOut) const;
    bool storeFile(uint32_t id, ByteSpan data, const std::string& name);
    void declare(uint32_t ordinal, uint32_t id);
    bool storeBundle(ByteSpan gz, const Offer& of, std::map<uint64_t, int>& hits);
    void settle(const Offer& of, uint64_t matched, size_t payloadBytes);
    size_t openBatches(bool urgent) const;
    uint64_t nowMs() const;

    std::string dir_;
    std::unique_ptr<RscStore> rs_;
    Send send_;
    Log log_;
    ResourceStats st_;
    std::vector<uint32_t> list_;  // ordinal -> id
    std::unordered_set<uint32_t> known_;
    std::deque<Decl> declQ_;
    void requeueFront(const std::vector<Decl>& decls);
    std::deque<Batch> out_;  // sent ResourceDeclare frames not yet settled, oldest first (DD answers in this order)
    uint64_t seq_ = 0;
    double bytesPerOrd_ = 0;  // EWMA of bundle bytes per declared ordinal (0 = nothing measured yet)
    std::function<uint64_t()> clock_;
    std::map<uint32_t, Xfer> xfers_;
    std::map<uint32_t, std::string> stored_;
    std::map<uint32_t, std::string> params_;  // verified id -> the whole-file offer's params
    std::vector<uint32_t> repushed_;
    uint64_t repushCount_ = 0;
    int unverifiedLogged_ = 0;
};

}  // namespace fugue::net

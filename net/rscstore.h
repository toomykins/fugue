// RscStore: the on-disk, content-addressed resource cache, at ~/.fugue/rsc by default. The layout is a fixed format.
//
// LAYOUT:
//   <dir>/<top byte of id, 2 lowercase hex>/<id, 8 lowercase hex>   the file holds the resource's raw bytes and nothing else
//   <dir>/names                                                     append-only "<8 hex>\t<name>\n" sidecar, later line wins
//   dirs mode 0700, files and names mode 0600. Writes go to ".tmp-<random>" in the same shard and are renamed into
//   place, so an interrupted write leaves no entry. Names that are not 8 hex digits and shards that are not 2 hex
//   digits are ignored, never deleted. Default cap 1 GiB; eviction is least-recently-used (mtime) down to 90% of it.
//
// KEY = resID = CRC-32/XFER over the file bytes, so an entry is never stale and can be shared across worlds. put()
// refuses bytes that do not hash to the id, and get() re-hashes every read and deletes a file that fails. A name is
// best-effort metadata: "" means unknown.
//
// Not thread-safe (one owner, like ResourcePump). Never touches ~/.byond/cache, an encrypted envelope this client
// can neither verify nor write.
#pragma once
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include "net/bytes.h"

namespace fugue::net {

struct RscStoreStats {
    uint64_t files = 0, bytes = 0;
    uint64_t hits = 0, misses = 0, puts = 0, rejected = 0, evicted = 0, deletedBad = 0;
};

class RscStore {
public:
    static constexpr int64_t kDefaultCapBytes = int64_t(1) << 30;
    // Creates dir (0700) if needed and indexes it by stat only; nothing is hashed at open. ok() is false if dir is unusable.
    explicit RscStore(std::string dir, int64_t capBytes = 0);
    ~RscStore();
    RscStore(const RscStore&) = delete;
    RscStore& operator=(const RscStore&) = delete;

    bool ok() const { return ok_; }
    const std::string& dir() const { return dir_; }
    // <dir>/<2 hex>/<8 hex>
    static std::string pathFor(const std::string& dir, uint32_t id);
    // $HOME/.fugue/rsc ("" when HOME is unset - the caller then runs without a cache).
    static std::string defaultDir();

    // Index lookup only: no read, no verification (optimistic).
    bool has(uint32_t id) const { return index_.count(id) != 0; }
    bool hasName(uint32_t id) const {
        auto it = index_.find(id);
        return it != index_.end() && !it->second.name.empty();
    }
    // The recorded name, or "" if unknown.
    std::string nameOf(uint32_t id) const {
        auto it = index_.find(id);
        return it == index_.end() ? std::string() : it->second.name;
    }
    // Reads + RE-HASHES. A failing/vanished file is dropped from the index and DELETED; returns false. name may be "".
    bool get(uint32_t id, Bytes& data, std::string* name = nullptr);
    // Stores iff resId(data) == id; a mismatch counts as rejected. Empty data and files above the cap are not stored.
    // If the id is already held, only a new name is recorded.
    bool put(uint32_t id, const std::string& name, ByteSpan data);

    RscStoreStats stats() const;
    // Counts or removes the files above, plus the names log.
    static void usage(const std::string& dir, uint64_t& files, uint64_t& bytes);
    static void clear(const std::string& dir, uint64_t* files = nullptr, uint64_t* bytes = nullptr);

private:
    struct Entry {
        int64_t size = 0;
        std::string name;
        time_t used = 0;
    };
    void scan();
    void loadNames();
    void appendName(uint32_t id, const std::string& name);
    void forget(uint32_t id);
    void evict();

    std::string dir_;
    int64_t cap_;
    bool ok_ = false;
    std::unordered_map<uint32_t, Entry> index_;
    int64_t bytes_ = 0;
    FILE* nameLog_ = nullptr;
    mutable RscStoreStats st_;
};

}  // namespace fugue::net

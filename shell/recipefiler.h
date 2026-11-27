// Session-worker adapter: DMI sheets and DDMI browse_rsc resources become first-cell PNGs.
// No model access or decoding on the GUI/HTTP thread; only complete PNGs are published.
#pragma once
#include "net/bytes.h"
#include "shell/browsestore.h"
#include "shell/iconcache.h"
#include <optional>

namespace fugue::shell {
class BrowseRecipeFiler {
public:
    static constexpr size_t maxPending = 256;
    static constexpr size_t maxAttempts = 32;
    static constexpr size_t maxOrdinalScan = 4096;
    static constexpr uint64_t maxExportPixels = 8 * 1024 * 1024;
    // Returns true for queued PNGs and all DDMI, including refused jobs: publish only after
    // export. A PNG that cannot be queued returns false so the caller files it unchanged.
    // Non-DMI PNGs retain their original bytes; other images/media bypass decoding.
    // The verified resource must also be available through pump's reader.
    bool onResource(uint32_t id, const std::string& name, ByteSpan data);
    // Called each UI tick, even without new resources, to resume cooperative jobs.
    void pump(const IconCache::Resources& resources, const IconCache::Read& read, BrowseRscStore& store);
    size_t pending() const { return pending_.size(); }

private:
    struct Pending {
        std::string name;
        std::optional<uint32_t> ordinal;
        bool png = false;
    };
    IconCache cache_;
    std::map<uint32_t, Pending> pending_;
    size_t scan_ = 0;
    uint32_t next_ = 0;
};
}  // namespace fugue::shell

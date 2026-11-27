// Worker-only image/DMI and bounded DDMI resource dependency cache.
#pragma once
#include "shell/dmiicon.h"
#include <functional>
#include "render/ddmi.h"
#include <vector>

namespace fugue::shell {
class IconCache {
public:
    using Read = std::function<bool(uint32_t, QByteArray&)>;
    using Resources = std::vector<std::pair<uint32_t, uint8_t>>;
    enum class Status { Unmapped, Missing, Ready, MalformedSource, UnsupportedRecipe,
                        DependencyUnavailable, DependencyEmpty, Cycle, WorkLimit, PixelLimit, InvalidRecipeResult,
                        Deferred, SafetyLimit };
    static const char* statusName(Status status);
    struct Failure { int offset = -1, operation = -1; };
    struct Result {
        uint32_t id = 0;
        uint64_t revision = 0;
        bool mapped = false, ready = false;
        Status status = Status::Unmapped;
        // Diagnostics must not alter dependency readiness/retry signatures.
        bool operator==(const Result& b) const {
            return id == b.id && revision == b.revision && mapped == b.mapped && ready == b.ready;
        }
    };
    // One epoch per preparation; even completed recipes recheck their dependency signatures.
    void begin(const Resources& resources, const Read& read, uint64_t decodePixels = 64 * 1024 * 1024);
    Result lookup(uint32_t ordinal);
    // A bounded consumer may decline available bytes for work/size policy reasons.
    // Call only inside its Read callback. This changes evidence, never readiness.
    void readLimited() { readStatus_ = Status::WorkLimit; }
    const DmiIcon& icon(uint32_t id) const { return entries_.at(id).icon; }
    uint64_t reads() const { return reads_; }
    struct Metrics {
        uint64_t starts = 0, completed = 0, yields = 0, cacheHits = 0, safetyRejections = 0, warnings = 0;
        uint64_t workPixels = 0, lastRecipePixels = 0, peakBytes = 0, maxSliceNs = 0;
    };
    const Metrics& metrics() const { return metrics_; }
    struct Observation { uint32_t ordinal = 0; Result result; Failure failure; unsigned depth = 0; };
    // Read cached evidence only: never resolves, loads or charges work. At most 16 nodes.
    std::vector<Observation> observations(uint32_t ordinal, const Result* observed = nullptr) const;
    // Optional retention limit for small consumers (INFO thumbnails). Call only
    // after resolving/using a whole dependency tree; not while holding icon refs.
    void trim(size_t maxEntries, uint64_t maxBytes);
private:
    struct Entry {
        uint64_t checked = 0, revision = 0, bytes = 0;
        bool loaded = false, recipe = false, ready = false, active = false, evaluated = false;
        Status status = Status::Missing;
        Failure failure;
        DmiIcon icon;
        assets::DdmiRecipe program;
        std::vector<Result> dependencies;
    };
    using Clock = assets::DdmiEvaluation::Clock;
    struct Pending {
        uint32_t id = 0;
        uint64_t revision = 0, copyPixels = 0, slices = 0;
        Clock::time_point started;
        std::unique_ptr<assets::DdmiEvaluation> evaluation;
    };
    void advance();
    void account(Entry& entry);
    Result resolve(uint32_t ordinal, unsigned depth);
    std::map<uint32_t, Entry> entries_;
    const Resources* resources_ = nullptr;
    Read read_;
    Status readStatus_ = Status::Missing;
    // At most one in-flight recipe per cache: <=64 MiB live pixel storage,
    // independent of retained assets. Resume once per preparation, not per lookup.
    Pending pending_;
    Metrics metrics_;
    Clock::time_point deadline_{}, lastWarning_{};
    uint64_t generatedBytes_ = 0; // new completed outputs share the 64 MiB job allowance
    uint64_t epoch_ = 0, revision_ = 0, reads_ = 0, work_ = 0, pixels_ = 0, decodeRemaining_ = 0, retainedBytes_ = 0, readRemaining_ = 0;
};
}

#include "shell/iconcache.h"
#include <algorithm>
#include <cstdio>

namespace fugue::shell {
namespace {
// An undrawable icon makes its atom invisible: say so once per resource.
// FUGUE_DUMP_REFUSED=<dir> also writes the refused bytes there (they are only at hand when the source is read).
void reportRefused(uint32_t id, const char* why, const QByteArray* bytes = nullptr) {
    std::fprintf(stderr, "[icon] UNDRAWABLE res %08x: %s\n", id, why);
    static const QByteArray dumpDir = qgetenv("FUGUE_DUMP_REFUSED");
    if (dumpDir.isEmpty() || !bytes) return;
    const QByteArray path = dumpDir + "/" + QByteArray::number(id, 16).rightJustified(8, '0');
    if (FILE* f = std::fopen(path.constData(), "wb")) {
        std::fwrite(bytes->constData(), 1, size_t(bytes->size()), f);
        std::fclose(f);
    }
}
}
const char* IconCache::statusName(Status status) {
    switch (status) {
#define S(x) case Status::x: return #x
    S(Unmapped); S(Missing); S(Ready); S(MalformedSource); S(UnsupportedRecipe);
    S(DependencyUnavailable); S(DependencyEmpty); S(Cycle); S(WorkLimit); S(PixelLimit); S(InvalidRecipeResult);
    S(Deferred); S(SafetyLimit);
#undef S
    }
    return "Unobserved";
}
std::vector<IconCache::Observation> IconCache::observations(uint32_t ordinal, const Result* observed) const {
    std::vector<Observation> out;
    auto visit = [&](auto&& self, uint32_t ord, unsigned depth, const Result* recorded) -> void {
        if (out.size() >= 16 || depth >= 8) return;
        Observation o; o.ordinal = ord; o.depth = depth;
        if (!resources_ || ord >= resources_->size()) { out.push_back(o); return; }
        const auto it = entries_.find((*resources_)[ord].first);
        if (it == entries_.end()) return;
        const auto& e = it->second;
        o.result = recorded ? *recorded : Result{it->first, e.revision, true, e.ready, e.status};
        if (o.result.status == Status::UnsupportedRecipe || o.result.status == Status::SafetyLimit) o.failure = e.failure;
        out.push_back(o);
        size_t dep = 0;
        for (const auto& ref : e.program.refs) if (!assets::ddmiRefIsEmptyIcon(ref)) {
            if (dep < e.dependencies.size()) self(self, ref.ordinal, depth + 1, &e.dependencies[dep]);
            ++dep;
        }
    };
    visit(visit, ordinal, 0, observed);
    return out;
}
void IconCache::trim(size_t maxEntries, uint64_t maxBytes) {
    if (entries_.size() <= maxEntries && retainedBytes_ <= maxBytes) return;
    std::vector<std::pair<uint64_t,uint32_t>> order;
    order.reserve(entries_.size());
    for (const auto& [id, entry] : entries_) order.emplace_back(entry.checked,id);
    std::sort(order.begin(),order.end());
    for (const auto& [epoch,id] : order) {
        if (entries_.size() <= maxEntries && retainedBytes_ <= maxBytes) break;
        const auto it = entries_.find(id);
        retainedBytes_ -= it->second.bytes;
        entries_.erase(it);
    }
}
void IconCache::account(Entry& e) {
    retainedBytes_ -= e.bytes;
    e.bytes = sizeof(Entry) + 128 + e.icon.storageBytes() + e.dependencies.capacity() * sizeof(Result)
        + e.program.ops.capacity() * sizeof(assets::DdmiOp) + e.program.refs.capacity() * sizeof(assets::DdmiRef);
    for (const auto& op : e.program.ops) e.bytes += op.payload.capacity();
    for (const auto& ref : e.program.refs) e.bytes += ref.name.capacity();
    retainedBytes_ += e.bytes;
}
void IconCache::advance() {
    if (!pending_.evaluation) return;
    const auto it = entries_.find(pending_.id);
    if (it == entries_.end() || it->second.revision != pending_.revision) { pending_ = {}; return; }
    const auto start = Clock::now();
    if (start >= deadline_) return;
    ++pending_.slices;
    const auto before = pending_.evaluation->workPixels();
    const bool done = pending_.evaluation->step(deadline_, 64 * 1024 * 1024 - generatedBytes_);
    const auto work = pending_.evaluation->workPixels() - before;
    metrics_.workPixels += work;
    pixels_ += work;
    metrics_.peakBytes = std::max(metrics_.peakBytes, pending_.evaluation->peakBytes());
    if (!done) ++metrics_.yields;
    else {
        auto& e = it->second;
        auto result = pending_.evaluation->takeResult();
        metrics_.lastRecipePixels = pending_.copyPixels + pending_.evaluation->workPixels();
        ++metrics_.completed;
        if (result.limited) {
            e.status = Status::SafetyLimit; e.failure = {result.failureOffset, result.failureOperation};
            ++metrics_.safetyRejections;
        }
        else if (result.icon()) {
            e.icon = DmiIcon::fromAsset(*result.icon());
            e.icon.generated = true;
            e.status = !e.icon.metadata ? Status::InvalidRecipeResult : Status::Ready;
        } else {
            e.status = Status::UnsupportedRecipe;
            e.failure = {result.failureOffset, result.failureOperation};
        }
        if (e.status != Status::Ready) reportRefused(it->first, statusName(e.status));
        // Permanent safety/unsupported refusals are stable empty assets, just
        // like malformed sources. Never repeat their expensive prefix each frame.
        e.evaluated = e.ready = true;
        e.revision = ++revision_;
        account(e);
        generatedBytes_ += e.icon.storageBytes();
        const auto now = Clock::now();
        if ((pixels_ > 16 * 1024 * 1024 || metrics_.lastRecipePixels > 16 * 1024 * 1024) &&
            (lastWarning_ == Clock::time_point{} || now - lastWarning_ >= std::chrono::seconds(5))) {
            lastWarning_ = now;
            ++metrics_.warnings;
            std::fprintf(stderr, "[generated-icon] id=%u status=%s work=%llu preparationWork=%llu advisory=16777216 peakBytes=%llu slices=%llu elapsedMs=%.3f\n",
                pending_.id, statusName(e.status), static_cast<unsigned long long>(metrics_.lastRecipePixels),
                static_cast<unsigned long long>(pixels_), static_cast<unsigned long long>(pending_.evaluation->peakBytes()),
                static_cast<unsigned long long>(pending_.slices),
                std::chrono::duration<double, std::milli>(now - pending_.started).count());
        }
        pending_ = {};
    }
    metrics_.maxSliceNs = std::max(metrics_.maxSliceNs,
        uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
}
void IconCache::begin(const Resources& resources, const Read& read, uint64_t decodePixels) {
    // Trim only between preparations, never while resolving a dependency tree.
    // Published QImages/metadata remain alive through their immutable owners.
    // Dense 71x71 views can keep >128 MiB of source sheets live. Trimming
    // below that working set reloads hot icons every preparation, invalidating
    // dependent templates and resident frame/texture identities as well.
    trim(4096, 256 * 1024 * 1024);
    resources_ = &resources; read_ = read; ++epoch_; reads_ = work_ = pixels_ = generatedBytes_ = 0;
    decodeRemaining_ = std::min<uint64_t>(decodePixels, 64 * 1024 * 1024);
    readRemaining_ = 128 * 1024 * 1024;
    // Recipe time only: resource decoding keeps its independent allocation/work
    // protections. One atomic operation may overrun this cooperative 2ms slice.
    deadline_ = Clock::time_point{};
    if (pending_.evaluation) {
        deadline_ = Clock::now() + std::chrono::milliseconds(2);
        advance();
    }
}
IconCache::Result IconCache::lookup(uint32_t ordinal) { return resolve(ordinal, 0); }
IconCache::Result IconCache::resolve(uint32_t ordinal, unsigned depth) {
    if (!resources_ || ordinal >= resources_->size()) return {};
    const uint32_t id = (*resources_)[ordinal].first;
    if (depth >= 32 || ++work_ > 65536) return {id, 0, true, false, Status::WorkLimit};
    auto& e = entries_[id];
    if (e.active) return {id, 0, true, false, Status::Cycle};
    if (e.checked == epoch_) return {id, e.revision, true, e.ready, e.status};
    e.checked = epoch_;
    bool accountingDirty = e.bytes == 0;
    auto observed = [&]() -> Result {
        if (!accountingDirty) return {id, e.revision, true, e.ready, e.status};
        account(e);
        return {id, e.revision, true, e.ready, e.status};
    };
    if (!e.loaded) {
        accountingDirty = true;
        QByteArray bytes;
        if (reads_ >= 256) { e.status = Status::WorkLimit; return observed(); }
        ++reads_;
        readStatus_ = Status::Missing;
        if (!read_ || !read_(id, bytes)) { e.status = readStatus_; return observed(); }
        if (uint64_t(bytes.size()) > readRemaining_) { e.status = Status::WorkLimit; return observed(); }
        readRemaining_ -= uint64_t(bytes.size());
        e.loaded = true;
        e.recipe = bytes.startsWith("DDMI");
        if (e.recipe) {
            std::string error;
            if (!assets::parseDdmi(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()), e.program, &error)) {
                const int offset = e.program.failureOffset;
                e.failure = {offset, offset >= 8 && offset < bytes.size() ? int(uint8_t(bytes[offset])) : -1};
                e.recipe = false; e.program = {}; e.status = Status::UnsupportedRecipe;
                reportRefused(id, error.empty() ? statusName(e.status) : error.c_str(), &bytes);
            }
        } else {
            if (!decodeRemaining_) { e.loaded = false; e.status = Status::PixelLimit; return observed(); }
            bool limited = false;
            e.icon = DmiIcon::decode(bytes, decodeRemaining_, &limited);
            if (limited) {
                decodeRemaining_ = 0;
                e.loaded = false; e.status = Status::PixelLimit;
                return observed();
            }
            // A permanent decode refusal may have consumed substantial work
            // before detecting corruption. Do not multiply that work by the
            // resource count in one preparation. Other sources retry next epoch.
            if (e.icon.sheet.isNull()) decodeRemaining_ = 0;
            else decodeRemaining_ -= uint64_t(e.icon.sheet.width()) * e.icon.sheet.height();
            e.status = e.icon.sheet.isNull() ? Status::MalformedSource : Status::Ready;
            if (e.icon.sheet.isNull()) reportRefused(id, statusName(e.status), &bytes);
        }
        e.revision = ++revision_;
        if (!e.recipe) e.ready = true; // permanent refusals are stable empty assets
    }
    if (e.recipe) {
        e.active = true;
        std::vector<Result> dependencies;
        bool ready = true;
        for (const auto& ref : e.program.refs) if (!assets::ddmiRefIsEmptyIcon(ref)) {
            dependencies.push_back(resolve(ref.ordinal, depth + 1));
            ready &= dependencies.back().ready;
        }
        e.active = false;
        if (!e.evaluated || e.dependencies != dependencies) {
            accountingDirty = true;
            e.icon = {}; e.ready = false; e.revision = ++revision_;
            e.dependencies = dependencies;
            e.evaluated = !ready;
            if (!ready) { e.status = Status::DependencyUnavailable; return observed(); }
            if (pending_.evaluation && pending_.id == id && pending_.revision != e.revision) pending_ = {};
            constexpr uint64_t limit = 16 * 1024 * 1024;
            // Charge copies into the interpreter BEFORE allocating. No cached
            // dependency pixels or metadata can be mutated by the interpreter.
            uint64_t copyPixels = 0, copyBytes = 256 * 1024;
            bool empty = false;
            for (const auto& dep : dependencies) {
                const auto& source = icon(dep.id);
                empty |= source.sheet.isNull() && !source.metadata;
                copyPixels += uint64_t(source.sheet.width()) * source.sheet.height();
                copyBytes += sizeof(assets::Icon) + source.storageBytes();
            }
            if (empty) {
                e.status = Status::DependencyEmpty; e.evaluated = e.ready = true;
            } else if (copyPixels > limit || copyBytes > 32 * 1024 * 1024) {
                e.status = Status::SafetyLimit; e.evaluated = e.ready = true;
                ++metrics_.safetyRejections;
            } else {
                if (deadline_ == Clock::time_point{}) deadline_ = Clock::now() + std::chrono::milliseconds(2);
                if (pending_.evaluation || Clock::now() >= deadline_ || copyBytes * 2 > 64 * 1024 * 1024 - generatedBytes_) {
                    e.status = Status::Deferred;
                    return observed();
                }
                const auto start = Clock::now();
                std::vector<assets::Icon> inputs;
                inputs.reserve(e.program.refs.size());
                size_t dep = 0;
                for (const auto& ref : e.program.refs)
                    inputs.push_back(assets::ddmiRefIsEmptyIcon(ref) ? assets::Icon{} : icon(dependencies[dep++].id).toAsset());
                pending_ = {id, e.revision, copyPixels, 0, start,
                    std::make_unique<assets::DdmiEvaluation>(e.program, std::move(inputs))};
                ++metrics_.starts;
                metrics_.workPixels += copyPixels;
                pixels_ += copyPixels;
                e.status = Status::Deferred;
                // Evaluated means a job was started; pending work must not restart
                // on the next lookup. Dependency changes cancel by revision above.
                e.evaluated = true;
                advance();
                metrics_.maxSliceNs = std::max(metrics_.maxSliceNs,
                    uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()));
            }
        }
        else if (e.ready) ++metrics_.cacheHits;
        for (size_t i = 0; i < e.dependencies.size(); ++i) e.dependencies[i].status = dependencies[i].status;
    }
    return observed();
}
}

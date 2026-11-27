#include "shell/recipefiler.h"
#include <QBuffer>
#include <algorithm>
#include <cstring>

namespace fugue::shell {
bool BrowseRecipeFiler::onResource(uint32_t id, const std::string& name, ByteSpan data) {
    const bool png = data.size() >= 8 && std::memcmp(data.data(), "\x89PNG\r\n\x1a\n", 8) == 0;
    if (!png && (data.size() < 4 || std::memcmp(data.data(), "DDMI", 4) != 0)) return false;
    const bool queued =
        name.size() <= 4096 && !BrowseRscStore::blind(name) && (pending_.count(id) || pending_.size() < maxPending);
    if (queued) pending_[id] = {name, {}, png};
    // A PNG that cannot be queued is still an ordinary image: let the caller file
    // its original bytes. DDMI is consumed either way (never file raw instructions).
    return queued || !png;
}

void BrowseRecipeFiler::pump(const IconCache::Resources& resources, const IconCache::Read& read,
                             BrowseRscStore& store) {
    if (pending_.empty()) return;
    // Resource ids arrive independently of ordinal mappings. Scan incrementally,
    // not once per recipe; cached ordinals are revalidated before every lookup.
    for (size_t n = 0; n < std::min(resources.size(), maxOrdinalScan); ++n) {
        if (scan_ >= resources.size()) scan_ = 0;
        const auto it = pending_.find(resources[scan_].first);
        if (it != pending_.end()) it->second.ordinal = uint32_t(scan_);
        ++scan_;
    }
    cache_.begin(resources, read, maxExportPixels);
    uint64_t pixels = 0;
    const size_t attempts = std::min(pending_.size(), maxAttempts);
    auto it = pending_.lower_bound(next_);
    for (size_t n = 0; n < attempts && !pending_.empty(); ++n) {
        if (it == pending_.end()) it = pending_.begin();
        const auto current = it++;
        if (current->second.png) {
            // Ordinary browse_rsc images need not have an icon ordinal. Decode
            // by resource id once, on the worker, never in an HTTP request.
            // At most one bounded PNG decode/export per pump, sharing the export
            // budget with recipes. Retain no raw payloads in the pending queue.
            if (pixels) continue;
            pixels = maxExportPixels;
            QByteArray bytes;
            if (!read || !read(current->first, bytes)) continue;
            if (bytes.size() <= (32 << 20)) {
                const auto icon = DmiIcon::decode(bytes, maxExportPixels);
                if (!icon.sheet.isNull() && icon.metadata && !icon.metadata->empty() &&
                    (icon.metadata->size() > 1 || icon.metadata->front().dirs * icon.metadata->front().frames > 1)) {
                    const auto cell = icon.frame(int(icon.metadata->front().firstCell));
                    QByteArray png;
                    QBuffer buffer(&png);
                    if (!cell.isNull() && buffer.open(QIODevice::WriteOnly) && cell.save(&buffer, "PNG"))
                        bytes = std::move(png);
                }
                // Plain images, single cells and decode refusals stay unchanged.
                store.put(current->first, current->second.name, bytes.toStdString());
            }
            pending_.erase(current);
            continue;
        }
        const auto ordinal = current->second.ordinal;
        if (!ordinal || *ordinal >= resources.size() || resources[*ordinal].first != current->first) continue;
        const auto result = cache_.lookup(*ordinal);
        if (!result.ready) continue;  // missing dependencies, read limits or cooperative continuation
        const auto& icon = cache_.icon(result.id);
        const uint64_t area = uint64_t(icon.cell.width()) * uint64_t(icon.cell.height());
        bool finished = result.status != IconCache::Status::Ready || icon.sheet.isNull() || !icon.metadata ||
                        icon.metadata->empty() || area > maxExportPixels;
        if (!finished && pixels + area <= maxExportPixels) {
            pixels += area;
            // Original metadata order, not the alphabetically sorted state map.
            // Browsers get a still first direction/frame, never an animated sheet.
            const QImage cell = icon.frame(int(icon.metadata->front().firstCell));
            QByteArray png;
            QBuffer buffer(&png);
            if (!cell.isNull() && buffer.open(QIODevice::WriteOnly) && cell.save(&buffer, "PNG"))
                store.put(current->first, current->second.name, std::string(png.constData(), size_t(png.size())));
            finished = true;
        }
        if (finished) pending_.erase(current);
    }
    next_ = it == pending_.end() ? 0 : it->first;
}
}  // namespace fugue::shell

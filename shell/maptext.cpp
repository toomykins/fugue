#include "shell/maptext.h"
#include "render/maptextpaint.h"
#include "shell/ttmetrics.h"
#include <QRegularExpression>
#include <cmath>
#include <cstring>
#include <mutex>

namespace fugue::shell {
namespace {
// The glyph and TrueType hooks of render/dsmaptext.h, for a cache made before (or without) the application's own call.
void installText() {
    static std::once_flag once;
    std::call_once(once, [] { installTrueTypeMetrics(); installMaptextGlyphs(); });
}
}
bool MapTextCache::setStyle(const MapTextStyle& style) {
    if (style_ == style) return false;
    style_ = style;
    ++styleRevision_;
    // The control's own font is the base under its style sheet: a later body rule does not replace an earlier one's
    // property. Drawn, a control without a font size is 8pt (stock 516.1689);
    // MeasureText keeps DreamSeeker's 10pt.
    sheet_ = style.sheet.toStdString();
    static const QRegularExpression family(R"(^[a-zA-Z0-9 ,'"_-]{1,128}$)");
    if (family.match(style.family).hasMatch()) sheet_ += "body{font-family:" + style.family.toStdString() + "}";
    drawSheet_ = sheet_;
    if (std::isfinite(style.points) && style.points >= 1 && style.points <= 96) {
        sheet_ += "body{font-size:" + QString::number(style.points).toStdString() + "pt}";
        drawSheet_ = sheet_;
    } else drawSheet_ += "body{font-size:8pt}";
    return true;
}
QSize MapTextCache::measure(const QString& html, const QString& inlineStyle, int width) const {
    // DreamSeeker's own answer (render/dsmaptext.h), from the layout the map text is drawn with.
    installText();
    const auto answer = render::dsMeasureTextAnswer(html.toStdString(), sheet_, inlineStyle.toStdString(), width);
    return answer.width == 0xffff && answer.height == 0xffff ? QSize(-1,-1) : QSize(answer.width, answer.height);
}
void MapTextCache::begin() { ++epoch_; rasters_ = attempts_ = 0; pixels_ = 0; }
MapTextCache::Result MapTextCache::lookup(const QString& html, QSize size) {
    constexpr uint64_t maxBytes = 16 * 1024 * 1024;
    if (html.isEmpty() || html.size() > 16384 || size.width() <= 0 || size.height() <= 0 ||
        size.width() > 1024 || size.height() > 1024 || uint64_t(size.width()) * size.height() > 262144) return {};
    const Key key{html, size.width(), size.height(), styleRevision_ + (resourceFontGeneration() << 32)};
    if (auto it = entries_.find(key); it != entries_.end()) {
        it->second.used = epoch_;
        return it->second.result;
    }
    const uint64_t pixels = uint64_t(size.width()) * size.height(), bytes = pixels * 4;
    if (attempts_ >= 16 || pixels_ + pixels > 2 * 1024 * 1024) return {Status::Deferred, {}};
    ++attempts_;
    // Images pinned by active templates count against the cache limit.
    // Do not evict those entries and then allocate unbounded replacement rasters.
    while (entries_.size() >= 128 || bytes_ + bytes > maxBytes) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it)
            if ((!it->second.result.image || it->second.result.image.use_count() == 1) &&
                (oldest == entries_.end() || it->second.used < oldest->second.used)) oldest = it;
        if (oldest == entries_.end()) return {Status::Deferred, {}};
        if (oldest->second.result.image) bytes_ -= uint64_t(oldest->second.result.image->sizeInBytes());
        entries_.erase(oldest);
    }
    // DreamSeeker's layout and, where the host has them, its bitmap fonts (render/maptextpaint.h). Text with nothing
    // to draw is still a box.
    installText();
    int pad = 0;
    const auto sprite = render::paintMaptext(html.toStdString(), drawSheet_, size.width(), size.height(), &pad);
    QImage raster(sprite ? QSize(sprite->w, sprite->h) : size, QImage::Format_RGBA8888);
    if (raster.isNull()) return {Status::Deferred, {}};
    raster.fill(Qt::transparent);
    if (sprite)
        for (int y = 0; y < raster.height(); ++y)
            std::memcpy(raster.scanLine(y), sprite->px.data() + size_t(y) * size_t(raster.width()) * 4, size_t(raster.width()) * 4);
    const Result result{Status::Ready, std::make_shared<const QImage>(std::move(raster)), sprite ? pad : 0};
    ++rasters_; pixels_ += pixels; bytes_ += uint64_t(result.image->sizeInBytes());
    entries_.emplace(key, Entry{result, epoch_});
    return result;
}
}

// Worker-only, bounded maptext raster cache. Published images are immutable.
#pragma once
#include <QImage>
#include <QString>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace fugue::shell {
struct MapTextStyle {
    QString sheet, family;
    double points = 0; // zero retains the renderer default
    bool operator==(const MapTextStyle&) const = default;
};
class MapTextCache {
public:
    enum class Status { Ready, Unsupported, Deferred };
    struct Result {
        Status status = Status::Unsupported;
        std::shared_ptr<const QImage> image;
        int pad = 0; // the image is the box with this many pixels round it, where an outline or shadow reaches
    };
    void begin();
    // Returns true when raster inputs change. Existing templates retain their raster
    // until appearance invalidation; newly built templates use the new style.
    bool setStyle(const MapTextStyle& style);
    Result lookup(const QString& html, QSize size);
    // The layout drawing uses; (-1,-1) is DreamSeeker's "cannot measure". Width 0 is intrinsic.
    QSize measure(const QString& html, const QString& inlineStyle, int width) const;
    unsigned rasterizations() const { return rasters_; }
private:
    using Key = std::tuple<QString, int, int, uint64_t>;
    struct Entry { Result result; uint64_t used = 0; };
    std::map<Key, Entry> entries_;
    MapTextStyle style_;
    std::string sheet_, drawSheet_; // the style as render/dsmaptext.h reads it, for MeasureText and for drawing
    uint64_t epoch_ = 0, bytes_ = 0, pixels_ = 0, styleRevision_ = 0;
    unsigned rasters_ = 0, attempts_ = 0;
};
}

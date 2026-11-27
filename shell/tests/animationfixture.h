// CPU observation adapter for flat animation fixtures. ReplayFixture prepares
// the scene; this does not exercise GPU composition or native presentation.
#pragma once
#include "shell/entitymap.h"
#include <QPainter>

struct AnimationFixture {
    static QImage raster(const fugue::shell::TurfMap::Frame& frame, const fugue::shell::MapSprites& sprites) {
        if (frame.image.isNull()) return {};
        QImage out(frame.source.size(), QImage::Format_ARGB32_Premultiplied); out.fill(Qt::black);
        QPainter painter(&out);
        for (const auto& sprite : sprites) {
            if (sprite.image.isNull()) continue;
            QImage tinted = sprite.image.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < tinted.height(); ++y) for (int x = 0; x < tinted.width(); ++x) {
                const auto c = tinted.pixelColor(x,y);
                tinted.setPixelColor(x,y,QColor(int(std::lround(c.red()*sprite.tint[0])), int(std::lround(c.green()*sprite.tint[1])),
                    int(std::lround(c.blue()*sprite.tint[2])), int(std::lround(c.alpha()*sprite.tint[3]))));
            }
            painter.drawImage(QRect(sprite.position - frame.source.topLeft(), sprite.size), tinted);
        }
        return out;
    }
};

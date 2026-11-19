// GUI-side diagnostics: 2 Hz text updates and a 50 ms event-loop probe, only while the overlay is enabled.
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <map>
#include "app/rendermetrics.h"

class RenderThread;
namespace fugue::shell {
class SkinWindow;
}

class RenderMonitor : public QObject {
public:
    RenderMonitor(RenderThread& renderer, fugue::shell::SkinWindow& window);
    void refresh();
    bool active() const { return timer_.isActive(); }
    bool probing() const { return probe_.isActive(); }

private:
    void setEnabled(bool enabled);
    QString formatMap(const std::string& id, const fugue::app::MapMetrics& map, double rate) const;
    RenderThread& renderer_;
    fugue::shell::SkinWindow& window_;
    QTimer timer_, probe_;
    QElapsedTimer interval_, lastProbe_;
    fugue::app::EventLoopHealth health_;
    std::map<std::string, uint64_t> previousFrames_;
    QString text_;
    std::map<std::string, QString> auxText_;
};

// Client-owned display actions, shared by the system menu and Options window; never part of the world's skin.
#pragma once
#include <QObject>

class QAction;
class QMenu;

namespace fugue::shell {
class DisplaySettings : public QObject {
    Q_OBJECT
public:
    explicit DisplaySettings(QObject* parent = nullptr);
    void addToMenu(QMenu* client);
    void setGpuAvailable(bool available);  // the menu entries exist only in GPU builds
    bool gpuAvailable() const { return gpuAvailable_; }
    void setUseGpu(bool enabled);
    bool useGpu() const { return useGpu_; }
    void setPerformanceOverlay(bool enabled);
    bool performanceOverlay() const;
    void setPerformanceDetails(bool enabled);
    bool performanceDetails() const;
signals:
    void rendererChanged(bool useGpu);
    void performanceOverlayChanged(bool enabled);
    void performanceDetailsChanged(bool enabled);

private:
    bool gpuAvailable_ = false, useGpu_ = false;
    QAction *cpu_, *gpu_, *metrics_, *details_;
};
}  // namespace fugue::shell

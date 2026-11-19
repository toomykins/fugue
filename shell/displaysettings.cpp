#include "shell/displaysettings.h"
#include <QAction>
#include <QActionGroup>
#include <QMenu>

namespace fugue::shell {
DisplaySettings::DisplaySettings(QObject* parent) : QObject(parent) {
    auto* group = new QActionGroup(this);
    group->setExclusive(true);
    cpu_ = new QAction("&CPU", this);
    gpu_ = new QAction("&GPU (experimental)", this);
    cpu_->setObjectName("rendererCpu");
    gpu_->setObjectName("rendererGpu");
    for (auto* action : {cpu_, gpu_}) {
        action->setCheckable(true);
        group->addAction(action);
    }
    cpu_->setChecked(true);
    setGpuAvailable(false);
    connect(cpu_, &QAction::triggered, this, [this] { setUseGpu(false); });
    connect(gpu_, &QAction::triggered, this, [this] { setUseGpu(true); });
    metrics_ = new QAction("&Performance metrics", this);
    metrics_->setObjectName("performanceMetrics");
    metrics_->setCheckable(true);
    details_ = new QAction("&Detailed performance metrics", this);
    details_->setObjectName("performanceDetails");
    details_->setCheckable(true);
    details_->setEnabled(false);
    details_->setToolTip("Per-map CPU stages, sample counts and counters; adds timing instrumentation");
    connect(metrics_, &QAction::toggled, details_, &QAction::setEnabled);
    connect(metrics_, &QAction::toggled, this, &DisplaySettings::performanceOverlayChanged);
    connect(details_, &QAction::toggled, this, &DisplaySettings::performanceDetailsChanged);
}

void DisplaySettings::addToMenu(QMenu* client) {
    auto* renderer = client->addMenu("&Renderer");
    renderer->setObjectName("rendererMenu");
    renderer->setToolTipsVisible(true);
    renderer->addAction(cpu_);
    renderer->addAction(gpu_);
    client->addAction(metrics_);
    client->addAction(details_);
}

void DisplaySettings::setGpuAvailable(bool available) {
    gpuAvailable_ = available;
    gpu_->setEnabled(available);
    gpu_->setToolTip(available ? "Experimental GPU renderer; unsupported frames use CPU"
                               : "GPU support was not built (FUGUE_BUILD_GPU=OFF)");
    if (!available) setUseGpu(false);
}

void DisplaySettings::setUseGpu(bool enabled) {
    enabled = enabled && gpuAvailable_;
    cpu_->setChecked(!enabled);
    gpu_->setChecked(enabled);
    if (useGpu_ == enabled) return;
    useGpu_ = enabled;
    emit rendererChanged(enabled);
}

void DisplaySettings::setPerformanceOverlay(bool enabled) { metrics_->setChecked(enabled); }
bool DisplaySettings::performanceOverlay() const { return metrics_->isChecked(); }
void DisplaySettings::setPerformanceDetails(bool enabled) { details_->setChecked(enabled); }
bool DisplaySettings::performanceDetails() const { return details_->isChecked(); }
}  // namespace fugue::shell

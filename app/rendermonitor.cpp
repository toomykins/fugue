#include "app/rendermonitor.h"
#include "app/renderthread.h"
#include "shell/displaysettings.h"
#include "shell/skinwindow.h"

using fugue::app::TimingSummary;
namespace {
QString timing(const TimingSummary& value, bool details) {
    if (!value.samples) return "unavailable";
    auto text = QString("%1 / %2 ms").arg(value.average, 0, 'f', 2).arg(value.p95, 0, 'f', 2);
    if (details) text += QString(" (n=%1, last %2)").arg(qulonglong(value.samples)).arg(value.last, 0, 'f', 2);
    return text;
}
}  // namespace

RenderMonitor::RenderMonitor(RenderThread& renderer, fugue::shell::SkinWindow& window)
    : renderer_(renderer), window_(window) {
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &RenderMonitor::refresh);
    probe_.setInterval(fugue::app::EventLoopHealth::intervalMs);
    probe_.setTimerType(Qt::PreciseTimer);
    connect(&probe_, &QTimer::timeout, this, [this] {
        const double gap = double(lastProbe_.nsecsElapsed()) / 1e6;
        lastProbe_.restart();
        health_.sampleGap(gap);
    });
    connect(window_.displaySettings(), &fugue::shell::DisplaySettings::performanceOverlayChanged, this,
            &RenderMonitor::setEnabled);
    connect(window_.displaySettings(), &fugue::shell::DisplaySettings::performanceDetailsChanged, this,
            [this] { refresh(); });
    connect(&window_, &fugue::shell::SkinWindow::mapReady, this,
            [this](fugue::shell::MapWidget* map) { map->setPerformanceText(text_); });
    connect(&window_, &fugue::shell::SkinWindow::auxMapBuilt, this, [this](const QString& id) {
        if (auto* map = window_.auxMap(id); map && active()) {
            const auto key = id.toLower().toStdString();
            auto it = auxText_.find(key);
            map->setPerformanceText(it == auxText_.end() ? formatMap(key, {}, 0) : it->second);
        }
    });
    connect(&window_, &fugue::shell::SkinWindow::auxMapRemoved, this, [this](const QString& id) {
        const auto key = id.toLower().toStdString();
        auxText_.erase(key);
        previousFrames_.erase(key);
    });
    setEnabled(window_.displaySettings()->performanceOverlay());
}

void RenderMonitor::setEnabled(bool enabled) {
    if (enabled == active()) return;
    if (enabled) {
        previousFrames_.clear();
        for (const auto& [id, map] : renderer_.metrics().maps) previousFrames_[id] = map.frames;
        health_ = {};
        interval_.start();
        lastProbe_.start();
        timer_.start();
        probe_.start();
        refresh();
    } else {
        timer_.stop();
        probe_.stop();
        text_.clear();
        auxText_.clear();
        for (auto* map : window_.findChildren<fugue::shell::MapWidget*>()) map->setPerformanceText({});
    }
}

QString RenderMonitor::formatMap(const std::string& id, const fugue::app::MapMetrics& m, double rate) const {
    const bool details = window_.displaySettings()->performanceDetails();
    const QString actual = !m.frames ? "waiting for frame" : m.lastGpu ? "GPU" : "CPU";
    QString text = QString(
                       "Map: %1 | Requested: %2\nRenderer: %3\nScene updates/s: %4\n"
                       "Timings avg / p95 (last <=120 samples)\nScene prepare: %5\n")
                       .arg(id.empty() ? "main" : QString::fromStdString(id))
                       .arg(window_.displaySettings()->useGpu() ? "GPU" : "CPU")
                       .arg(actual)
                       .arg(rate, 0, 'f', 1)
                       .arg(timing(m.preparation, details));
    if (m.lastGpu) {
        text += "Render CPU (submit): " + timing(m.cpuSubmit, details);
        text += "\nGPU: " + (m.lastGpuMs >= 0 ? timing(m.gpu, details) : QString("unavailable"));
    } else {
        text += "Render CPU (worker): " + timing(m.cpuRender, details);
        text += "\nGPU: " + QString(m.frames ? "inactive (CPU rendering)" : "unavailable");
    }
    const auto& render = m.lastGpu ? m.cpuSubmit : m.cpuRender;
    if (render.samples) text += QString("\nLast scene render sample: %1 s ago").arg(render.ageMs / 1000, 0, 'f', 1);
    const auto lag = health_.lag.summary();
    text += lag.samples ? QString("\nGUI lag p95: %1 ms | stalls >=50 ms: %2")
                              .arg(lag.p95, 0, 'f', 1)
                              .arg(qulonglong(health_.stalls))
                        : QString("\nGUI lag: collecting (50 ms probe)");
    if (!m.fallbackReason.empty()) text += "\nCPU fallback: " + QString::fromStdString(m.fallbackReason);
    if (!details) return text;

    text += "\nCPU wall time; stages may block. No CPU+GPU total.";
    if (m.lastGpu) {
        static const char* phases[] = {"Preflight / acquire", "Scene recording", "View composition",
                                       "Submit / present"};
        for (size_t i = 0; i < m.phases.size(); ++i) {
            text += QString("\n  %1: %2").arg(phases[i], timing(m.phases[i], true));
            if (i == 1) {
                static const char* recording[] = {"Setup", "Plan / upload prep", "Uniforms / bindings",
                                                  "Command encoding", "Resource retirement"};
                for (size_t j = 0; j < m.recording.size(); ++j)
                    text += QString("\n    %1: %2").arg(recording[j], timing(m.recording[j], true));
            }
        }
        text += "\nRecording sub-stages are nested, not additional.";
        text += "\nGPU samples are delayed observations, not paired with CPU.";
        if (m.gpu.samples) text += QString("\nLast valid GPU observation: %1 s ago").arg(m.gpu.ageMs / 1000, 0, 'f', 1);
        if (!m.backend.empty()) text += "\nBackend: " + QString::fromStdString(m.backend);
    }
    text += "\nPreparation includes unchanged/dropped assemblies.";
    if (m.preparation.samples)
        text += QString("\nLast preparation sample: %1 s ago").arg(m.preparation.ageMs / 1000, 0, 'f', 1);
    text += QString("\nPrepared/rendered: %1 / %2\nGPU/CPU/fallback: %3 / %4 / %5\nDropped handoffs/unchanged: %6 / %7")
                .arg(m.prepared)
                .arg(m.frames)
                .arg(m.gpuFrames)
                .arg(m.cpuFrames)
                .arg(m.fallbacks)
                .arg(m.dropped)
                .arg(m.unchanged);
    text += "\nGUI probe: scheduling delay, not UI execution time.";
    if (lag.samples)
        text += QString("\nGUI lag avg / p95: %1 | max %2 ms").arg(timing(lag, true)).arg(lag.maximum, 0, 'f', 1);
    return text;
}

void RenderMonitor::refresh() {
    if (!active()) return;
    auto metrics = renderer_.metrics();
    metrics.maps.try_emplace("");  // show a waiting main-map panel before any work has arrived
    const double seconds = double(interval_.nsecsElapsed()) / 1e9;
    interval_.restart();
    std::map<std::string, uint64_t> next;
    auxText_.clear();
    for (const auto& [id, map] : metrics.maps) {
        const auto previous = previousFrames_.find(id);
        const uint64_t before = previous == previousFrames_.end() ? 0 : previous->second;
        const double rate = seconds > 0 && map.frames >= before ? double(map.frames - before) / seconds : 0;
        next[id] = map.frames;
        auto text = formatMap(id, map, rate);
        if (id.empty()) {
            text_ = text;
            if (window_.map()) window_.map()->setPerformanceText(text);
        } else {
            auxText_[id] = text;
            if (auto* widget = window_.auxMap(QString::fromStdString(id))) widget->setPerformanceText(text);
        }
    }
    previousFrames_ = std::move(next);
}

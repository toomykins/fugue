// Bounded, independently sampled timings. CPU wall times overlap GPU work; never add them into a frame total.
#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <QMetaType>

namespace fugue::app {
using MetricsClock = std::chrono::steady_clock;

struct TimingSummary {
    size_t samples = 0;
    double last = 0, average = 0, p95 = 0, maximum = 0;
    double ageMs = -1;  // time since the latest observation, NOT the age of a delayed GPU frame
};

class TimingWindow {
public:
    static constexpr size_t capacity = 120;
    void add(double ms, MetricsClock::time_point now = MetricsClock::now()) {
        if (!std::isfinite(ms) || ms < 0) return;  // unavailable is not a zero-time sample
        values_[next_] = ms;
        next_ = (next_ + 1) % capacity;
        count_ = std::min(count_ + 1, capacity);
        last_ = ms;
        lastAt_ = now;
    }
    TimingSummary summary(MetricsClock::time_point now = MetricsClock::now()) const {
        TimingSummary result;
        if (!count_) return result;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.begin() + count_);
        result.samples = count_;
        result.last = last_;
        result.average = std::accumulate(sorted.begin(), sorted.begin() + count_, 0.0) / count_;
        result.p95 = sorted[(95 * count_ + 99) / 100 - 1];  // nearest-rank percentile
        result.maximum = sorted[count_ - 1];
        result.ageMs = std::max(0.0, std::chrono::duration<double, std::milli>(now - lastAt_).count());
        return result;
    }

private:
    std::array<double, capacity> values_{};
    size_t count_ = 0, next_ = 0;
    double last_ = 0;
    MetricsClock::time_point lastAt_{};
};

struct RenderPhases {
    // Disjoint CPU wall-time phases: preflight/acquire, scene record, view composition, endFrame/present.
    std::array<double, 4> wallMs{-1, -1, -1, -1};
    // Nested INSIDE scene record: setup, plan/uploads, bindings, encode, retire. Detailed diagnostics only.
    std::array<double, 5> recordMs{-1, -1, -1, -1, -1};
};

struct MapCounters {
    uint64_t frames = 0, gpuFrames = 0, cpuFrames = 0, fallbacks = 0;
    uint64_t prepared = 0, dropped = 0, unchanged = 0;
    bool lastGpu = false;
    double lastGpuMs = -1;
    std::string backend, fallbackReason;
};
struct MapMetrics : MapCounters {
    TimingSummary preparation, cpuRender, cpuSubmit, gpu;
    std::array<TimingSummary, 4> phases;
    std::array<TimingSummary, 5> recording;
};
struct MapHistory : MapCounters {
    TimingWindow preparation, cpuRender, cpuSubmit, gpu;
    std::array<TimingWindow, 4> phases;
    std::array<TimingWindow, 5> recording;
    MapMetrics snapshot(MetricsClock::time_point now) const {
        MapMetrics result;
        static_cast<MapCounters&>(result) = *this;
        result.preparation = preparation.summary(now);
        result.cpuRender = cpuRender.summary(now);
        result.cpuSubmit = cpuSubmit.summary(now);
        result.gpu = gpu.summary(now);
        for (size_t i = 0; i < phases.size(); ++i) result.phases[i] = phases[i].summary(now);
        for (size_t i = 0; i < recording.size(); ++i) result.recording[i] = recording[i].summary(now);
        return result;
    }
};

// A GUI timer measures scheduling delay, not CPU utilization or time spent exclusively in UI code.
struct EventLoopHealth {
    static constexpr int intervalMs = 50;
    static constexpr double stallThresholdMs = 50;
    TimingWindow lag;
    uint64_t stalls = 0;
    void sampleGap(double elapsedMs) {
        if (!std::isfinite(elapsedMs) || elapsedMs < 0) return;
        const double excess = std::max(0.0, elapsedMs - intervalMs);
        lag.add(excess);
        if (excess >= stallThresholdMs) ++stalls;
    }
};
}  // namespace fugue::app

Q_DECLARE_METATYPE(fugue::app::RenderPhases)

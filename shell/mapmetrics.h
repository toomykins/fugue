// Small cross-thread accumulator. Timings use steady_clock, never the model's wall clock.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <vector>

namespace fugue::shell {
class MapMetrics {
public:
    static int64_t nowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    struct Timing { double average = 0, p95 = 0; bool available = false; };
    struct FrameTiming {
        int64_t started = 0, prepared = 0, received = 0, render = 0, recorded = 0;
    };
    struct Snapshot {
        double targetFps = 10, mapsPerSecond = 0, submittedPerSecond = 0;
        double missedPerSecond = 0, uploadMBPerSecond = 0;
        Timing compose, latency, queue, renderWait, submit, recording, qtSubmitWait;
        double overwrittenPerSecond = 0;
        unsigned pending = 0;
    };
    explicit MapMetrics(int64_t now = nowNs()) : since_(now) {}
    void reset(int64_t now = nowNs()) {
        std::lock_guard lock(mu_);
        clear(); since_ = now; target_ = 10; pending_ = 0; cached_ = {};
    }
    void setPeriod(int64_t ns) {
        std::lock_guard lock(mu_);
        target_ = ns > 0 ? 1e9 / double(ns) : 0;
    }
    void composed(double ms, uint64_t missed) {
        std::lock_guard lock(mu_);
        ++maps_; missed_ += missed; sample(compose_, ms);
    }
    void uploaded(uint64_t bytes) { std::lock_guard lock(mu_); bytes_ += bytes; }
    void published(bool overwrote) {
        std::lock_guard lock(mu_);
        if (overwrote) ++overwritten_;
        pending_ = 1;
    }
    void consumed() { std::lock_guard lock(mu_); pending_ = 0; }
    void submitted(const FrameTiming& frame, int64_t now = nowNs()) {
        std::lock_guard lock(mu_);
        ++submitted_;
        if (frame.started <= 0 || now < frame.started) return;
        sample(latency_, double(now - frame.started) / 1e6);
        if (frame.prepared >= frame.started && frame.received >= frame.prepared &&
            frame.render >= frame.received && now >= frame.render) {
            sample(queue_, double(frame.received - frame.prepared) / 1e6);
            sample(renderWait_, double(frame.render - frame.received) / 1e6);
            sample(submit_, double(now - frame.render) / 1e6);
            if (frame.recorded >= frame.render && now >= frame.recorded) {
                sample(recording_, double(frame.recorded - frame.render) / 1e6);
                sample(qtSubmitWait_, double(now - frame.recorded) / 1e6);
            }
        }
    }
    // Shared cached windows allow multiple map overlays to read without stealing samples.
    Snapshot snapshot(int64_t now = nowNs()) {
        std::lock_guard lock(mu_);
        if (now - since_ >= 1'000'000'000) {
            double seconds = double(now - since_) / 1e9;
            cached_ = {target_, maps_ / seconds, submitted_ / seconds, missed_ / seconds,
                       bytes_ / seconds / 1e6, timing(compose_), timing(latency_),
                       timing(queue_), timing(renderWait_), timing(submit_), timing(recording_), timing(qtSubmitWait_),
                       overwritten_ / seconds, pending_};
            clear(); since_ = now;
        }
        cached_.targetFps = target_;
        cached_.pending = pending_;
        return cached_;
    }
private:
    static void sample(std::vector<double>& samples, double value) {
        if (samples.size() < 2048) samples.push_back(value);
    }
    static Timing timing(std::vector<double>& samples) {
        if (samples.empty()) return {};
        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        std::sort(samples.begin(), samples.end());
        return {sum / samples.size(), samples[(samples.size() * 95 + 99) / 100 - 1], true};
    }
    void clear() {
        maps_ = submitted_ = missed_ = bytes_ = overwritten_ = 0;
        compose_.clear(); latency_.clear(); queue_.clear(); renderWait_.clear(); submit_.clear();
        recording_.clear(); qtSubmitWait_.clear();
    }
    std::mutex mu_;
    int64_t since_;
    double target_ = 10;
    uint64_t maps_ = 0, submitted_ = 0, missed_ = 0, bytes_ = 0, overwritten_ = 0;
    unsigned pending_ = 0;
    std::vector<double> compose_, latency_, queue_, renderWait_, submit_, recording_, qtSubmitWait_;
    Snapshot cached_;
};
}

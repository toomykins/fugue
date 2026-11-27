// Session-thread map deadlines, independent of the slower chat/stat publication tick.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include "model/world.h"

namespace fugue {
class MapFrameClock {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    explicit MapFrameClock(Time now) : next_(now) {}

    void setTiming(int64_t tickNs, int64_t frameNs, Time now) {
        // Same fallback as the model: client.fps, then world tick, then the default tick.
        auto period = std::chrono::nanoseconds(std::clamp<int64_t>(
            frameNs > 0 ? frameNs : (tickNs > 0 ? tickNs : model::kDefaultTickNanos),
            10'000'000, 1'000'000'000)); // bounds accepted by the Timing decoder
        if (period != period_) { period_ = period; next_ = now; }
    }
    bool due(Time now) const { return now >= next_; }
    // Called after composing. Preserve phase, but skip missed deadlines instead of
    // trying to catch up with a burst of expensive compositions.
    uint64_t completed(Time now) {
        if (now < next_) return 0;
        auto missed = (now - next_) / period_;
        next_ += period_ * (missed + 1);
        return uint64_t(missed);
    }
    int64_t periodNs() const { return period_.count(); }
    std::chrono::milliseconds wait(Time now, Time uiDeadline) const {
        const auto deadline = std::min(next_, uiDeadline);
        if (deadline <= now) return std::chrono::milliseconds(0);
        // Round up to avoid a sub-ms poll spin. Keep input/audio pumping at least every 30 ms.
        return std::min(std::chrono::ceil<std::chrono::milliseconds>(deadline - now), std::chrono::milliseconds(30));
    }
private:
    Time next_;
    std::chrono::nanoseconds period_{model::kDefaultTickNanos};
};
}

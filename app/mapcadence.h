// MapCadence: when the session thread next assembles the map. A map in motion, or one with frames ingested since its
// last assembly, is drawn at the motion cadence; a still map idles at a slower one. A frame ingested while idling
// brings the next assembly forward to one motion frame after the last, so the first frame of a glide that starts
// from rest is drawn as promptly as the rest of it.
#pragma once
#include <algorithm>
#include <chrono>

namespace fugue {

struct MapCadence {
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kMotion{16};  // ~60 fps
    static constexpr std::chrono::milliseconds kIdle{50};  // icon_state steps in deciseconds, so 20 fps is frame-exact

    Clock::time_point next{}, last{};
    bool idle = false;

    // An assembly ran at `now`. `busy`: the map is in motion, or frames arrived since the previous assembly.
    void assembled(Clock::time_point now, bool busy) {
        last = now;
        idle = !busy;
        next = now + (busy ? kMotion : kIdle);
    }
    // A frame was ingested. True when it brought the next assembly forward, i.e. the caller should stop waiting.
    bool ingested() {
        if (!idle) return false;
        idle = false;
        next = std::min(next, last + kMotion);
        return true;
    }
};

}  // namespace fugue

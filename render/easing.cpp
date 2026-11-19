// See easing.h.
#include "render/easing.h"

#include <cmath>

#include "model/gomath.h"

namespace fugue::render {

namespace {

int g_easingMutation = 0;

constexpr uint8_t kLinear = 0, kSine = 1, kCircular = 2, kCubic = 3, kBounce = 4, kElastic = 5, kBack = 6, kQuad = 7,
                  kJump = 8;
constexpr uint8_t kCurveMask = 0x3f, kEaseIn = 64, kEaseOut = 128;

double easeOutBounce(double t) {
    const double n1 = 7.5625, d1 = 2.75;
    if (t < 1 / d1) return n1 * t * t;
    if (t < 2 / d1) {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1) {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}
double easeInBounce(double t) { return 1 - easeOutBounce(1 - t); }
double easeInElastic(double t) {
    if (t == 0 || t == 1) return t;
    const double period = 0.3;
    double s = period / 4;
    t--;
    return -std::pow(2.0, 10 * t) * std::sin((t - s) * (2 * gomath::kPi) / period);
}

// curve: the "In" shape of each curve; defaultOut = documented to default to EASE_OUT.
bool curve(uint8_t id, double t, double& out, bool& defaultOut) {
    defaultOut = false;
    switch (id) {
        case kLinear: out = t; return true;
        case kSine: out = (g_easingMutation == 1) ? t : 1 - gomath::cos(t * gomath::kPi / 2); return true;
        case kCircular: out = 1 - std::sqrt(1 - t * t); return true;
        case kQuad: out = t * t; return true;
        case kCubic: out = t * t * t; return true;
        case kBack: {
            const double s = 1.70158;
            out = t * t * ((s + 1) * t - s);
            return true;
        }
        case kBounce:
            defaultOut = true;
            out = easeInBounce(t);
            return true;
        case kElastic:
            defaultOut = true;
            out = easeInElastic(t);
            return true;
    }
    return false;
}

double easeJump(double t, bool in, bool out) {
    (void)t;
    if (in && out) return t < 0.5 ? 0 : 1;
    if (in) return 1;
    return 0;
}

}  // namespace

void setEasingMutation(int m) { g_easingMutation = m; }

double ease(double t, uint8_t easing) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    uint8_t id = easing & kCurveMask;
    bool in = (easing & kEaseIn) != 0, out = (easing & kEaseOut) != 0;
    if (id == kJump) return easeJump(t, in, out);
    auto fn = [&](double x) {
        double v;
        bool d;
        curve(id, x, v, d);
        return v;
    };
    double probe, dummy;
    bool defaultOut;
    if (!curve(id, 0.5, probe, defaultOut)) return t;  // an unrecognised curve id behaves as LINEAR
    (void)dummy;
    if (in && out) {
        if (t < 0.5) return fn(t * 2) * 0.5;
        return 0.5 + (1 - fn((1 - t) * 2)) * 0.5;
    }
    if (in) return fn(t);
    if (out) return 1 - fn(1 - t);
    if (defaultOut) return 1 - fn(1 - t);
    if (t < 0.5) return fn(t * 2) / 2;
    return 1 - fn((1 - t) * 2) / 2;
}

}  // namespace fugue::render

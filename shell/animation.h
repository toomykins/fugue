// Read-only animation-chain composition at a caller-supplied time.
// Does not mutate the model or own asset playback clocks.
#pragma once
#include "model/world.h"
#include "model/gomath.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <optional>
#include <type_traits>

namespace fugue::shell::animation {
// BYOND matrices [a,b,c,d,e,f]: x' = a*x + b*y + c, y' = d*x + e*y + f.
using Matrix = std::array<double, 6>;
// l applied after r.
inline Matrix multiply(const Matrix& l, const Matrix& r) {
    return {l[0]*r[0] + l[1]*r[3], l[0]*r[1] + l[1]*r[4], l[0]*r[2] + l[1]*r[5] + l[2],
            l[3]*r[0] + l[4]*r[3], l[3]*r[1] + l[4]*r[4], l[3]*r[2] + l[4]*r[5] + l[5]};
}
inline std::optional<Matrix> inverse(const Matrix& m) {
    const double det = m[0]*m[4] - m[1]*m[3];
    if (!std::isfinite(det) || std::abs(det) < 1e-12) return {};
    const double a = m[4]/det, b = -m[1]/det, d = -m[3]/det, e = m[0]/det;
    return Matrix{a, b, -(a*m[2] + b*m[5]), d, e, -(d*m[2] + e*m[5])};
}
// Transform interpolation, as stock DreamSeeker 516.1688 does it (tests/data/transform-tween-world). The linear
// part is decomposed as rotation * [[sx, k], [0, sy]] (the angle from the first column), and the angle (the shorter
// way), scales, shear and translation are interpolated separately, so Turn() tweens rotate rather than shrink through
// the midpoint; a change of handedness rides on sy. An exact half turn has no shorter way: the row angle atan2(-b, a)
// picks the direction, and when that is a half turn too (a 180 degree Turn, a mirror) the six components are
// interpolated directly, as they always are with ANIMATION_LINEAR_TRANSFORM. Endpoints are returned exactly.
inline Matrix interpolate(const Matrix& from, const Matrix& to, double f, bool linear) {
    // Exact endpoints; BACK/ELASTIC easing extrapolates past them.
    if (f == 0) return from;
    if (f == 1) return to;
    Matrix out;
    auto mix = [f](double a, double b) { return a + (b - a) * f; };
    for (int i = 0; i < 6; ++i) out[i] = mix(from[i], to[i]);
    if (linear) return out;
    struct Parts { double angle, sx, sy, k; };
    auto decompose = [](const Matrix& m) -> std::optional<Parts> {
        const double sx = std::hypot(m[0], m[3]);
        if (!std::isfinite(sx) || sx < 1e-12) return {};
        return Parts{std::atan2(m[3], m[0]), sx, (m[0]*m[4] - m[1]*m[3]) / sx, (m[0]*m[1] + m[3]*m[4]) / sx};
    };
    const auto a = decompose(from), b = decompose(to);
    if (!a || !b) return out;
    constexpr double pi = 3.14159265358979323846, tie = 1e-5;
    auto wrap = [](double turn) {
        while (turn > pi) turn -= 2 * pi;
        while (turn <= -pi) turn += 2 * pi;
        return turn;
    };
    double turn = wrap(b->angle - a->angle);
    if (std::abs(turn) > pi - tie) {
        const double rows = wrap(std::atan2(-to[1], to[0]) - std::atan2(-from[1], from[0]));
        if (std::abs(rows) > pi - tie) return out;
        turn = rows < 0 ? -pi : pi;
    }
    const double angle = a->angle + turn * f, sx = mix(a->sx, b->sx), sy = mix(a->sy, b->sy), k = mix(a->k, b->k);
    const double c = std::cos(angle), s = std::sin(angle);
    out[0] = sx * c; out[3] = sx * s;
    out[1] = c * k - s * sy; out[4] = s * k + c * sy;
    return out;
}
// DM easing: curve in the low bits (LINEAR 0, SINE 1, CIRCULAR 2, CUBIC 3,
// BOUNCE 4, ELASTIC 5, BACK 6, QUAD 7, JUMP 8), EASE_IN 64 / EASE_OUT 128.
// Stock DreamSeeker 516.1688 (tests/data/easing-world) uses the Penner ease-in
// shapes below; without a direction BOUNCE and ELASTIC ease out and the rest
// ease in and out. In-out BACK is asymmetric: its first half uses Penner's
// in-out overshoot (1.70158 * 1.525), its second half the plain 1.70158.
// JUMP jumps at the start by default and with EASE_OUT, at the end with
// EASE_IN, and halfway with both. BACK/ELASTIC overshoot outside [0,1].
inline bool supportedEasing(uint8_t easing) { return (easing & 63) <= 8; }
inline double ease(double t, uint8_t easing) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    const int type = easing & 63;
    if (!type) return t;
    int mode = easing & 192;
    if (type == 8) {
        if (mode == 64) return 0;
        if (mode == 192) return t < .5 ? 0 : 1;
        return 1;
    }
    if (!mode) mode = type == 4 || type == 5 ? 128 : 192;
    auto bounceOut = [](double x) {
        const double n = 7.5625, d = 2.75;
        if (x < 1 / d) return n * x * x;
        if (x < 2 / d) { x -= 1.5 / d; return n * x * x + .75; }
        if (x < 2.5 / d) { x -= 2.25 / d; return n * x * x + .9375; }
        x -= 2.625 / d; return n * x * x + .984375;
    };
    auto curve = [&](double x) -> double { // ease-in shape on [0,1]
        switch (type) {
        case 1: return 1 - gomath::cos(x * gomath::kPi / 2);
        case 2: return 1 - std::sqrt(std::max(0.0, 1 - x * x));
        case 3: return x * x * x;
        case 4: return 1 - bounceOut(1 - x);
        case 5: return x <= 0 ? 0 : x >= 1 ? 1 :
                -std::pow(2.0, 10 * x - 10) * std::sin((10 * x - 10.75) * (2 * gomath::kPi / 3));
        case 6: { const double c1 = 1.70158; return (c1 + 1) * x * x * x - c1 * x * x; }
        case 7: return x * x;
        }
        return x;
    };
    if (mode == 64) return curve(t);
    if (mode == 128) return 1 - curve(1 - t);
    if (type == 6 && t < .5) { const double c2 = 1.70158 * 1.525, x = t * 2; return ((c2 + 1) * x - c2) * x * x / 2; }
    if (t < .5) return curve(t * 2) / 2;
    return 1 - curve((1 - t) * 2) / 2;
}
// ANIMATION_SLICE (stock 516.1688, tests/data/anim-flags-world): the server resends the chain behind a leading slice
// step (from = to = 0xffff, flag 0x8) whose time is the slice's length, whose loop is how often it plays and whose
// trailing f32 is where the slice starts in the sequence behind it. That sequence then plays the window over and over
// from the slice step's start, and holds at the window's end once the loops run out. CONTINUE and END_LOOP never reach
// the client as flags: the first arrives as an ordinary chain with a negative start, the second as an empty chain.
inline bool sliceStep(const model::Atom::Chain& ch) {
    if (ch.steps.size() < 2) return false;
    const auto& s = ch.steps.front();
    const double length = std::bit_cast<float>(s.timeBits), start = std::bit_cast<float>(s.extBits);
    return (s.flags & 0x8u) && s.from == 0xffffu && s.to == 0xffffu && std::isfinite(start) && start >= 0 &&
        start <= 1'000'000 && std::isfinite(length) && length > 0 && (ch.steps[1].flags & 0x8u) == 0;
}
// The sequence behind a slice, and the time to evaluate it at.
inline std::pair<model::Atom::Chain, int64_t> sliced(const model::Atom::Chain& ch, int64_t now) {
    const auto& s = ch.steps.front();
    model::Atom::Chain rest{ch.steps[1].startNs, ch.tickNs, {ch.steps.begin() + 1, ch.steps.end()}, ch.rejected};
    const auto length = std::max<int64_t>(1, int64_t(double(std::bit_cast<float>(s.timeBits)) * double(ch.tickNs)));
    const auto start = int64_t(double(std::bit_cast<float>(s.extBits)) * double(ch.tickNs));
    const int64_t t = now - s.startNs;
    int64_t at = start;
    if (t > 0) at = s.loop > 0 && t / length >= s.loop ? start + length : start + t % length;
    const int64_t origin = rest.startNs;
    return {std::move(rest), origin + at};
}
// Refuse unsafe timing, unknown flag semantics and unverified easing. Charge a
// conservative cubic sequence-work bound before running the historical pruning.
inline bool admit(const model::Atom::Chain& ch, uint64_t& work) {
    const uint64_t n = ch.steps.size();
    if (ch.rejected || n > 128 || n*n*n > work || ch.tickNs <= 0 || ch.tickNs > 10'000'000'000LL) return false;
    work -= n*n*n;
    for (const auto& s : ch.steps) {
        const double ticks = std::bit_cast<float>(s.timeBits);
        if (!std::isfinite(ticks) || ticks < 0 || ticks > 1'000'000 ||
            s.startNs < 0 || s.startNs > INT64_MAX / 2 || s.loop > 1'000'000 ||
            !supportedEasing(s.easing) || (s.flags & ~0x40000087u & ~(&s == &ch.steps.front() ? 0x8u : 0u))) return false;
            // 0x2: ANIMATION_LINEAR_TRANSFORM; 0x8: ANIMATION_SLICE, only as the leading slice step (sliceStep).
            // ANIMATION_RELATIVE never reaches the wire: the server sends the resolved
            // absolute appearance (flags 0), so it needs no client handling.
    }
    if (n && (ch.steps.front().flags & 0x8u) && !sliceStep(ch)) return false;
    // Bound the total before model::animSequences multiplies by the loop count.
    long double duration = 0;
    for (const auto& s : ch.steps) duration += double(std::bit_cast<float>(s.timeBits)) * double(ch.tickNs);
    for (const auto& s : ch.steps)
        if (duration * std::max(1, s.loop) > INT64_MAX / 4) return false;
    return true;
}
// The stock chain evaluator, for any target, over whatever value V it composes. The chain is pruned of finished
// sequences, then the sequences are composed in order into `out`: the first is eased absolutely From -> To; a parallel
// one adds its own To - From; a head eases from what the sequences before it give (snapping to its From when that
// differs from the previous sequence's end). A sequence stops advancing when a later head starts.
// get(appid) is an appearance's value; lerp(from, to, raw, f) one step at raw fraction `raw`, eased `f`; add(out, base,
// from, to, raw, f) adds lerp(from, to) - base to `out`. Either may take the step's flags as a last argument. False when nothing is left; `lastTo` is the last sequence's end.
template <class V, class Get, class Lerp, class Add>
bool composeChain(const model::Atom::Chain& ch, int64_t now, Get&& get, Lerp&& lerp, Add&& add, V& out, uint32_t& lastTo) {
    if (sliceStep(ch)) {
        const auto [rest, at] = sliced(ch, now);
        return composeChain(rest, at, get, lerp, add, out, lastTo);
    }
    auto stepNs = [&](size_t i) { return int64_t(double(std::bit_cast<float>(ch.steps[i].timeBits)) * double(ch.tickNs)); };
    auto doLerp = [&](const V& from, const V& to, double raw, double f, uint32_t flags) -> V {
        if constexpr (std::is_invocable_v<Lerp&, const V&, const V&, double, double, uint32_t>)
            return lerp(from, to, raw, f, flags);
        else return lerp(from, to, raw, f);
    };
    auto doAdd = [&](V& o, const V& base, const V& from, const V& to, double raw, double f, uint32_t flags) {
        if constexpr (std::is_invocable_v<Add&, V&, const V&, const V&, const V&, double, double, uint32_t>)
            add(o, base, from, to, raw, f, flags);
        else add(o, base, from, to, raw, f);
    };
    const int64_t eps = ch.tickNs / 1000;   // the evaluator's tolerance of 0.001 time units
    std::vector<model::AnimSeq> seqs = model::animSequences(ch);
    std::vector<bool> alive(seqs.size(), true);
    auto nextAlive = [&](size_t i) { for (size_t j = i + 1; j < seqs.size(); j++) if (alive[j]) return j; return seqs.size(); };
    auto nextHead = [&](size_t i) { for (size_t j = i + 1; j < seqs.size(); j++) if (alive[j] && seqs[j].head) return j; return seqs.size(); };
    // A sequence's opening From; a head that starts with the sequence before it takes that one's.
    std::vector<uint32_t> headFrom(seqs.size());
    for (size_t i = 0; i < seqs.size(); i++) headFrom[i] = ch.steps[seqs[i].first].from;
    // The first later head that has started decides; without one, the sequence's own end does. A looping sequence a head
    // has started over plays out the pass it is in. Otherwise it is done once the head has run its first step without a
    // head after it starting first.
    auto finished = [&](size_t i) {
        model::AnimSeq& q = seqs[i];
        for (size_t j = nextHead(i); j < seqs.size(); j = nextHead(j)) {
            const model::AnimSeq& h = seqs[j];
            if (h.startNs > now) continue;
            if (q.endNs >= 0 && q.endNs <= h.startNs) return true;
            if (h.startNs - eps <= q.startNs) { headFrom[j] = headFrom[i]; return true; }
            if (q.endNs < 0 || q.endNs != q.startNs + q.durNs) {
                if (q.durNs > 0) q.startNs = h.startNs - (h.startNs - q.startNs) % q.durNs;
                else q.startNs = h.startNs;
                q.endNs = q.startNs + q.durNs;
            }
            if (j == nextAlive(i) && q.first == q.last && headFrom[i] == ch.steps[q.first].to) return true;
            size_t after = nextHead(j);
            return (after < seqs.size() ? seqs[after].startNs : now) >= h.startNs + stepNs(h.first);
        }
        return q.endNs >= 0 && now >= q.endNs;
    };
    // A finished sequence goes when nothing or a head follows it; one followed by a parallel stays, held
    // at its end, until that one goes.
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t i = 0; i < seqs.size(); i++) {
            if (!alive[i] || !finished(i)) continue;
            size_t n = nextAlive(i);
            if (n < seqs.size() && seqs[n].parallel) continue;
            alive[i] = false;
            changed = true;
        }
    }
    bool first = true;
    uint32_t prevEnd = 0;
    for (size_t i = 0; i < seqs.size(); i++) {
        if (!alive[i]) continue;
        const model::AnimSeq& q = seqs[i];
        if (!first && prevEnd != headFrom[i] && (q.head || q.parallel)) {
            V a = get(prevEnd), b = get(headFrom[i]);
            if (q.parallel) doAdd(out, a, a, b, 1.0, 1.0, 0);
            else out = doLerp(a, b, 1.0, 1.0, 0);
        }
        // The step in flight and its fraction, the sequence held where the next head started.
        int64_t t = now;
        size_t nh = nextHead(i);
        if (nh < seqs.size() && seqs[nh].startNs < t) t = seqs[nh].startNs;
        size_t cur = q.first;
        double f = 0;
        if (q.startNs <= now) {
            // Past its end with no later head the sequence rests on its last To; otherwise the clock wraps by its length.
            if (q.durNs <= 0 || (nh == seqs.size() && q.endNs >= 0 && t >= q.endNs)) { cur = q.last; f = 1; }
            else {
                int64_t e = (t - q.startNs) % q.durNs;
                while (cur < q.last && stepNs(cur) <= e) { e -= stepNs(cur); cur++; }
                int64_t d = stepNs(cur);
                f = d > 0 ? std::min(1.0, double(e) / double(d)) : 1.0;
            }
        }
        const model::Atom::AnimStep& st = ch.steps[cur];
        const double raw = f;
        if (st.easing) f = ease(f, st.easing);
        // The head step eases from the sequence's last To once the whole sequence has played out by the next head
        // (or now), else from its own From (the first sequence, a parallel) or from `out` (a later head).
        const int64_t tref = nh < seqs.size() ? seqs[nh].startNs : now;
        const bool done = cur == q.first && q.startNs + q.durNs < tref;
        if (q.parallel && !first) {
            V base = get(headFrom[i]);
            V from = done ? get(ch.steps[q.last].to) : cur == q.first ? base : get(st.from);
            doAdd(out, base, from, get(st.to), raw, f, st.flags);
        } else {
            V from = done ? get(ch.steps[q.last].to) : cur != q.first ? get(st.from) : first ? get(headFrom[i]) : out;
            out = doLerp(from, get(st.to), raw, f, st.flags);
        }
        first = false;
        prevEnd = ch.steps[q.last].to;
    }
    lastTo = prevEnd;
    return !first;
}

} // namespace fugue::shell::animation

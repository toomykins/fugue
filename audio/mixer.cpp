#include "audio/mixer.h"
#include <algorithm>
#include <cmath>

namespace fugue::audio {

// Flat inside falloff, falloff/dist outside; never zero (FMOD's inverse rolloff has no cutoff). falloff <= 0 = no model.
double attenuate(double dist, double falloff) {
    if (falloff <= 0 || dist <= falloff) return 1;
    return falloff / dist;
}

// The horizontal-plane direction: y is ignored so height does not shrink the stereo image.
double azimuth(double x, double, double z) {
    double h = std::hypot(x, z);
    if (h == 0) return 0;
    return std::clamp(x / h, -1.0, 1.0);
}

double freqStep(const Clip* c, double freq, int outRate) {
    if (!c || outRate <= 0) return 0;
    double native = c->rate > 0     ? c->rate
                    : c->streamed() ? outRate
                                    : 0;  // a module renders at whatever rate it is asked
    if (native <= 0) return 0;
    double eff = native;
    if (freq == 0 || freq == 1) {
    } else if (std::fabs(freq) <= 100) {
        eff = native * freq;
    } else {
        eff = freq;  // absolute rate in Hz
    }
    return eff / outRate;
}

namespace {
void gains(double vol, double pan, double& l, double& r) {
    vol = std::clamp(vol, 0.0, 1.0);
    pan = std::clamp(pan, -1.0, 1.0);
    l = r = 1;
    if (pan > 0)
        l = 1 - pan;
    else if (pan < 0)
        r = 1 + pan;
    l *= vol;
    r *= vol;
}
}  // namespace

void spatialGains(const Cmd& c, double& l, double& r) {
    if (!c.spatial) {
        gains(c.volume, c.pan, l, r);
        return;
    }
    double d = std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z);
    gains(c.volume * attenuate(d, c.falloff), std::clamp(c.pan + azimuth(c.x, c.y, c.z), -1.0, 1.0), l, r);
}

void Mixer::start(Voice& v, const Cmd& c) {
    v.cmd = c;
    v.seq = ++seq_;
    v.step = freqStep(c.clip.get(), c.frequency, rate_);
    v.pos = v.step < 0 && !c.clip->streamed() ? double(c.clip->frames()) - 1 : 0;
    v.back = v.done = false;
    v.stream.reset();
    if (c.clip->streamed()) v.stream = c.clip->open(c.repeat != kRepeatNone);  // null if it will not load: done at once
    v.paused = c.status & kStatusPaused;
    v.muted = c.status & kStatusMute;
    spatialGains(c, v.gainL, v.gainR);
    st_.started++;
}

void Mixer::apply(const Cmd& c) {
    std::lock_guard<std::mutex> g(mu_);
    // STOP: a null sound on a named channel silences it; on channel <= 0, all.
    if (!c.clip) {
        if (c.channel <= 0) {
            st_.stopped += int(chans_.size());
            chans_.clear();
        } else if (chans_.erase(c.channel))
            st_.stopped++;
        return;
    }
    // SOUND_UPDATE retunes a playing sound in place; with nothing playing on the channel it does nothing.
    if ((c.status & kStatusUpdate) && c.channel > 0) {
        auto it = chans_.find(c.channel);
        if (it == chans_.end()) return;
        Voice& v = it->second;
        v.cmd.volume = c.volume;
        v.cmd.pan = c.pan;
        v.cmd.x = c.x;
        v.cmd.y = c.y;
        v.cmd.z = c.z;
        v.cmd.falloff = c.falloff;
        v.cmd.spatial = c.spatial;
        v.paused = c.status & kStatusPaused;
        v.muted = c.status & kStatusMute;
        if (c.frequency != 0) v.step = freqStep(v.cmd.clip.get(), c.frequency, rate_);
        spatialGains(v.cmd, v.gainL, v.gainR);
        return;
    }
    int ch = c.channel <= 0 ? allocAuto()
                            : c.channel;  // > kMaxChannel is kept as-is: an invented id breaks the matching stop
    if (auto it = chans_.find(ch); it != chans_.end()) {
        if (c.wait) {
            it->second.queue.push_back(c);
            return;
        }
        if (it->second.cmd.priority > c.priority) {
            st_.dropped++;
            return;
        }
        st_.stolen++;
        chans_.erase(it);
    }
    if (chans_.size() >= size_t(kMaxVoices) && !evictFor(c)) {
        st_.dropped++;
        return;
    }
    start(chans_[ch], c);
}

int Mixer::allocAuto() {
    for (int i = 0; i < kMaxVoices * 4; i++) {
        if (++nextA_ > kMaxChannel + kMaxVoices * 4) nextA_ = kMaxChannel + 1;
        if (!chans_.count(nextA_)) return nextA_;
    }
    return nextA_;
}

bool Mixer::evictFor(const Cmd& c) {
    int worstCh = 0, worstPri = 1 << 30;
    uint64_t worstSeq = 0;
    for (const auto& [ch, v] : chans_)
        if (v.cmd.priority < worstPri || (v.cmd.priority == worstPri && v.seq < worstSeq)) {
            worstCh = ch;
            worstPri = v.cmd.priority;
            worstSeq = v.seq;
        }
    if (worstCh == 0 || worstPri > c.priority) return false;
    chans_.erase(worstCh);
    st_.stolen++;
    return true;
}

void Mixer::stopAll() {
    std::lock_guard<std::mutex> g(mu_);
    st_.stopped += int(chans_.size());
    chans_.clear();
}

size_t Mixer::active() const {
    std::lock_guard<std::mutex> g(mu_);
    return chans_.size();
}
Mixer::Stats Mixer::stats() const {
    std::lock_guard<std::mutex> g(mu_);
    return st_;
}
std::vector<int> Mixer::channels() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<int> o;
    for (const auto& kv : chans_) o.push_back(kv.first);
    return o;
}

void Mixer::render(float* out, size_t frames) {
    std::fill(out, out + frames * 2, 0.f);
    if (!frames) return;
    std::lock_guard<std::mutex> g(mu_);
    for (auto it = chans_.begin(); it != chans_.end();) {
        Voice& v = it->second;
        if (!v.paused) renderVoice(v, out, frames);
        if (v.done) {
            if (!v.queue.empty()) {  // sound.wait: the queued sound takes the channel over as this one ends
                std::vector<Cmd> rest(v.queue.begin() + 1, v.queue.end());
                Cmd next = v.queue.front();
                start(v, next);
                v.queue = std::move(rest);
                ++it;
                continue;
            }
            it = chans_.erase(it);
            continue;
        }
        ++it;
    }
    for (size_t i = 0; i < frames * 2; i++) out[i] = std::clamp(out[i], -1.f, 1.f);
}

// A module voice: rendered at rate_/|step| and taken frame for frame, so sound.frequency moves pitch and tempo together
// as it does for a sample. libopenmpt accepts 8-192 kHz; a step outside that is clamped (at 44.1 kHz: x0.23..x5.5).
void Mixer::renderStream(Voice& v, float* out, size_t frames) {
    double st = std::fabs(v.step);
    if (!v.stream || st == 0) {
        v.done = true;
        return;
    }
    int sr = int(std::clamp(std::lround(rate_ / st), 8000L, 192000L));
    v.sbuf.resize(frames * 2);
    size_t got = v.stream->read(sr, frames, v.sbuf.data());
    double gl = v.muted ? 0 : v.gainL, gr = v.muted ? 0 : v.gainR;
    for (size_t i = 0; i < got; i++) {
        out[i * 2] += float(v.sbuf[i * 2] * gl);
        out[i * 2 + 1] += float(v.sbuf[i * 2 + 1] * gr);
    }
    if (got < frames) v.done = true;
}

void Mixer::renderVoice(Voice& v, float* out, size_t frames) {
    if (v.cmd.clip->streamed()) {
        renderStream(v, out, frames);
        return;
    }
    const Clip& c = *v.cmd.clip;
    const size_t n = c.frames();
    if (n == 0 || v.step == 0) {
        v.done = true;
        return;
    }
    double gl = v.muted ? 0 : v.gainL, gr = v.muted ? 0 : v.gainR;
    const double dn = double(n);
    for (size_t i = 0; i < frames; i++) {
        double step = v.back ? -v.step : v.step;
        if (v.pos < 0 || v.pos >= dn) {
            if (v.cmd.repeat == kRepeatLoop) {
                v.pos += v.pos >= dn ? -dn : dn;
            } else if (v.cmd.repeat == kRepeatPingPong) {
                v.back = !v.back;
                v.pos = v.pos < 0 ? -v.pos : 2 * (dn - 1) - v.pos;
                if (v.pos < 0 || v.pos >= dn) {
                    v.done = true;
                    return;
                }
                step = v.back ? -v.step : v.step;
            } else {
                v.done = true;
                return;
            }
        }
        // Linear interpolation.
        size_t a = std::min(size_t(std::max(0.0, v.pos)), n - 1), b = std::min(a + 1, n - 1);
        double f = v.pos - double(a);
        double l, r;
        if (c.channels == 1) {
            l = r = c.samples[a] + (c.samples[b] - c.samples[a]) * f;
        } else {
            size_t ch = size_t(c.channels);
            l = c.samples[a * ch] + (c.samples[b * ch] - c.samples[a * ch]) * f;
            r = c.samples[a * ch + 1] + (c.samples[b * ch + 1] - c.samples[a * ch + 1]) * f;
        }
        out[i * 2] += float(l * gl);
        out[i * 2 + 1] += float(r * gr);
        v.pos += step;
    }
}

}  // namespace fugue::audio

// Mixer: DM's `sound` datum semantics as a channel mixer, rendering interleaved stereo float.
//
//   - Channels 1..1024 are explicit. <= 0 means "any available", served from a pool above 1024 so that an explicit
//     command can never steal or stop it. repeat: 0 none, 1 loop, 2 ping-pong. wait queues behind the channel.
//   - frequency 0/1 is native; |f| <= 100 is a multiple (negative plays backwards); otherwise an absolute rate in Hz.
//   - priority: a higher value may steal from a lower one. status bits: MUTE 1, PAUSED 2, STREAM 4, UPDATE 16.
//   - Wire volume is round(DM*2.55), so /255 recovers it exactly. Wire pan is 128 + trunc(DM*1.27), so (w-128)/127
//     recovers +-1. DM pan 0 and an all-zero x/y/z emit no block, which means centre and flat.
//   - 3D: +X right, +Y up, +Z forward, in tiles, relative to the hearer. Gain is flat inside `falloff` and
//     attenuates outside it. Azimuth x/hypot(x,z) is added to pan. Balance is linear.
//   - Tracker modules play as a Stream (clip.h), not samples. Repeat 1 and 2 both loop by the song's order list, so
//     ping-pong and negative frequency play forwards.
//   - Not implemented: sound.atom tracking, environment/echo, the STREAM status bit, offset/pitch, on-end callbacks.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "audio/clip.h"

namespace fugue::audio {

constexpr uint8_t kStatusMute = 1, kStatusPaused = 2, kStatusStream = 4, kStatusUpdate = 16;
constexpr int kRepeatNone = 0, kRepeatLoop = 1, kRepeatPingPong = 2;
constexpr int kMaxChannel = 1024;
constexpr int kMaxVoices = 64;

// One sound command in DM units (already converted out of wire units). A null clip is a STOP; on channel <= 0, of all.
struct Cmd {
    int channel = 0;
    std::shared_ptr<const Clip> clip;
    double volume = 1;     // 0..1
    double pan = 0;        // -1..1
    double frequency = 0;  // DM units, see freqStep
    int repeat = 0;
    bool wait = false;
    int priority = 0;
    uint8_t status = 0;
    double x = 0, y = 0, z = 0, falloff = 0;
    bool spatial = false;  // the wire carried a 3D block
    std::string name;      // reporting only
};

double attenuate(double dist, double falloff);
double azimuth(double x, double y, double z);
double freqStep(const Clip* c, double freq, int outRate);
// Per-side linear gains for a command (volume, pan, and the 3D block when spatial).
void spatialGains(const Cmd& c, double& l, double& r);

class Mixer {
public:
    explicit Mixer(int rate = 44100) : rate_(rate > 0 ? rate : 44100) {}
    int rate() const { return rate_; }
    void apply(const Cmd& c);                // thread-safe
    void render(float* out, size_t frames);  // thread-safe; overwrites `out` (2*frames floats), clipped to [-1,1]
    void stopAll();
    size_t active() const;
    std::vector<int> channels() const;  // live channel ids, sorted
    struct Stats {
        int started = 0, stopped = 0, stolen = 0, dropped = 0;
    };
    Stats stats() const;

private:
    struct Voice {
        Cmd cmd;
        double pos = 0, step = 0, gainL = 0, gainR = 0;
        bool back = false, paused = false, muted = false, done = false;
        std::vector<Cmd> queue;
        uint64_t seq = 0;
        std::shared_ptr<Stream> stream;  // a module's own sequencer (clip->streamed()); null for sampled clips
        std::vector<float> sbuf;
    };
    void start(Voice& v, const Cmd& c);
    int allocAuto();
    bool evictFor(const Cmd& c);
    void renderVoice(Voice& v, float* out, size_t frames);
    void renderStream(Voice& v, float* out, size_t frames);
    int rate_;
    mutable std::mutex mu_;
    std::map<int, Voice> chans_;
    int nextA_ = kMaxChannel + 1;
    uint64_t seq_ = 0;
    Stats st_;
};

}  // namespace fugue::audio

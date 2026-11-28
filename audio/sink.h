// Sinks: where mixed stereo float goes.
//
// On macOS the device path uses Core Audio's Audio Queue API (no external player required).
// On Linux it pipes raw s16le stereo into a player already on the desktop (pw-play, paplay, aplay, in that order).
// No audio library is needed at build time, and this works on PipeWire, PulseAudio and bare ALSA.
//   - A player on $PATH may still be unable to reach a server (pw-play with no PipeWire daemon exits at once, and the
//     first write hits a closed pipe). Each candidate gets a 150 ms grace window to stay alive, and auto walks all
//     candidates rather than stopping at the first. A new player is primed with ~6 ms of silence so one that fails
//     only on its first input fails inside the window too.
//   - Each candidate pins a ~80 ms target latency. With the player's default, paplay buffers seconds.
//   - A write failure must not latch silence for the session: the player re-opens (Player::outputLoop).
// The spec grammar is the -audio flag / $FUGUE_AUDIO: "auto" (default), "none", "wav:<path>", "cmd:a,b,c".
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace fugue::audio {

class Sink {
public:
    virtual ~Sink() = default;
    // Consumes `frames` interleaved stereo frames. May block (that is the pacing of a pipe sink). false = broken.
    virtual bool write(const float* p, size_t frames, std::string* err) = 0;
    virtual void close() {}
    virtual std::string name() const = 0;
    // True when write() returns at once (no device behind it): the caller paces by the wall clock instead.
    virtual bool instant() const { return false; }
};

class NullSink : public Sink {
public:
    bool write(const float*, size_t, std::string*) override { return true; }
    std::string name() const override { return "null (silent)"; }
    bool instant() const override { return true; }
};

// Records the mix to a 16-bit stereo WAV, header written on close (it carries the length).
std::unique_ptr<Sink> openWavSink(const std::string& path, int rate, std::string* err);
// argv[0] spawned with raw s16le stereo on its stdin; nullptr (with err holding the player's own stderr) when it is
// missing or dies inside the start grace window.
std::unique_ptr<Sink> openExecSink(const std::vector<std::string>& argv, std::string* err);
#ifdef __APPLE__
std::unique_ptr<Sink> openCoreAudioSink(int rate, std::string* err);
#endif
// The Linux candidate list at `rate`, best first.
std::vector<std::vector<std::string>> execPlayers(int rate);
// Resolve a spec ("" = $FUGUE_AUDIO, then "auto"). Never nullptr: an unusable device is a NullSink plus `err` saying why.
// `skipped` (optional) gets the candidates "auto" tried and dropped before the one that opened, each with its reason
// ("" when the first one worked).
std::unique_ptr<Sink> openSink(const std::string& spec, int rate, std::string* err, std::string* skipped = nullptr);

}  // namespace fugue::audio

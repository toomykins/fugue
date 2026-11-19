// The seam from the wire to the speaker: s2c 0x006d -> resource -> decoded clip -> mixer -> sink.
//
//   0x006d ResID -> ordinal into the concatenated 0x000e availability list -> content id -> the fetched file bytes
//
// 0x006d is decoded in proto/dec_s2c_006d_sound.cpp; this file parses it again into typed fields. Resolution:
// - A play can arrive before its file, because the resource channel is one serial queue. A play whose bytes are missing
//   is held (see kPendingTtlMs) and its resource moves to the front of the declaration queue.
// - The ordinal is re-resolved on every retry, since the 0x000e list can still be growing.
// - The kind byte is not used to filter plays.
// - The decoded-clip cache is bounded at 64 MB.
//
// Threads: SoundFeed lives on the session thread (it needs the model's resource list and the pump's store). Player owns
// a decoder thread, so a long Ogg decode never stalls the mix, and an output thread that renders and writes, paced by
// the sink or the wall clock. Neither touches the model or the socket.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "audio/mixer.h"
#include "audio/sink.h"

namespace fugue::audio {

constexpr int kAudioRate = 44100;  // every clip is resampled anyway (sound.frequency); 44.1 kHz needs no negotiation
constexpr uint8_t kPanBlock = 0x01, kBlock3D = 0x02, kChannelOverride = 0x10;

// One 0x006d frame as typed fields. A frame whose marker block this model cannot account for keeps its head fields only
// (`resolved` false), exactly as the registry decoder carries it as `unresolved`.
struct SoundMsg {
    uint32_t resID = 0;
    uint8_t repeat = 0, wait = 0, channel = 0;
    uint16_t volume = 0;
    bool hasMarker = false, resolved = true;
    uint8_t marker = 0;
    uint16_t pan = 0, channelWide = 0;
    float frequency = 0, x = 0, y = 0, z = 0, falloff = 0;
    uint8_t priority = 0, status = 0;
    int8_t environment = 0;
    // The channel the handler ends up with: the bit-0x10 u16 override when present, else the head's u8.
    uint32_t channelId() const { return hasMarker && (marker & kChannelOverride) ? channelWide : channel; }
    // 0xffff is the null/stop sentinel: "silence this channel", not "play resource 0xffff".
    bool isStop() const { return resID == 0xffff; }
};
bool parseSound(ByteSpan b, SoundMsg& out);
// Wire units -> DM units (volume /255; pan (w-128)/127 only when the pan block is present, else centre; 3D only when the
// 3D block is present). `clip` null = a stop.
Cmd toCmd(const SoundMsg& s, std::shared_ptr<const Clip> clip, const std::string& name);

class Player {
public:
    // Opens the sink from `spec` (see sink.h) and starts both threads. Never fails: an unusable device is a NullSink
    // whose reason is in sinkNote(), and the mixer runs regardless, so stats() still says what would have been heard.
    explicit Player(const std::string& spec, int rate = kAudioRate);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Thread-safe. `bytes` may be empty when hasClip(id) said the clip is already decoded.
    void play(const SoundMsg& s, uint32_t id, Bytes bytes, std::string name);
    void stop(const SoundMsg& s);
    bool hasClip(uint32_t id) const;  // decoded (or known-undecodable) - the caller can skip reading the bytes

    std::string sinkName() const;
    const std::string& sinkNote() const { return sinkNote_; }
    struct Stats {
        int played = 0, stops = 0, undecodable = 0, failed = 0, evicted = 0;
        std::map<std::string, int> byFormat, unsupported;
        size_t live = 0;
        uint64_t blocks = 0,
                 peakBlocks = 0;  // blocks written; blocks whose mix was non-silent (proof PCM reached the sink)
    };
    Stats stats() const;
    Mixer& mixer() { return mix_; }

private:
    struct Job {
        SoundMsg msg;
        uint32_t id = 0;
        Bytes bytes;
        std::string name;
        bool stop = false;
    };
    void decodeLoop();
    void outputLoop();
    std::shared_ptr<const Clip> clipFor(uint32_t id, const Bytes& b, const std::string& name, bool& refused);

    int rate_;
    Mixer mix_;
    std::string spec_, sinkNote_;
    std::unique_ptr<Sink> sink_;
    mutable std::mutex sinkNameMu_;
    std::string sinkName_;

    std::mutex jobMu_;
    std::condition_variable jobCv_;
    std::deque<Job> jobs_;

    mutable std::mutex cacheMu_;
    std::map<uint32_t, std::shared_ptr<const Clip>> clips_;  // id -> decoded; nullptr = negative-cached (refused)
    std::deque<uint32_t> order_;
    size_t cacheBytes_ = 0;
    Stats st_;  // guarded by cacheMu_ (blocks/peakBlocks by the atomics below)

    std::atomic<uint64_t> blocks_{0}, peak_{0};
    std::atomic<bool> quit_{false};
    std::thread decoder_, output_;
};

// The session-thread half: 0x006d frames in, resolved plays out to a Player. Callbacks keep it free of net/ and model/.
class SoundFeed {
public:
    struct Env {
        std::function<bool(uint32_t ordinal, uint32_t& id)> resolve;         // the 0x000e list, as it stands NOW
        std::function<bool(uint32_t id)> held;                               // bytes are stored (readable)
        std::function<bool(uint32_t id, Bytes& b, std::string& name)> read;  // read them
    };
    // How long a play waits for its bytes. A stop does not cancel a play still waiting for its file; the TTL is the
    // backstop that keeps a file that never comes from holding a pending slot (kPendingCap) for the whole session.
    static constexpr int64_t kPendingTtlMs = 300000;
    static constexpr size_t kPendingCap = 128;
    SoundFeed(Player& p, Env env) : p_(p), env_(std::move(env)) {}
    void onFrame(ByteSpan body, int64_t nowMs);
    void pump(int64_t nowMs);  // retry the pending list; cheap when it is empty
    size_t pending() const { return pending_.size(); }
    // Resources a new play is waiting for. They move to the front of the declaration queue (ResourcePump::prioritize),
    // since the pump otherwise declares the whole list in order.
    std::vector<uint32_t> takeWanted() {
        std::vector<uint32_t> v;
        v.swap(wanted_);
        return v;
    }
    int expired() const { return expired_; }
    int malformed() const { return malformed_; }

private:
    enum class R { Ok, Wait };
    R dispatch(const SoundMsg& s, bool fresh);
    struct Pending {
        SoundMsg s;
        int64_t deadline;
    };
    Player& p_;
    Env env_;
    std::vector<Pending> pending_;
    std::vector<uint32_t> wanted_;
    int expired_ = 0, malformed_ = 0;
};

}  // namespace fugue::audio

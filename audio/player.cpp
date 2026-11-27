#include "audio/player.h"
#include "audio/note.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#if !defined(_WIN32)
#include <signal.h>
#include <pthread.h>
#endif

namespace fugue::audio {

namespace {
constexpr size_t kClipCacheBytes = size_t(64) << 20;  // repeated sounds play without re-decoding
constexpr size_t kBlockFrames = 1024;                 // ~23 ms at 44.1 kHz
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
float f32(const uint8_t* p) {
    uint32_t u = rd32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
}  // namespace

// Same framing as proto/dec_s2c_006d_sound.cpp: [ResID u32][Repeat][Wait][Channel u8][Volume u16], then an optional
// Marker byte; 0x01 -> Pan u16, Frequency f32, Priority u8, Status u8, Environment i8; 0x02 -> X,Y,Z,Falloff f32;
// 0x10 -> Channel u16. Any other marker bit, a short block or trailing slack = the head only (`resolved` false).
bool parseSound(ByteSpan b, SoundMsg& s) {
    s = SoundMsg{};
    if (b.size() < 9) return false;
    const uint8_t* p = b.data();
    s.resID = rd32(p);
    s.repeat = p[4];
    s.wait = p[5];
    s.channel = p[6];
    s.volume = rd16(p + 7);
    if (b.size() < 10) return true;
    s.hasMarker = true;
    s.marker = p[9];
    size_t o = 10, n = b.size();
    bool ok = (s.marker & ~(kPanBlock | kBlock3D | kChannelOverride)) == 0;
    if (ok && (s.marker & kPanBlock)) {
        if (n - o < 9)
            ok = false;
        else {
            s.pan = rd16(p + o);
            s.frequency = f32(p + o + 2);
            s.priority = p[o + 6];
            s.status = p[o + 7];
            s.environment = int8_t(p[o + 8]);
            o += 9;
        }
    }
    if (ok && (s.marker & kBlock3D)) {
        if (n - o < 16)
            ok = false;
        else {
            s.x = f32(p + o);
            s.y = f32(p + o + 4);
            s.z = f32(p + o + 8);
            s.falloff = f32(p + o + 12);
            o += 16;
        }
    }
    if (ok && (s.marker & kChannelOverride)) {
        if (n - o < 2)
            ok = false;
        else {
            s.channelWide = rd16(p + o);
            o += 2;
        }
    }
    if (ok && o != n) ok = false;
    if (!ok) {
        SoundMsg h;
        h.resID = s.resID;
        h.repeat = s.repeat;
        h.wait = s.wait;
        h.channel = s.channel;
        h.volume = s.volume;
        h.hasMarker = true;
        h.marker = s.marker;
        h.resolved = false;
        s = h;
    }
    return true;
}

Cmd toCmd(const SoundMsg& s, std::shared_ptr<const Clip> clip, const std::string& name) {
    Cmd c;
    c.channel = int(s.channelId());
    c.clip = std::move(clip);
    c.volume = std::min(1.0, double(s.volume) / 255.0);
    c.repeat = s.repeat;
    c.wait = s.wait != 0;
    c.priority = s.priority;
    c.status = s.status;
    c.name = name;
    if (s.hasMarker && (s.marker & kPanBlock)) {
        c.pan = (double(s.pan) - 128.0) / 127.0;
        c.frequency = s.frequency;
    }
    if (s.hasMarker && (s.marker & kBlock3D)) {
        c.x = s.x;
        c.y = s.y;
        c.z = s.z;
        c.falloff = s.falloff;
        c.spatial = true;
    }
    return c;
}

// ---- Player --------------------------------------------------------------------------------------------------------

Player::Player(const std::string& spec, int rate) : rate_(rate), mix_(rate), spec_(spec) {
    std::string why, skipped;
    sink_ = openSink(spec, rate, &why, &skipped);
    sinkNote_ = why;
    sinkName_ = sink_->name();
    note(std::to_string(rate) + " Hz stereo -> " + sinkName_ + (why.empty() ? "" : ": " + why) +
         (skipped.empty() ? "" : " (not used: " + skipped + ")"));
    decoder_ = std::thread([this] { decodeLoop(); });
    output_ = std::thread([this] { outputLoop(); });
}

Player::~Player() {
    quit_ = true;
    jobCv_.notify_all();
    if (decoder_.joinable()) decoder_.join();
    if (output_.joinable()) output_.join();
    if (sink_) sink_->close();
}

void Player::play(const SoundMsg& s, uint32_t id, Bytes bytes, std::string name) {
    std::lock_guard<std::mutex> g(jobMu_);
    if (jobs_.size() >= 1024) return;  // a wedged decoder must not grow this without bound
    jobs_.push_back(Job{s, id, std::move(bytes), std::move(name), false});
    jobCv_.notify_one();
}

void Player::stop(const SoundMsg& s) {
    std::lock_guard<std::mutex> g(jobMu_);
    jobs_.push_back(Job{s, 0, {}, {}, true});
    jobCv_.notify_one();
}

bool Player::hasClip(uint32_t id) const {
    std::lock_guard<std::mutex> g(cacheMu_);
    return clips_.count(id) != 0;
}

std::string Player::sinkName() const {
    std::lock_guard<std::mutex> g(sinkNameMu_);
    return sinkName_;
}

Player::Stats Player::stats() const {
    Stats s;
    {
        std::lock_guard<std::mutex> g(cacheMu_);
        s = st_;
    }
    s.live = mix_.active();
    s.blocks = blocks_.load();
    s.peakBlocks = peak_.load();
    return s;
}

// Decoded-clip cache. Caller holds cacheMu_. Both refusal kinds are negative-cached: retrying a MIDI every play would burn
// CPU forever, and the counters are what make the gap reportable.
std::shared_ptr<const Clip> Player::clipFor(uint32_t id, const Bytes& b, const std::string& name, bool& refused) {
    refused = false;
    if (auto it = clips_.find(id); it != clips_.end()) {
        refused = !it->second;
        return it->second;
    }
    if (b.empty()) {
        st_.evicted++;
        refused = true;
        return nullptr;
    }  // the caller trusted a cache entry that has since gone
    std::string err;
    bool unsup = false;
    std::shared_ptr<const Clip> c = decode(ByteSpan(b.data(), b.size()), &err, &unsup);
    clips_[id] = c;
    if (!c) {
        refused = true;
        if (unsup) {
            st_.undecodable++;
            std::string k = sniff(ByteSpan(b.data(), b.size()));
            st_.unsupported[k.empty() ? "unknown" : k]++;
        } else
            st_.failed++;
        char hexId[16];
        std::snprintf(hexId, sizeof hexId, "%08x", id);
        note(std::string("res ") + hexId + (name.empty() ? "" : " (" + name + ")") + " refused: " + err);
        return nullptr;
    }
    order_.push_back(id);
    cacheBytes_ += c->samples.size() * sizeof(float) + c->sourceBytes;
    st_.byFormat[c->format]++;
    while (cacheBytes_ > kClipCacheBytes && order_.size() > 1) {
        uint32_t old = order_.front();
        order_.pop_front();
        if (auto it = clips_.find(old); it != clips_.end()) {
            if (it->second) cacheBytes_ -= it->second->samples.size() * sizeof(float) + it->second->sourceBytes;
            clips_.erase(it);
        }
    }
    return c;
}

void Player::decodeLoop() {
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> g(jobMu_);
            jobCv_.wait(g, [&] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            j = std::move(jobs_.front());
            jobs_.pop_front();
        }
        if (j.stop) {
            mix_.apply(toCmd(j.msg, nullptr, ""));
            std::lock_guard<std::mutex> g(cacheMu_);
            st_.stops++;
            continue;
        }
        std::shared_ptr<const Clip> c;
        {
            std::lock_guard<std::mutex> g(cacheMu_);
            bool refused = false;
            c = clipFor(j.id, j.bytes, j.name, refused);
            if (!c) continue;
            st_.played++;
        }
        mix_.apply(toCmd(j.msg, std::move(c), j.name));
    }
}

// Renders in ~23 ms blocks and lets the sink pace (a pipe or device blocks once full); a sink that accepts everything at
// once (Sink::instant) is throttled by the wall clock so a silent client does not spin a core. A failed write re-opens the sink with exponential
// backoff (capped at 30 s) instead of going silent for the session.
void Player::outputLoop() {
#if !defined(_WIN32)
    // A player process that dies turns our next write into SIGPIPE, whose default action kills the whole client. Block it
    // on THIS thread (the only one that writes the pipe) so the write fails with EPIPE and the sink is re-opened.
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &ss, nullptr);
#endif
    std::vector<float> buf(kBlockFrames * 2);
    using clk = std::chrono::steady_clock;
    const auto period = std::chrono::nanoseconds(int64_t(double(kBlockFrames) / rate_ * 1e9));
    auto next = clk::now();
    auto backoff = std::chrono::seconds(1);
    clk::time_point retryAt{};
    auto reopen = [&](const std::string& reason) {
        note("sink " + sink_->name() + ": " + reason + " - reopening");
        sink_->close();
        std::string why;
        sink_ = openSink(spec_, rate_, &why);
        if (dynamic_cast<NullSink*>(sink_.get()) && spec_ != "none" && spec_ != "null" && spec_ != "off") {
            note("reopen failed: " + why);
            retryAt = clk::now() + backoff;
            backoff = std::min<std::chrono::seconds>(backoff * 2, std::chrono::seconds(30));
        } else {
            note("reopened -> " + sink_->name());
            backoff = std::chrono::seconds(1);
            retryAt = {};
        }
        std::lock_guard<std::mutex> g(sinkNameMu_);
        sinkName_ = sink_->name();
    };
    while (!quit_) {
        if (retryAt != clk::time_point{} && clk::now() > retryAt) {
            retryAt = {};
            reopen("scheduled retry");
        }
        mix_.render(buf.data(), kBlockFrames);
        bool loud = false;
        for (float v : buf)
            if (v != 0.f) {
                loud = true;
                break;
            }
        std::string err;
        if (!sink_->write(buf.data(), kBlockFrames, &err)) reopen(err);
        blocks_++;
        if (loud) peak_++;
        // Only a sink with nothing behind it is paced by the clock. A device or pipe sink blocks in write() once its
        // buffer is full, and that is what keeps blocks queued ahead of the player: sleeping to each block's deadline
        // as well left at most one block in the pipe, so pw-play found it empty between wake-ups and played gaps in
        // any sustained sound.
        auto now = clk::now();
        if (!sink_->instant()) {
            next = now;
            continue;
        }
        next += period;
        if (next > now)
            std::this_thread::sleep_for(next - now);
        else if (now - next > std::chrono::seconds(1))
            next = now;
    }
}

// ---- SoundFeed (session thread) -------------------------------------------------------------------------------------

SoundFeed::R SoundFeed::dispatch(const SoundMsg& s, bool fresh) {
    // A STOP carries no resource and never waits for one. 0xffff on channel 0 stops everything.
    if (s.isStop()) {
        p_.stop(s);
        return R::Ok;
    }
    uint32_t id = 0;
    if (!env_.resolve(s.resID, id)) return R::Wait;  // re-resolved on every retry: the 0x000e list may still be growing
    if (p_.hasClip(id)) {
        p_.play(s, id, {}, {});
        return R::Ok;
    }
    if (!env_.held(id)) {
        if (fresh) wanted_.push_back(id);
        return R::Wait;
    }
    Bytes b;
    std::string name;
    if (!env_.read(id, b, name)) return R::Wait;  // failed verification: the pump has re-declared it
    p_.play(s, id, std::move(b), std::move(name));
    return R::Ok;
}

void SoundFeed::onFrame(ByteSpan body, int64_t nowMs) {
    SoundMsg s;
    if (!parseSound(body, s)) {
        malformed_++;
        return;
    }
    if (dispatch(s, true) == R::Wait) {
        if (pending_.size() < kPendingCap)
            pending_.push_back({s, nowMs + kPendingTtlMs});
        else {
            expired_++;
            note("play on channel " + std::to_string(s.channelId()) + " dropped: " + std::to_string(kPendingCap) +
                 " plays already wait for their files");
        }
    }
}

void SoundFeed::pump(int64_t nowMs) {
    if (pending_.empty()) return;
    std::vector<Pending> keep;
    for (Pending& p : pending_) {
        if (dispatch(p.s, false) == R::Ok) continue;
        if (nowMs > p.deadline) {
            expired_++;
            uint32_t id = 0;
            char what[64];
            if (env_.resolve(p.s.resID, id))
                std::snprintf(what, sizeof what, "res %08x", id);
            else
                std::snprintf(what, sizeof what, "resource ordinal %u (not in the resource list)", p.s.resID);
            note(std::string("play on channel ") + std::to_string(p.s.channelId()) + " dropped: " + what +
                 " did not arrive within " + std::to_string(kPendingTtlMs / 1000) + " s");
            continue;
        }
        keep.push_back(p);
    }
    pending_.swap(keep);
}

}  // namespace fugue::audio

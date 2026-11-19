// Recorder: FUGUE_RECORD_DIR=<dir> makes the app write the session it plays, in the development tools' recording
// layout, plus what a replay needs to run it as the app did. fromEnv() is null when the variable
// is unset, and every call site is guarded by that pointer, so an unset variable costs one branch per frame.
//
//   frames.txt     "0xTTTT len=N <hex>" per s2c frame, N = body+1 (0x009f resource-pump frames are not recorded)
//   times.txt      one line per frame in frames.txt: microseconds since the recorder started (arrival time);
//                  a line "S" starts a new session (a reconnect), the next frame is its first
//   c2s.txt        "<usec> 0xTTTT <hex>" per client->server frame; Login and CertAnswer bodies are redacted
//   res/<resid>    each verified resource, named by its CRC-32/XFER id
//   markers.jsonl  one JSON object per event the player flags (a right-click menu, a screenshot):
//                  {"t_us":..,"frame":N,"eye":[x,y,z],"kind":"menu"|"screenshot",...}
//   manifest.json  {client, start_unix_ns, world}; start_unix_ns anchors times.txt on the wall clock the model was fed
#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "net/bytes.h"

namespace fugue::model {

class Recorder {
public:
    // nullptr when FUGUE_RECORD_DIR is unset, empty or cannot be created (the reason goes to stderr).
    static std::unique_ptr<Recorder> fromEnv(const std::string& world);
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void newSession();  // session thread
    void s2c(uint16_t type, ByteSpan body);
    void c2s(uint16_t type, ByteSpan body);
    void resource(uint32_t id, const std::string& name, ByteSpan data);
    void setEye(int x, int y, int z);  // after each ingest: markers carry it from any thread
    // Any thread. `fields` is the rest of the JSON object, without braces (e.g. "\"px\":3,\"py\":4").
    void marker(const char* kind, const std::string& fields);
    static std::string quote(const std::string& s);  // a JSON string literal

private:
    Recorder() = default;
    int64_t nowUs() const;
    std::mutex mu_;
    FILE *frames_ = nullptr, *times_ = nullptr, *c2s_ = nullptr, *markers_ = nullptr;
    std::string dir_;
    std::chrono::steady_clock::time_point t0_;
    uint64_t nframes_ = 0;
    std::map<uint32_t, bool> resSeen_;
    std::atomic<int> eyeX_{0}, eyeY_{0}, eyeZ_{0};
};

}  // namespace fugue::model

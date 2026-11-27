#include "fugue_env.h"
#include "audio/sink.h"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include "audio/clip.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include "platform/posix.h"
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fugue::audio {
#ifdef _WIN32
std::unique_ptr<Sink> openWindowsSink(int rate, std::string* err);
#endif

namespace {
class WavSink : public Sink {
public:
    WavSink(std::FILE* f, std::string path, int rate) : f_(f), path_(std::move(path)), rate_(rate) {}
    ~WavSink() override { close(); }
    bool write(const float* p, size_t frames, std::string*) override {
        buf_.insert(buf_.end(), p, p + frames * 2);
        return true;
    }
    void close() override {
        if (!f_) return;
        Bytes b = encodeWav16(rate_, 2, buf_);
        std::fwrite(b.data(), 1, b.size(), f_);
        std::fclose(f_);
        f_ = nullptr;
    }
    std::string name() const override { return "wav:" + path_; }
    bool instant() const override { return true; }

private:
    std::FILE* f_;
    std::string path_;
    int rate_;
    std::vector<float> buf_;
};

#if !defined(_WIN32)
class ExecSink : public Sink {
public:
    ExecSink(pid_t pid, int in, int err, std::string name) : pid_(pid), in_(in), err_(err), name_(std::move(name)) {}
    ~ExecSink() override { close(); }
    bool write(const float* p, size_t frames, std::string* err) override {
        if (in_ < 0) {
            if (err) *err = "closed";
            return false;
        }
        buf_.resize(frames * 4);
        for (size_t i = 0; i < frames * 2; i++) {
            uint16_t u = uint16_t(toS16(p[i]));
            buf_[i * 2] = uint8_t(u);
            buf_[i * 2 + 1] = uint8_t(u >> 8);
        }
        // A dead player must surface as EPIPE, not a SIGPIPE that kills the client. The output thread blocks SIGPIPE in
        // its own mask (Player::outputLoop), so a write to a closed pipe returns EPIPE.
        size_t off = 0;
        while (off < buf_.size()) {
            ssize_t n = ::write(in_, buf_.data() + off, buf_.size() - off);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                // errno first: childSaid() reads a non-blocking pipe, which would replace it (often with EAGAIN).
                const std::string why = std::string("write: ") + std::strerror(errno);
                if (err) *err = why + childSaid();
                return false;
            }
            off += size_t(n);
        }
        return true;
    }
    void close() override {
        if (in_ >= 0) {
            ::close(in_);
            in_ = -1;
        }
        if (pid_ > 0) {
            // The player drains and exits on EOF; give it a moment, then make sure it does not outlive us.
            for (int i = 0; i < 50; i++) {
                if (::waitpid(pid_, nullptr, WNOHANG) == pid_) {
                    pid_ = -1;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (pid_ > 0) {
                ::kill(pid_, SIGTERM);
                ::waitpid(pid_, nullptr, 0);
                pid_ = -1;
            }
        }
        if (err_ >= 0) {
            ::close(err_);
            err_ = -1;
        }
    }
    std::string name() const override { return name_; }

private:
    std::string childSaid() {
        if (err_ < 0) return "";
        char b[1024];
        ssize_t n = ::read(err_, b, sizeof b - 1);
        if (n <= 0) return "";
        std::string s(b, size_t(n));
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return " (" + s + ")";
    }
    pid_t pid_;
    int in_, err_;
    std::string name_;
    std::vector<uint8_t> buf_;
};
#endif
}  // namespace

std::unique_ptr<Sink> openWavSink(const std::string& path, int rate, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "open " + path + ": " + std::strerror(errno);
        return nullptr;
    }
    return std::make_unique<WavSink>(f, path, rate);
}

std::vector<std::vector<std::string>> execPlayers(int rate) {
    std::string r = std::to_string(rate);
    return {
        // --raw: without it pw-play hands stdin to libsndfile, which wants a file header and exits on the first write.
        // A pw-play too old to know --raw exits inside the start grace window, so paplay takes over.
        {"pw-play", "--raw", "--format", "s16", "--rate", r, "--channels", "2", "--latency", "80ms", "-"},
        {"paplay", "--raw", "--format=s16le", "--rate=" + r, "--channels=2", "--latency-msec=80"},
        {"aplay", "-q", "-t", "raw", "-f", "S16_LE", "-r", r, "-c", "2", "--buffer-time=160000", "--period-time=40000",
         "-"},
    };
}

std::unique_ptr<Sink> openExecSink(const std::vector<std::string>& argv, std::string* err) {
#if defined(_WIN32)
    if (err) *err = "cmd sinks are not supported on Windows";
    return nullptr;
#else
    if (argv.empty() || argv[0].empty()) {
        if (err) *err = "empty command";
        return nullptr;
    }
    int in[2], er[2];
    if (platform::pipeCloseOnExec(in) != 0) {
        if (err) *err = "pipe failed";
        return nullptr;
    }
    if (platform::pipeCloseOnExec(er) != 0) {
        ::close(in[0]);
        ::close(in[1]);
        if (err) *err = "pipe failed";
        return nullptr;
    }
    // A small pipe: what sits in it is latency on top of the player's own 80 ms. 8 KiB = ~46 ms of 44.1 kHz s16 stereo.
#ifdef F_SETPIPE_SZ
    (void)::fcntl(in[1], F_SETPIPE_SZ, 8192);
#endif
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0);
    posix_spawn_file_actions_adddup2(&fa, er[1], 2);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(&at, &def);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&at, &none);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    std::vector<char*> av;
    for (const std::string& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    pid_t pid = -1;
    int rc = ::posix_spawnp(&pid, argv[0].c_str(), &fa, &at, av.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    ::close(in[0]);
    ::close(er[1]);
    if (rc != 0) {
        ::close(in[1]);
        ::close(er[0]);
        if (err) *err = argv[0] + ": " + std::strerror(rc);
        return nullptr;
    }
    (void)::fcntl(er[0], F_SETFL, O_NONBLOCK);
    // The start grace window: pw-play with no reachable daemon exits well inside 50 ms.
    for (int i = 0; i < 15; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        int status = 0;
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            char b[512];
            ssize_t n = ::read(er[0], b, sizeof b - 1);
            std::string said = n > 0 ? std::string(b, size_t(n)) : std::string("exited with no output");
            while (!said.empty() && (said.back() == '\n' || said.back() == '\r')) said.pop_back();
            ::close(in[1]);
            ::close(er[0]);
            if (err) *err = argv[0] + " exited immediately (status " + std::to_string(status) + "): " + said;
            return nullptr;
        }
    }
    return std::make_unique<ExecSink>(pid, in[1], er[0], "cmd:" + argv[0]);
#endif
}

std::unique_ptr<Sink> openSink(const std::string& specIn, int rate, std::string* err, std::string* skipped) {
    if (skipped) skipped->clear();
    std::string spec = specIn;
    if (spec.empty())
        if (auto e = fugueEnv("AUDIO")) spec = *e;
    if (spec.empty()) spec = "auto";
    std::string why;
    if (spec == "none" || spec == "null" || spec == "off") return std::make_unique<NullSink>();
    if (spec.rfind("wav:", 0) == 0) {
        if (auto s = openWavSink(spec.substr(4), rate, &why)) return s;
    } else if (spec.rfind("cmd:", 0) == 0) {
        std::vector<std::string> argv;
        std::string cur;
        for (char c : spec.substr(4)) {
            if (c == ',') {
                argv.push_back(cur);
                cur.clear();
            } else
                cur.push_back(c);
        }
        argv.push_back(cur);
        if (auto s = openExecSink(argv, &why)) return s;
    } else if (spec == "auto") {
#ifdef __APPLE__
        if (auto s = openCoreAudioSink(rate, &why)) return s;
        why = "Core Audio: " + why + " - running SILENT";
#elif defined(_WIN32)
        if (auto s = openWindowsSink(rate, &why)) return s;
        why += " - running SILENT";
#else
        std::string all;
        for (const auto& argv : execPlayers(rate)) {
            std::string e;
            if (auto s = openExecSink(argv, &e)) {
                if (skipped) *skipped = all;
                return s;
            }
            all += (all.empty() ? "" : "; ") + e;
        }
        why = "no working PCM player (" + all + ") - running SILENT";
#endif
    } else {
        why = "unknown sink spec \"" + spec + "\"";
    }
    if (err) *err = why;
    return std::make_unique<NullSink>();
}

}  // namespace fugue::audio

#include "model/recorder.h"
#include <ctime>
#include <filesystem>
#include <fstream>
#include "fugue_env.h"
#include "proto/msg.h"

namespace fugue::model {

std::unique_ptr<Recorder> Recorder::fromEnv(const std::string& world) {
    std::string dir = fugueEnvOr("RECORD_DIR");
    if (dir.empty()) return nullptr;
    std::unique_ptr<Recorder> r(new Recorder());
    std::error_code ec;
    std::filesystem::create_directories(dir + "/res", ec);
    r->dir_ = dir;
    r->frames_ = std::fopen((dir + "/frames.txt").c_str(), "w");
    r->times_ = std::fopen((dir + "/times.txt").c_str(), "w");
    r->c2s_ = std::fopen((dir + "/c2s.txt").c_str(), "w");
    r->markers_ = std::fopen((dir + "/markers.jsonl").c_str(), "w");
    if (ec || !r->frames_ || !r->times_ || !r->c2s_ || !r->markers_) {
        std::fprintf(stderr, "[record] cannot create %s - not recording\n", dir.c_str());
        return nullptr;
    }
    r->t0_ = std::chrono::steady_clock::now();
    int64_t unixNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    std::fprintf(r->frames_,
                 "=== fuguedump record: decrypted s2c frames (0x009f resource-pump frames are not recorded; see res/) "
                 "[fugue-qt app, world=%s] ===\n",
                 world.c_str());
    std::fflush(r->frames_);
    std::ofstream(dir + "/manifest.json", std::ios::trunc)
        << "{\n  \"client\": \"fugue-qt\",\n  \"source\": \"app-session\",\n  \"start_unix_ns\": " << unixNs
        << ",\n  \"world\": " << quote(world) << "\n}\n";
    std::fprintf(stderr, "[record] recording this session into %s\n", dir.c_str());
    return r;
}

Recorder::~Recorder() {
    for (FILE* f : {frames_, times_, c2s_, markers_})
        if (f) std::fclose(f);
}

int64_t Recorder::nowUs() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0_).count();
}

std::string Recorder::quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            o += '\\';
            o += char(c);
        } else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            o += b;
        } else
            o += char(c);
    }
    return o + "\"";
}

void Recorder::newSession() {
    std::lock_guard<std::mutex> g(mu_);
    std::fprintf(times_, "S\n");
    std::fflush(times_);
}

void Recorder::s2c(uint16_t type, ByteSpan body) {
    if (type == S2C::Resource) return;
    std::lock_guard<std::mutex> g(mu_);
    std::fprintf(frames_, "0x%04x len=%zu %s\n", type, body.size() + 1, hexEncode(body).c_str());
    std::fprintf(times_, "%lld\n", (long long)nowUs());
    std::fflush(frames_);
    std::fflush(times_);
    nframes_++;
}

void Recorder::c2s(uint16_t type, ByteSpan body) {
    std::lock_guard<std::mutex> g(mu_);
    // Login and CertAnswer carry live credentials.
    if (type == C2S::Login || type == C2S::CertAnswer)
        std::fprintf(c2s_, "%lld 0x%04x <redacted>\n", (long long)nowUs(), type);
    else
        std::fprintf(c2s_, "%lld 0x%04x %s\n", (long long)nowUs(), type, hexEncode(body).c_str());
    std::fflush(c2s_);
}

void Recorder::resource(uint32_t id, const std::string&, ByteSpan data) {
    std::lock_guard<std::mutex> g(mu_);
    if (!resSeen_.emplace(id, true).second) return;
    char n[16];
    std::snprintf(n, sizeof n, "%08x", id);
    std::ofstream(dir_ + "/res/" + n, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

void Recorder::setEye(int x, int y, int z) {
    eyeX_ = x;
    eyeY_ = y;
    eyeZ_ = z;
}

void Recorder::marker(const char* kind, const std::string& fields) {
    std::lock_guard<std::mutex> g(mu_);
    std::time_t t = std::time(nullptr);
    char wall[40];
    std::strftime(wall, sizeof wall, "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    std::fprintf(markers_, "{\"t_us\":%lld,\"wall\":\"%s\",\"frame\":%llu,\"eye\":[%d,%d,%d],\"kind\":\"%s\"%s%s}\n",
                 (long long)nowUs(), wall, (unsigned long long)nframes_, eyeX_.load(), eyeY_.load(), eyeZ_.load(), kind,
                 fields.empty() ? "" : ",", fields.c_str());
    std::fflush(markers_);
}

}  // namespace fugue::model

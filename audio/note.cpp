#include "audio/note.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace fugue::audio {

namespace {
std::mutex mu;
std::FILE* file = nullptr;
std::string path;
}  // namespace

bool setNoteFile(const std::string& p, std::string* err) {
    std::lock_guard<std::mutex> g(mu);
    if (file) {
        std::fclose(file);
        file = nullptr;
    }
    path.clear();
    if (p.empty()) return true;
    file = std::fopen(p.c_str(), "w");
    if (!file) {
        if (err) *err = "open " + p + ": " + std::strerror(errno);
        return false;
    }
    path = p;
    return true;
}

std::string noteFile() {
    std::lock_guard<std::mutex> g(mu);
    return path;
}

void note(const std::string& line) {
    std::lock_guard<std::mutex> g(mu);
    std::fprintf(stderr, "[audio] %s\n", line.c_str());
    if (!file) return;
    std::time_t t = std::time(nullptr);
    char ts[32] = "";
    std::tm tm{};
#if defined(_WIN32)
    bool ok = localtime_s(&tm, &t) == 0;
#else
    bool ok = localtime_r(&t, &tm) != nullptr;
#endif
    if (ok) std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    std::fprintf(file, "%s %s\n", ts, line.c_str());
    std::fflush(file);
}

}  // namespace fugue::audio

#include "net/rscstore.h"
#include "platform/file.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include "net/resource.h"

namespace fugue::net {
namespace fs = std::filesystem;
namespace {
constexpr double kEvictTarget = 0.9;
constexpr time_t kTouchInterval = 24 * 3600;

int hexVal(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
// 8 hex chars, either case on read.
bool parseId(const std::string& s, uint32_t& out) {
    if (s.size() != 8) return false;
    uint32_t v = 0;
    for (char c : s) {
        int d = hexVal(c);
        if (d < 0) return false;
        v = v << 4 | uint32_t(d);
    }
    out = v;
    return true;
}
bool parseShard(const std::string& s) { return s.size() == 2 && hexVal(s[0]) >= 0 && hexVal(s[1]) >= 0; }

// mkdir -p with mode 0700.
bool mkdirs(const std::string& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) return true;
    size_t slash = p.rfind('/');
    if (slash != std::string::npos && slash > 0 && !mkdirs(p.substr(0, slash))) return false;
    return platform::mkdir(p.c_str(), 0700) == 0 || errno == EEXIST;
}
}  // namespace

std::string RscStore::pathFor(const std::string& dir, uint32_t id) {
    char b[16];
    std::snprintf(b, sizeof b, "%02x/%08x", unsigned(id >> 24), unsigned(id));
    return dir + "/" + b;
}

std::string RscStore::defaultDir() {
    auto h = platform::homeDir();
    return h.empty() ? std::string() : h + "/.fugue/rsc";
}

RscStore::RscStore(std::string dir, int64_t capBytes)
    : dir_(std::move(dir)), cap_(capBytes > 0 ? capBytes : kDefaultCapBytes) {
    if (dir_.empty() || !mkdirs(dir_)) return;
    scan();
    loadNames();
    nameLog_ = std::fopen((dir_ + "/names").c_str(), "ab");
    if (nameLog_) platform::fchmod(platform::fileno(nameLog_), 0600);
    ok_ = nameLog_ != nullptr;
    evict();
}

RscStore::~RscStore() {
    if (nameLog_) std::fclose(nameLog_);
}

void RscStore::scan() {
    std::error_code ec;
    for (fs::directory_iterator sh(dir_, ec), end; !ec && sh != end; sh.increment(ec)) {
        std::error_code e2;
        if (!sh->is_directory(e2) || !parseShard(sh->path().filename().string())) continue;
        for (fs::directory_iterator f(sh->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
            uint32_t id;
            if (!parseId(f->path().filename().string(), id)) continue;
            struct stat sb;
            if (::stat(f->path().string().c_str(), &sb) != 0) continue;
            index_[id] = Entry{int64_t(sb.st_size), "", sb.st_mtime};
            bytes_ += sb.st_size;
        }
    }
}

void RscStore::loadNames() {
    std::ifstream f(dir_ + "/names", std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        size_t t = line.find('\t');
        if (t == std::string::npos) continue;
        uint32_t id;
        if (!parseId(line.substr(0, t), id)) continue;
        auto it = index_.find(id);
        if (it != index_.end()) it->second.name = line.substr(t + 1);
    }
}

void RscStore::appendName(uint32_t id, const std::string& name) {
    if (!nameLog_) return;
    std::string n = name;
    for (char& c : n)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';  // keep the names log line-framed
    std::fprintf(nameLog_, "%08x\t%s\n", unsigned(id), n.c_str());
    std::fflush(nameLog_);
}

void RscStore::forget(uint32_t id) {
    auto it = index_.find(id);
    if (it != index_.end()) {
        bytes_ -= it->second.size;
        index_.erase(it);
    }
    platform::unlink(pathFor(dir_, id).c_str());
}

bool RscStore::get(uint32_t id, Bytes& data, std::string* name) {
    auto it = index_.find(id);
    if (it == index_.end()) {
        st_.misses++;
        return false;
    }
    std::string nm = it->second.name;
    time_t used = it->second.used;
    std::string p = pathFor(dir_, id);
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        forget(id);
        st_.misses++;
        return false;
    }
    Bytes b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (resId(ByteSpan(b.data(), b.size())) != id) {
        // Close before deleting: Windows does not allow deleting an open stream.
        f.close();
        // The bytes do not hash to the id: drop and delete the file; the caller fetches the real bytes.
        forget(id);
        st_.deletedBad++;
        st_.misses++;
        return false;
    }
    st_.hits++;
    time_t now = std::time(nullptr);
    if (now - used > kTouchInterval) {
        it->second.used = now;
        std::error_code ec;
        fs::last_write_time(p, fs::file_time_type::clock::now(), ec);
    }
    data = std::move(b);
    if (name) *name = std::move(nm);
    return true;
}

bool RscStore::put(uint32_t id, const std::string& name, ByteSpan data) {
    if (data.empty()) return false;
    if (resId(data) != id) {
        st_.rejected++;
        return false;
    }
    if (int64_t(data.size()) > cap_) return false;
    auto it = index_.find(id);
    if (it != index_.end()) {
        if (!name.empty() && name != it->second.name) {
            it->second.name = name;
            appendName(id, name);
        }
        return true;
    }
    std::string p = pathFor(dir_, id);
    std::string shard = p.substr(0, p.rfind('/'));
    if (!mkdirs(shard)) return false;
    // temp file in the SAME directory, then rename: a reader never sees a half-written resource under a hash name.
    static std::mt19937_64 rng{std::random_device{}()};
    std::string tmp;
    int fd = -1;
    for (int i = 0; i < 16 && fd < 0; i++) {
        char r[32];
        std::snprintf(r, sizeof r, ".tmp-%016llx", (unsigned long long)rng());
        tmp = shard + "/" + r;
        fd = platform::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0 && errno != EEXIST) return false;
    }
    if (fd < 0) return false;
    size_t off = 0;
    while (off < data.size()) {
        auto n = platform::write(fd, data.data() + off, unsigned(std::min(data.size() - off, size_t(1 << 30))));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            platform::close(fd);
            platform::unlink(tmp.c_str());
            return false;
        }
        off += size_t(n);
    }
    if (platform::close(fd) != 0 || platform::rename(tmp.c_str(), p.c_str()) != 0) {
        platform::unlink(tmp.c_str());
        return false;
    }
    index_[id] = Entry{int64_t(data.size()), name, std::time(nullptr)};
    bytes_ += int64_t(data.size());
    st_.puts++;
    if (!name.empty()) appendName(id, name);
    evict();
    return true;
}

void RscStore::evict() {
    if (bytes_ <= cap_) return;
    std::vector<std::pair<time_t, uint32_t>> all;
    all.reserve(index_.size());
    for (auto& [id, e] : index_) all.push_back({e.used, id});
    std::sort(all.begin(), all.end());
    int64_t target = int64_t(double(cap_) * kEvictTarget);
    for (auto& [t, id] : all) {
        if (bytes_ <= target) break;
        forget(id);
        st_.evicted++;
    }
}

RscStoreStats RscStore::stats() const {
    RscStoreStats s = st_;
    s.files = index_.size();
    s.bytes = uint64_t(bytes_);
    return s;
}

void RscStore::usage(const std::string& dir, uint64_t& files, uint64_t& bytes) {
    files = bytes = 0;
    std::error_code ec;
    for (fs::directory_iterator sh(dir, ec), end; !ec && sh != end; sh.increment(ec)) {
        std::error_code e2;
        if (!sh->is_directory(e2) || !parseShard(sh->path().filename().string())) continue;
        for (fs::directory_iterator f(sh->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
            uint32_t id;
            if (!parseId(f->path().filename().string(), id)) continue;
            std::error_code e3;
            auto sz = f->file_size(e3);
            if (e3) continue;
            files++;
            bytes += sz;
        }
    }
}

void RscStore::clear(const std::string& dir, uint64_t* files, uint64_t* bytes) {
    uint64_t nf = 0, nb = 0;
    std::error_code ec;
    for (fs::directory_iterator sh(dir, ec), end; !ec && sh != end; sh.increment(ec)) {
        std::error_code e2;
        if (!sh->is_directory(e2) || !parseShard(sh->path().filename().string())) continue;
        size_t total = 0, removed = 0;
        for (fs::directory_iterator f(sh->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
            total++;
            uint32_t id;
            if (!parseId(f->path().filename().string(), id)) continue;
            std::error_code e3;
            auto sz = f->file_size(e3);
            if (platform::unlink(f->path().string().c_str()) == 0) {
                nf++;
                nb += sz;
                removed++;
            }
        }
        if (removed == total) fs::remove(sh->path(), e2);
    }
    platform::unlink((dir + "/names").c_str());
    if (files) *files = nf;
    if (bytes) *bytes = nb;
}

}  // namespace fugue::net

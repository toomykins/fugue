#include "pager/cache.h"
#include "platform/file.h"
#include <filesystem>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#ifndef _WIN32
#include <csignal>
#include <unistd.h>
#endif
#include "account/store.h"
#include "net/cauth.h"
#include "net/guest.h"
#include "net/rscstore.h"
#include "pager/launch.h"
#include "pager/model.h"

namespace fugue::pager {
namespace {
bool regularSize(const std::string& p, uint64_t* sz) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(p, ec))) return false;
    *sz = std::filesystem::file_size(p, ec);
    if (ec) return false;
    return true;
}

std::string webDir() {
    std::string sd = net::defaultStateDir();
    return sd.empty() ? sd : sd + "/web";
}

// True while a client holds the web profile's QLockFile (<web>/.lock): its first line is the owner's pid.
bool webStorageInUse(const std::string& web) {
    long long pid = 0;
    {
        std::ifstream f(web + "/.lock");  // closed before the Windows delete probe, which our own handle would fail
        if (!(f >> pid) || pid <= 0) return false;
    }
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::path lk = std::filesystem::path(web) / ".lock";
    return !std::filesystem::remove(lk, ec) && ec;  // an open QLockFile cannot be deleted on Windows
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

void webUsage(const std::string& web, uint64_t& files, uint64_t& bytes) {
    files = bytes = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator
             it(web, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2) || it->path().filename() == ".lock") continue;
        auto sz = it->file_size(e2);
        if (e2) continue;
        files++;
        bytes += sz;
    }
}
}  // namespace

uint64_t CachePlan::files() const {
    uint64_t n = 0;
    for (auto& i : items) n += i.files;
    return n;
}
uint64_t CachePlan::bytes() const {
    uint64_t n = 0;
    for (auto& i : items) n += i.bytes;
    return n;
}

std::string humanBytes(uint64_t n) {
    char b[64];
    if (n < 1024)
        std::snprintf(b, sizeof b, "%llu bytes", (unsigned long long)n);
    else if (n < (1ULL << 20))
        std::snprintf(b, sizeof b, "%.1f KiB", double(n) / 1024);
    else if (n < (1ULL << 30))
        std::snprintf(b, sizeof b, "%.1f MiB", double(n) / (1 << 20));
    else
        std::snprintf(b, sizeof b, "%.2f GiB", double(n) / (1ULL << 30));
    return b;
}

std::vector<std::string> savedSignInNames() {
    std::vector<std::string> out;
    std::string e, fd = account::fugueDir(&e);
    if (fd.empty()) return out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(fd, ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = it->path().filename().string();
        uint64_t sz;
        if (n.rfind("identity-", 0) == 0 && n.size() > 9 && regularSize(fd + "/" + n, &sz)) out.push_back(n.substr(9));
    }
    std::sort(out.begin(), out.end());
    return out;
}

CachePlan planClearCache() {
    CachePlan p;
    std::string e, fd = account::fugueDir(&e);
    if (fd.empty()) return p;
    std::vector<std::string> ids;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(fd, ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = it->path().filename().string();
        if (n.rfind("identity-", 0) == 0 && n.size() > 9) ids.push_back(n);
    }
    std::sort(ids.begin(), ids.end());
    for (auto& n : ids) {
        CacheItem it;
        it.path = fd + "/" + n;
        if (!regularSize(it.path, &it.bytes)) continue;
        it.files = 1;
        it.what = "Saved sign-in for " + n.substr(9);
        p.items.push_back(it);
    }
    if (std::string g = guestChoicePath(); !g.empty()) {
        CacheItem it;
        it.path = g;
        if (regularSize(g, &it.bytes)) {
            it.files = 1;
            it.what = "Remembered \"Play as guest\" choice";
            p.items.push_back(it);
        }
    }
    if (std::string r = rscDir(); !r.empty()) {
        CacheItem it;
        it.path = r;
        it.what = "Downloaded game resources";
        net::RscStore::usage(r, it.files, it.bytes);
        uint64_t nsz = 0;
        if (regularSize(r + "/names", &nsz)) {
            it.files++;
            it.bytes += nsz;
        }
        if (it.files) p.items.push_back(it);
    }
    if (std::string w = webDir(); !w.empty()) {
        CacheItem it;
        it.path = w;
        it.what = "Web page storage (localStorage, IndexedDB)";
        webUsage(w, it.files, it.bytes);
        if (it.files) p.items.push_back(it);
    }
    if (std::string sd = net::defaultStateDir(); !sd.empty()) {
        CacheItem it;
        it.path = net::cauthPath(sd);
        if (regularSize(it.path, &it.bytes)) {
            it.files = 1;
            it.what = "Saved server login keys";
            p.items.push_back(it);
        }
    }
    return p;
}

std::string describeClearCache(const CachePlan& p) {
    std::string o;
    if (p.empty()) o = "There is nothing cached to remove.\n";
    for (auto& i : p.items) {
        o += "\xe2\x80\xa2 " + i.what + " \xe2\x80\x94 " + humanBytes(i.bytes);
        if (i.files > 1) o += " in " + std::to_string(i.files) + " files";
        o += "\n    " + i.path + "\n";
    }
    if (!p.empty()) o += "\nTotal: " + humanBytes(p.bytes()) + ".\n";
    o += "\nKept: your saved servers and settings (pager.json). A removed sign-in means signing in again; removed "
         "resources "
         "download again when you next join; cleared "
         "web storage means in-game web panels forget what they saved.";
    return o;
}

CachePlan clearCache(std::string* err) {
    CachePlan plan = planClearCache(), done;
    auto fail = [&](const std::string& m) {
        if (err && err->empty()) *err = m;
    };
    for (auto& it : plan.items) {
        if (it.path == rscDir()) {
            uint64_t f = 0, b = 0, nsz = 0, left = 0;
            const bool names = regularSize(it.path + "/names", &nsz);
            net::RscStore::clear(it.path, &f, &b);  // the shards and `names`, by the disk cache's clear rule
            if (names && !regularSize(it.path + "/names", &left)) {
                f++;
                b += nsz;
            }
            uint64_t lf = 0, lb = 0;
            net::RscStore::usage(it.path, lf, lb);
            if (lf) fail("could not remove " + std::to_string(lf) + " files under " + it.path);
            if (f) done.items.push_back(CacheItem{it.what, it.path, f, b});
            continue;
        }
        if (it.path == webDir()) {
            if (webStorageInUse(it.path)) {
                fail("web storage " + it.path + " is in use by a running client; close it and try again");
                continue;
            }
            uint64_t f = 0, b = 0;
            std::error_code ec;
            std::vector<std::filesystem::path> entries;
            for (std::filesystem::directory_iterator d(it.path, ec), dend; !ec && d != dend; d.increment(ec))
                if (d->path().filename() != ".lock") entries.push_back(d->path());
            for (const auto& e : entries) {
                std::error_code rm;
                std::filesystem::remove_all(e, rm);
            }
            webUsage(it.path, f, b);
            if (f) fail("could not remove " + std::to_string(f) + " files under " + it.path);
            if (f < it.files)
                done.items.push_back(CacheItem{it.what, it.path, it.files - f, it.bytes - std::min(b, it.bytes)});
            continue;
        }
        if (platform::unlink(it.path.c_str()) == 0 || errno == ENOENT)
            done.items.push_back(it);
        else
            fail("remove " + it.path + ": " + std::strerror(errno));
    }
    return done;
}

}  // namespace fugue::pager

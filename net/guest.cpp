#include "net/guest.h"
#include "platform/file.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include "net/md5.h"
#include "net/resource.h"

namespace fugue::net {
namespace {
// The 16-byte guest scaffold (credential bytes [0:16]).
const uint8_t kScaffold[16] = {0x00, 0x00, 0x7e, 0xcc, 0x5a, 0x2e, 0x89, 0x35,
                               0x73, 0xd7, 0x6d, 0x97, 0x6c, 0xb5, 0xf6, 0x00};

std::string hexLower(const Digest16& d) { return hexEncode(ByteSpan(d.data(), d.size())); }
}  // namespace

uint32_t computerId(const Digest16& d) {
    std::string h = hexLower(d);
    return resId(ByteSpan(reinterpret_cast<const uint8_t*>(h.data()), h.size()));
}

Digest16 digestForInstall(const std::string& sid) {
    for (uint32_t i = 0;; i++) {
        Digest16 d = md5("fugue-cid-v1:install:" + sid + ":" + std::to_string(i));
        if (computerId(d) % 1000 == 523) return d;
    }
}

Bytes mintGuestCredential(const Digest16& digest, const uint8_t window[4]) {
    Bytes b(kScaffold, kScaffold + 16);
    b.insert(b.end(), window, window + 4);
    b.push_back(0x7f);
    b.insert(b.end(), digest.begin(), digest.end());
    putLe32(b, computerId(digest) ^ 0x3C201960u);
    b.push_back(0x1a);
    for (int i = 0; i < 5; i++) b.push_back(0);
    return b;
}

std::string defaultStateDir() {
    auto h = platform::homeDir();
    return (h.empty() ? "." : h) + "/.fugue";
}

std::string loadOrCreateSid(const std::string& dir, std::string* err) {
    namespace fs = std::filesystem;
#ifdef _WIN32
    const fs::path p = platform::pathFromUtf8(dir) / "sid";
#else
    fs::path p = fs::path(dir) / "sid";
#endif
    auto valid = [](const std::string& s) {
        if (s.size() != 24) return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    };
    std::error_code ec;
    {
        std::ifstream f(p);
        std::string s;
        if (f && std::getline(f, s) && valid(s)) return s;
    }
    std::random_device rd;
    Bytes raw(12);
    for (auto& x : raw) x = uint8_t(rd());
    std::string sid = hexEncode(raw);
#ifdef _WIN32
    fs::create_directories(platform::pathFromUtf8(dir).parent_path(), ec);
    platform::mkdir(dir.c_str(), 0700);
    platform::privateWrite(dir + "/sid", sid + "\n", err);
    return sid;
#else
    fs::create_directories(dir, ec);
    if (!ec) fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    std::ofstream f(p, std::ios::trunc);
    if (!f) {
        if (err) *err = "cannot write " + p.string();
        return sid;  // ephemeral: still a valid identity for this process
    }
    f << sid << "\n";
    f.close();
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    return sid;
#endif
}
}  // namespace fugue::net

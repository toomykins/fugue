#include "net/cauth.h"
#include "platform/file.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <utility>
#include <vector>

namespace fugue::net {
namespace {
struct Line {
    std::string name, value;  // value is the raw "key=..;pwd=..;ctime=.." text, kept verbatim for lines we do not touch
};

std::vector<Line> readLines(const std::string& path) {
    std::vector<Line> out;
    std::ifstream f(path);
    std::string s;
    while (std::getline(f, s)) {
        if (!s.empty() && s.back() == '\r') s.pop_back();  // DreamSeeker's own file is CRLF
        size_t sp = s.find_first_of(" \t");
        if (s.empty() || s[0] == '#' || sp == std::string::npos) continue;
        size_t v = s.find_first_not_of(" \t", sp);
        out.push_back({s.substr(0, sp), v == std::string::npos ? "" : s.substr(v)});
    }
    return out;
}

// QueryParams over "a=b;c=d".
std::string param(const std::string& value, const std::string& k) {
    size_t p = 0;
    while (p <= value.size()) {
        size_t e = value.find(';', p);
        if (e == std::string::npos) e = value.size();
        std::string kv = value.substr(p, e - p);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.compare(0, eq, k) == 0) return kv.substr(eq + 1);
        p = e + 1;
    }
    return "";
}

bool hasParam(const std::string& value, const std::string& k) {
    for (size_t p = 0; p <= value.size();) {
        size_t e = value.find(';', p);
        if (e == std::string::npos) e = value.size();
        size_t eq = value.find('=', p);
        if (eq != std::string::npos && eq < e && value.compare(p, eq - p, k) == 0) return true;
        p = e + 1;
    }
    return false;
}

// strtol(s, 16) with 32-bit clamping, as the stock client does. Minted keys are below 2^31, so only a hand-edited
// line can tell the difference.
uint32_t strtol32(const std::string& s) {
    long long v = std::strtoll(s.c_str(), nullptr, 16);
    if (v > 0x7fffffffLL) v = 0x7fffffffLL;
    if (v < -0x80000000LL) v = -0x80000000LL;
    return uint32_t(int32_t(v));
}

CauthEntry parse(const std::string& value) {
    CauthEntry c;
    c.key = strtol32(param(value, "key"));
    c.pwd = param(value, "pwd");
    c.ctime = std::strtoull(param(value, "ctime").c_str(), nullptr, 16);
    return c;
}

// A 31-bit random value, redrawn until non-zero.
uint32_t draw31(std::random_device& rd) {
    for (;;)
        if (uint32_t v = uint32_t(rd()) & 0x7fffffffu) return v;
}

void urlEncode(std::string& o, std::string_view s) {
    // Host names, IPv4 literals and port digits are unreserved under any URL encoding, so they come out the same.
    static const char* hx = "0123456789ABCDEF";
    for (unsigned char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~')
            o.push_back(char(c));
        else {
            o.push_back('%');
            o.push_back(hx[c >> 4]);
            o.push_back(hx[c & 15]);
        }
    }
}
}  // namespace

std::string cauthPath(const std::string& stateDir) {
    return (std::filesystem::path(stateDir) / "cauth.txt").generic_string();
}

std::string cauthEntryName(std::string_view account, std::string_view host, std::string_view port) {
    std::string o;
    for (unsigned char c : account) {  // lowercase, [a-z0-9] only
        if (c >= 'A' && c <= 'Z') c = char(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) o.push_back(char(c));
    }
    o.push_back('@');
    urlEncode(o, host);
    if (!port.empty()) {  // ':' + port only when the URL's port string is non-null
        o.push_back(':');
        urlEncode(o, port);
    }
    return o;
}

std::string cauthEntryName(std::string_view account, std::string_view host, uint16_t port) {
    return cauthEntryName(account, host, std::to_string(port));
}

bool cauthLookup(const std::string& stateDir, const std::string& name, CauthEntry* out) {
    for (auto& l : readLines(cauthPath(stateDir)))
        if (l.name == name) {
            if (out) *out = parse(l.value);
            return true;
        }
    return false;
}

CauthCache::CauthCache(std::string stateDir, std::string name, uint64_t now)
    : dir_(std::move(stateDir)), name_(std::move(name)), now_(now) {}

uint32_t CauthCache::loginKey(bool engaged, std::string* err) {
    if (!engaged) return 0;  // not engaged: key 0, the cache is neither read nor written
    for (auto& l : readLines(cauthPath(dir_)))
        if (l.name == name_) {
            CauthEntry e = parse(l.value);
            pwd_ = e.pwd;
            if (hasParam(l.value, "key")) {
                key_ = e.key;
                return key_;
            }  // reused whatever its ctime
            break;
        }
    mint(false, err);
    return key_;
}

void CauthCache::certRequest(uint32_t* key, std::string* certStr, std::string* err) {
    if (!minted_) mint(true, err);  // a key read from the cache (or none, when not engaged) gets a fresh certStr
    if (key) *key = key_;
    if (certStr) *certStr = pwd_;
}

void CauthCache::mint(bool keepKey, std::string* err) {
    namespace fs = std::filesystem;
    std::random_device rd;
    if (!keepKey || key_ == 0) {
        key_ = draw31(rd);
        minted_ = true;
    }
    char pwd[32];
    uint32_t a = draw31(rd), b = draw31(rd), c = draw31(rd);
    std::snprintf(pwd, sizeof pwd, "03%.8x%.8x%.8x", a, b, c);
    pwd_ = pwd;

    const uint64_t now = now_ ? now_ : uint64_t(std::time(nullptr));
    const std::string path = cauthPath(dir_);
    // 32-bit unsigned compare, as the stock prune: a ctime in the future wraps and is pruned too.
    std::vector<Line> kept;
    for (auto& l : readLines(path)) {
        if (l.name == name_) continue;  // this endpoint's line is replaced
        if (uint32_t(uint32_t(now) - uint32_t(parse(l.value).ctime)) > kCauthMaxAge) continue;
        kept.push_back(l);
    }
    char val[128];
    std::snprintf(val, sizeof val, "key=%x;pwd=%s;ctime=%llx", key_, pwd, (unsigned long long)now);
    kept.push_back({name_, val});

    std::error_code ec;
    fs::create_directories(dir_, ec);
#ifdef _WIN32
    std::string data;
    for (auto& l : kept) data += l.name + ' ' + l.value + '\n';
    platform::privateWrite(path, data, err);
#else
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (err) *err = "cannot write " + tmp;
            return;
        }
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        for (auto& l : kept) f << l.name << ' ' << l.value << '\n';
        if (!f.flush()) {
            if (err) *err = "cannot write " + tmp;
            return;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec && err) *err = "cannot replace " + path + ": " + ec.message();
#endif
}

uint32_t cauthKeyFor(const std::string& stateDir, const std::string& name, std::string* err, uint64_t now) {
    return CauthCache(stateDir, name, now).loginKey(true, err);
}

}  // namespace fugue::net

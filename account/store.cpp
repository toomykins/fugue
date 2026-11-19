#include "account/store.h"
#include "fugue_env.h"
#include "platform/file.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include "account/keyblock.h"
#include "account/records.h"

namespace fugue::account {
using fugue::Bytes;

namespace {
void setErr(std::string* e, std::string m) {
    if (e) *e = std::move(m);
}
std::string env(const char* n) {
    const char* v = std::getenv(n);
    return v ? v : "";
}
std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}
std::string dirOf(const std::string& p) {
    auto i = p.find_last_of("/\\");
    if (i == std::string::npos) return ".";
    return i == 0 ? "/" : p.substr(0, i);
}
bool isFile(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}
bool readFile(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
bool mkdirP(const std::string& d, std::string* err) {
    if (d.empty() || d == "." || d == "/") return true;
    struct stat st;
    if (::stat(d.c_str(), &st) == 0) return S_ISDIR(st.st_mode) ? true : (setErr(err, d + ": not a directory"), false);
    if (!mkdirP(dirOf(d), err)) return false;
    if (platform::mkdir(d.c_str(), 0700) != 0 && errno != EEXIST) {
        setErr(err, "mkdir " + d + ": " + std::strerror(errno));
        return false;
    }
    return true;
}
std::string trimWs(std::string s) {
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (!s.empty() && ws(s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && ws(s[i])) i++;
    return s.substr(i);
}
std::vector<std::string> fields(const std::string& s) {
    std::vector<std::string> o;
    std::istringstream is(s);
    std::string w;
    while (is >> w) o.push_back(w);
    return o;
}
std::vector<std::string> globPaths(const std::string& pat) {
    std::vector<std::string> o;
    size_t star = pat.find('*');
    if (star == std::string::npos) {
        if (QFileInfo(QString::fromStdString(pat)).isDir()) o.push_back(pat);
        return o;
    }
    size_t start = pat.rfind('/', star), end = pat.find('/', star);
    QDir dir(QString::fromStdString(start == std::string::npos ? "." : pat.substr(0, start + 1)));
    std::string pattern =
        pat.substr(start == std::string::npos ? 0 : start + 1, end == std::string::npos ? end : end - start - 1);
    for (const QString& name :
         dir.entryList({QString::fromStdString(pattern)}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        auto matches = globPaths(dir.filePath(name).toStdString() + (end == std::string::npos ? "" : pat.substr(end)));
        o.insert(o.end(), matches.begin(), matches.end());
    }
    return o;
}
bool eqFold(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
}  // namespace

std::optional<std::string> homeDir() {
    std::string h = platform::homeDir();
    if (h.empty()) return std::nullopt;
    return h;
}
std::string fugueDir(std::string* err) {
    auto h = homeDir();
    if (!h) {
        setErr(err, "$HOME is not defined");
        return {};
    }
    return join(*h, ".fugue");
}
std::string qtStateDir() {
    auto h = homeDir();
    return h ? join(join(*h, ".fugue"), "qt") : std::string();
}

bool atomicWrite(const std::string& path, const std::string& data, std::string* err) {
    if (!mkdirP(dirOf(path), err)) return false;
#ifdef _WIN32
    return platform::privateWrite(path, data, err);
#else
    std::string tmp = join(dirOf(path), ".tmp-XXXXXX");
    std::vector<char> t(tmp.begin(), tmp.end());
    t.push_back(0);
    int fd = ::mkstemp(t.data());  // created 0600
    if (fd < 0) {
        setErr(err, "mkstemp " + tmp + ": " + std::strerror(errno));
        return false;
    }
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            setErr(err, std::string("write: ") + std::strerror(errno));
            ::close(fd);
            ::unlink(t.data());
            return false;
        }
        off += size_t(n);
    }
    ::fchmod(fd, 0600);
    ::fsync(fd);
    ::close(fd);
    if (::rename(t.data(), path.c_str()) != 0) {
        setErr(err, std::string("rename: ") + std::strerror(errno));
        ::unlink(t.data());
        return false;
    }
    return true;
#endif
}

// ---------------- identity ----------------
std::string identityPath(const std::string& account, std::string* err) {
    std::string p = fugueEnvOr("IDENTITY");
    if (!p.empty()) return p;
    std::string d = fugueDir(err);
    return d.empty() ? std::string() : join(d, "identity-" + ckey(account));
}

std::string formatIdentityFile(const Account& a) {
    // install_id and inner_key are written as 0x + 8 hex digits.
    auto h = [](uint32_t v) {
        char b[16];
        std::snprintf(b, sizeof b, "0x%08x", v);
        return std::string(b);
    };
    return "identity=" + hexEncode(a.identity) + "\ninstall_id=" + h(a.installId) + "\ninner_key=" + h(a.innerKey) +
           "\n";
}

bool saveAccount(const std::string& account, const Account& a, std::string* err) {
    if (a.identity.empty()) {
        setErr(err, "identity is empty");
        return false;
    }
    if (a.installId == 0 || a.innerKey == 0) {
        setErr(err, "hub credentials are zero");
        return false;
    }
    std::string p = identityPath(account, err);
    if (p.empty()) return false;
    return atomicWrite(p, formatIdentityFile(a), err);
}

static bool parseUint32(const std::string& v, uint32_t& out) {
    // Base-0 parse: 0x / 0o / 0b prefixes, leading 0 for octal, else decimal.
    if (v.empty()) return false;
    int base = 10;
    size_t i = 0;
    if (v.size() > 1 && v[0] == '0') {
        char c = char(std::tolower((unsigned char)v[1]));
        if (c == 'x') {
            base = 16;
            i = 2;
        } else if (c == 'b') {
            base = 2;
            i = 2;
        } else if (c == 'o') {
            base = 8;
            i = 2;
        } else {
            base = 8;
            i = 1;
        }
    }
    if (i >= v.size()) return false;
    uint64_t n = 0;
    for (; i < v.size(); i++) {
        int d;
        char c = v[i];
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return false;
        if (d >= base) return false;
        n = n * uint64_t(base) + uint64_t(d);
        if (n > 0xffffffffULL) return false;
    }
    out = uint32_t(n);
    return true;
}

std::optional<Account> loadAccount(const std::string& account, std::string* err) {
    std::string path = identityPath(account, err);
    if (path.empty()) return std::nullopt;
    std::string raw;
    if (!readFile(path, raw)) {
        setErr(err, "open " + path + ": no such file or unreadable");
        return std::nullopt;
    }
    std::string body = trimWs(raw);
    if (body.find('=') == std::string::npos) {
        setErr(err, path + " is from an older login and carries no hub credentials");
        return std::nullopt;
    }
    Account a;
    std::istringstream is(body);
    std::string ln;
    while (std::getline(is, ln)) {
        ln = trimWs(ln);
        auto eq = ln.find('=');
        if (eq == std::string::npos) continue;
        std::string k = ln.substr(0, eq), v = ln.substr(eq + 1);
        if (k == "identity") {
            auto id = hexDecode(v);
            if (!id) {
                setErr(err, path + ": bad identity hex");
                return std::nullopt;
            }
            a.identity = *id;
        } else if (k == "install_id" || k == "inner_key") {
            uint32_t n;
            if (!parseUint32(v, n)) {
                setErr(err, path + ": bad " + k);
                return std::nullopt;
            }
            (k == "install_id" ? a.installId : a.innerKey) = n;
        }
    }
    if (a.identity.empty() || a.installId == 0 || a.innerKey == 0) {
        setErr(err, path + " is incomplete");
        return std::nullopt;
    }
    return a;
}

// ---------------- sids ----------------
std::optional<std::string> byondConfigSid() {
    auto h = homeDir();
    if (!h) return std::nullopt;
    for (const std::string& p : {join(*h, ".byond/cfg/byond.txt"), join(*h, "Documents/BYOND/cfg/byond.txt")}) {
        std::string raw;
        if (!readFile(p, raw)) continue;
        std::istringstream is(raw);
        std::string ln;
        while (std::getline(is, ln)) {
            auto f = fields(ln);
            if (f.size() == 2 && f[0] == "sid" && f[1].size() == 24) return f[1];
        }
    }
    return std::nullopt;
}

static std::string randomSid() {
    std::random_device rd;
    Bytes b(12);
    for (auto& x : b) x = uint8_t(rd());
    return hexEncode(b);
}

std::optional<std::string> pagerSid(std::string* err) {
    std::string path = fugueEnvOr("BYOND_SID");
    if (path.empty()) {
        std::string d = fugueDir(err);
        if (d.empty()) return std::nullopt;
        path = join(d, "byond-sid");
    }
    if (auto s = byondConfigSid()) {
        if (!atomicWrite(path, *s + "\n", err)) return std::nullopt;
        return s;
    }
    std::string raw;
    if (readFile(path, raw)) {
        std::string s = trimWs(raw);
        if (s.size() == 24) return s;
    }
    std::string s = randomSid();
    if (!atomicWrite(path, s + "\n", err)) return std::nullopt;
    return s;
}

std::string installSidPath() {
    std::string x = env("XDG_STATE_HOME");
    if (!x.empty()) return join(join(x, "fugue"), "sid");
    auto h = homeDir();
    return h ? join(join(*h, ".fugue"), "sid") : std::string();
}
std::optional<std::string> installSid(std::string* err) {
    std::string path = installSidPath();
    if (path.empty()) {
        setErr(err, "$HOME is not defined");
        return std::nullopt;
    }
    std::string raw;
    if (readFile(path, raw)) {
        std::string s = trimWs(raw);
        if (s.size() == 24) return s;
    }
    std::string s = randomSid();
    if (!atomicWrite(path, s + "\n", err)) return std::nullopt;
    return s;
}

// ---------------- time ----------------
static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int64_t(doe) - 719468;
}
static void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = int64_t(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

Timestamp Timestamp::now() {
    auto t = std::chrono::system_clock::now().time_since_epoch();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t).count();
    Timestamp r;
    r.sec = ns / 1000000000LL;
    r.nsec = int32_t(ns % 1000000000LL);
    return r;
}

std::optional<Timestamp> parseRfc3339(const std::string& s) {
    auto dig = [&](size_t i, size_t n, int& out) {
        if (i + n > s.size()) return false;
        out = 0;
        for (size_t k = 0; k < n; k++) {
            if (s[i + k] < '0' || s[i + k] > '9') return false;
            out = out * 10 + (s[i + k] - '0');
        }
        return true;
    };
    int Y, M, D, h, mi, se;
    if (s.size() < 20 || !dig(0, 4, Y) || s[4] != '-' || !dig(5, 2, M) || s[7] != '-' || !dig(8, 2, D) ||
        s[10] != 'T' || !dig(11, 2, h) || s[13] != ':' || !dig(14, 2, mi) || s[16] != ':' || !dig(17, 2, se))
        return std::nullopt;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || se > 59) return std::nullopt;
    size_t i = 19;
    int32_t nsec = 0;
    if (s[i] == '.') {
        i++;
        int digits = 0;
        int64_t f = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (digits < 9) {
                f = f * 10 + (s[i] - '0');
                digits++;
            }
            i++;
        }
        if (digits == 0) return std::nullopt;
        for (; digits < 9; digits++) f *= 10;
        nsec = int32_t(f);
    }
    int64_t off = 0;
    if (i < s.size() && s[i] == 'Z' && i + 1 == s.size()) {
    } else if (i + 6 == s.size() && (s[i] == '+' || s[i] == '-') && s[i + 3] == ':') {
        int oh, om;
        if (!dig(i + 1, 2, oh) || !dig(i + 4, 2, om)) return std::nullopt;
        off = (oh * 3600 + om * 60) * (s[i] == '-' ? -1 : 1);
    } else {
        return std::nullopt;
    }
    Timestamp t;
    t.sec = daysFromCivil(Y, unsigned(M), unsigned(D)) * 86400 + h * 3600 + mi * 60 + se - off;
    t.nsec = nsec;
    return t;
}

std::string formatRfc3339(const Timestamp& t) {
    int64_t days = t.sec >= 0 ? t.sec / 86400 : -((-t.sec + 86399) / 86400);
    int64_t rem = t.sec - days * 86400;
    int64_t y;
    unsigned m, d;
    civilFromDays(days, y, m, d);
    char b[64];
    std::snprintf(b, sizeof b, "%04lld-%02u-%02uT%02lld:%02lld:%02lld", (long long)y, m, d, (long long)(rem / 3600),
                  (long long)(rem % 3600 / 60), (long long)(rem % 60));
    std::string o = b;
    if (t.nsec) {
        char f[16];
        std::snprintf(f, sizeof f, ".%09d", t.nsec);
        std::string fs = f;
        while (fs.back() == '0') fs.pop_back();
        o += fs;
    }
    return o + "Z";
}

// ---------------- pager.json ----------------
std::string configPath(std::string* err) {
    std::string p = fugueEnvOr("PAGER_CONFIG");
    if (!p.empty()) return p;
    std::string d = fugueDir(err);
    return d.empty() ? std::string() : join(d, "pager.json");
}

std::vector<Server> defaultServers() {
    Server a;
    a.name = "/vg/station (ss13.moe)";
    a.addr = "game.ss13.moe:7777";
    a.hub = true;
    Server b;
    b.name = "local server";
    b.addr = "127.0.0.1:6000";
    return {a, b};
}

int Config::find(const std::string& addr) const {
    for (size_t i = 0; i < servers.size(); i++)
        if (eqFold(servers[i].addr, addr)) return int(i);
    return -1;
}
void Config::remember(Server s) {
    s.lastPlayed = Timestamp::now();
    int i = find(s.addr);
    if (i >= 0) {
        const Server& old = servers[size_t(i)];
        s.name = old.name;
        if (s.flags.empty()) s.flags = old.flags;
        if (s.lastPrefetch.isZero()) s.lastPrefetch = old.lastPrefetch;
        // Keep unknown keys: a whole-record replace would drop them.
        if (s.extra.isEmpty()) s.extra = old.extra;
        servers[size_t(i)] = s;
        return;
    }
    if (s.name.empty()) s.name = s.addr;
    servers.push_back(s);
}
void Config::markPrefetched(Server s) {
    int i = find(s.addr);
    if (i >= 0) {
        servers[size_t(i)].lastPrefetch = Timestamp::now();
        return;
    }
    s.lastPrefetch = Timestamp::now();
    servers.push_back(s);
}
void Config::clearPrefetched() {
    for (auto& s : servers) s.lastPrefetch = Timestamp();
}
void Config::remove(const std::string& addr) {
    int i = find(addr);
    if (i >= 0) servers.erase(servers.begin() + i);
}
static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::vector<Server> Config::sorted() const {
    auto out = servers;
    std::stable_sort(out.begin(), out.end(), [](const Server& a, const Server& b) {
        if (!(a.lastPlayed == b.lastPlayed))
            return a.lastPlayed.sec != b.lastPlayed.sec ? a.lastPlayed.sec > b.lastPlayed.sec
                                                        : a.lastPlayed.nsec > b.lastPlayed.nsec;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

namespace {
const char* const kSrvKnown[] = {"name", "addr", "hub", "flags", "last_played", "last_prefetch"};
const char* const kCfgKnown[] = {"account", "keytxt", "client", "servers"};
template <size_t N>
bool known(const char* const (&arr)[N], const QString& k) {
    for (auto s : arr)
        if (k == s) return true;
    return false;
}
bool parseTime(const QJsonValue& v, Timestamp& t, std::string* err, const char* what) {
    if (v.isUndefined() || v.isNull()) {
        t = Timestamp();
        return true;
    }
    if (!v.isString()) {
        setErr(err, std::string(what) + ": not a string");
        return false;
    }
    auto p = parseRfc3339(v.toString().toStdString());
    if (!p) {
        setErr(err, std::string(what) + ": bad RFC3339 time");
        return false;
    }
    t = *p;
    return true;
}
}  // namespace

std::optional<Config> loadConfig(std::string* err) {
    std::string path = configPath(err);
    if (path.empty()) return std::nullopt;
    std::string raw;
    if (!isFile(path) || !readFile(path, raw)) {
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 && errno == ENOENT) {
            Config c;
            c.servers = defaultServers();
            return c;
        }
        setErr(err, "open " + path + ": " + std::strerror(errno));
        return std::nullopt;
    }
    QJsonParseError pe;
    auto doc = QJsonDocument::fromJson(QByteArray::fromStdString(raw), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        setErr(err, path + ": " +
                        (pe.error != QJsonParseError::NoError ? pe.errorString().toStdString() : "not a JSON object"));
        return std::nullopt;
    }
    QJsonObject o = doc.object();
    Config c;
    auto str = [&](const char* k, std::string& dst) {
        auto v = o.value(k);
        if (v.isUndefined() || v.isNull()) return true;
        if (!v.isString()) {
            setErr(err, path + ": " + k + " is not a string");
            return false;
        }
        dst = v.toString().toStdString();
        return true;
    };
    if (!str("account", c.account) || !str("keytxt", c.keytxt) || !str("client", c.client)) return std::nullopt;
    auto sv = o.value("servers");
    if (!sv.isUndefined() && !sv.isNull()) {
        if (!sv.isArray()) {
            setErr(err, path + ": servers is not an array");
            return std::nullopt;
        }
        for (const auto& e : sv.toArray()) {
            if (!e.isObject()) {
                setErr(err, path + ": server entry is not an object");
                return std::nullopt;
            }
            QJsonObject so = e.toObject();
            Server s;
            auto sstr = [&](const char* k, std::string& dst) {
                auto v = so.value(k);
                if (v.isUndefined() || v.isNull()) return true;
                if (!v.isString()) {
                    setErr(err, path + ": server." + k + " is not a string");
                    return false;
                }
                dst = v.toString().toStdString();
                return true;
            };
            if (!sstr("name", s.name) || !sstr("addr", s.addr)) return std::nullopt;
            auto hv = so.value("hub");
            if (!hv.isUndefined() && !hv.isNull()) {
                if (!hv.isBool()) {
                    setErr(err, path + ": server.hub is not a bool");
                    return std::nullopt;
                }
                s.hub = hv.toBool();
            }
            auto fv = so.value("flags");
            if (!fv.isUndefined() && !fv.isNull()) {
                if (!fv.isArray()) {
                    setErr(err, path + ": server.flags is not an array");
                    return std::nullopt;
                }
                for (const auto& f : fv.toArray()) {
                    if (!f.isString()) {
                        setErr(err, path + ": server.flags entry is not a string");
                        return std::nullopt;
                    }
                    s.flags.push_back(f.toString().toStdString());
                }
            }
            if (!parseTime(so.value("last_played"), s.lastPlayed, err, "last_played") ||
                !parseTime(so.value("last_prefetch"), s.lastPrefetch, err, "last_prefetch"))
                return std::nullopt;
            for (auto it = so.begin(); it != so.end(); ++it)
                if (!known(kSrvKnown, it.key())) s.extra.insert(it.key(), it.value());
            c.servers.push_back(std::move(s));
        }
    }
    for (auto it = o.begin(); it != o.end(); ++it)
        if (!known(kCfgKnown, it.key())) c.extra.insert(it.key(), it.value());
    if (c.servers.empty()) c.servers = defaultServers();
    return c;
}

namespace {
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else
                    o.push_back(char(c));
        }
    }
    return o + "\"";
}
void writeValue(std::string& o, const QJsonValue& v, int ind);
void writeObj(std::string& o, const QJsonObject& obj, int ind) {
    if (obj.isEmpty()) {
        o += "{}";
        return;
    }
    o += "{\n";
    QStringList keys = obj.keys();  // QJsonObject keeps keys sorted
    for (int i = 0; i < keys.size(); i++) {
        o += std::string(size_t(ind + 1) * 2, ' ') + jstr(keys[i].toStdString()) + ": ";
        writeValue(o, obj.value(keys[i]), ind + 1);
        o += i + 1 < keys.size() ? ",\n" : "\n";
    }
    o += std::string(size_t(ind) * 2, ' ') + "}";
}
void writeValue(std::string& o, const QJsonValue& v, int ind) {
    switch (v.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined: o += "null"; break;
        case QJsonValue::Bool: o += v.toBool() ? "true" : "false"; break;
        case QJsonValue::Double: {
            double d = v.toDouble();
            char b[40];
            if (d == double(int64_t(d)) && d > -9e15 && d < 9e15)
                std::snprintf(b, sizeof b, "%lld", (long long)d);
            else
                std::snprintf(b, sizeof b, "%.17g", d);
            o += b;
            break;
        }
        case QJsonValue::String: o += jstr(v.toString().toStdString()); break;
        case QJsonValue::Array: {
            QJsonArray a = v.toArray();
            if (a.isEmpty()) {
                o += "[]";
                break;
            }
            o += "[\n";
            for (int i = 0; i < a.size(); i++) {
                o += std::string(size_t(ind + 1) * 2, ' ');
                writeValue(o, a[i], ind + 1);
                o += i + 1 < a.size() ? ",\n" : "\n";
            }
            o += std::string(size_t(ind) * 2, ' ') + "]";
            break;
        }
        case QJsonValue::Object: writeObj(o, v.toObject(), ind); break;
    }
}
}  // namespace

std::string serializeConfig(const Config& c) {
    // Known fields in struct order, then unknown keys alphabetically.
    std::vector<std::pair<std::string, std::string>> top;
    if (!c.account.empty()) top.push_back({"account", jstr(c.account)});
    if (!c.keytxt.empty()) top.push_back({"keytxt", jstr(c.keytxt)});
    if (!c.client.empty()) top.push_back({"client", jstr(c.client)});
    std::string sv;
    if (c.servers.empty())
        sv = "[]";
    else {
        sv = "[\n";
        for (size_t i = 0; i < c.servers.size(); i++) {
            const Server& s = c.servers[i];
            std::vector<std::pair<std::string, std::string>> f;
            f.push_back({"name", jstr(s.name)});
            f.push_back({"addr", jstr(s.addr)});
            f.push_back({"hub", s.hub ? "true" : "false"});
            if (!s.flags.empty()) {
                std::string fl = "[\n";
                for (size_t k = 0; k < s.flags.size(); k++)
                    fl += "        " + jstr(s.flags[k]) + (k + 1 < s.flags.size() ? ",\n" : "\n");
                f.push_back({"flags", fl + "      ]"});
            }
            f.push_back({"last_played", jstr(formatRfc3339(s.lastPlayed))});
            f.push_back({"last_prefetch", jstr(formatRfc3339(s.lastPrefetch))});
            for (const QString& k : s.extra.keys()) {
                std::string vv;
                writeValue(vv, s.extra.value(k), 3);
                f.push_back({k.toStdString(), vv});
            }
            sv += "    {\n";
            for (size_t k = 0; k < f.size(); k++)
                sv += "      " + jstr(f[k].first) + ": " + f[k].second + (k + 1 < f.size() ? ",\n" : "\n");
            sv += std::string("    }") + (i + 1 < c.servers.size() ? ",\n" : "\n");
        }
        sv += "  ]";
    }
    top.push_back({"servers", sv});
    for (const QString& k : c.extra.keys()) {
        std::string vv;
        writeValue(vv, c.extra.value(k), 1);
        top.push_back({k.toStdString(), vv});
    }
    std::string o = "{\n";
    for (size_t i = 0; i < top.size(); i++)
        o += "  " + jstr(top[i].first) + ": " + top[i].second + (i + 1 < top.size() ? ",\n" : "\n");
    return o + "}\n";
}

bool saveConfig(const Config& c, std::string* err) {
    std::string path = configPath(err);
    if (path.empty()) return false;
    return atomicWrite(path, serializeConfig(c), err);
}

// ---------------- key.txt discovery ----------------
std::vector<std::string> keyTxtCandidates(const std::string& explicitPath) {
    std::vector<std::string> out;
    for (const std::string& c : {explicitPath, fugueEnvOr("KEYTXT")})
        if (!c.empty()) out.push_back(c);
    auto h = homeDir();
    if (!h) return out;
    out.push_back(join(fugueDir(nullptr), "key.txt"));  // Fugue's own first, then BYOND's
    out.push_back(join(*h, ".byond/cfg/key.txt"));
    out.push_back(join(*h, ".byond/key.txt"));
    out.push_back(join(*h, "Documents/BYOND/cfg/key.txt"));
    out.push_back(join(*h, "Documents/BYOND/key.txt"));
    for (const std::string& pat :
         {join(*h, ".wine/drive_c/users/*/Documents/BYOND"), join(*h, "Games/*/drive_c/users/*/Documents/BYOND"),
          join(*h, ".local/share/wineprefixes/*/drive_c/users/*/Documents/BYOND")})
        for (const auto& d : globPaths(pat)) {
            out.push_back(join(d, "cfg/key.txt"));
            out.push_back(join(d, "key.txt"));
        }
    return out;
}

std::optional<std::string> resolveKeyTxt(const std::string& explicitPath, const std::string& account,
                                         std::string* err) {
    auto cands = keyTxtCandidates(explicitPath);
    std::vector<std::string> existing;
    for (const auto& c : cands) {
        if (!isFile(c)) continue;
        existing.push_back(c);
        if (account.empty()) continue;
        std::string raw;
        if (!readFile(c, raw)) continue;
        for (const auto& n : keyNames(raw))
            if (eqFold(n, account)) return c;
    }
    if (!existing.empty()) return existing[0];
    std::string tried;
    for (size_t i = 0; i < cands.size(); i++) tried += (i ? ", " : "") + cands[i];
    setErr(err, "no key.txt found (tried: " + tried + ")");
    return std::nullopt;
}

std::string keyTxtWithBlock(const std::string& existing, const std::string& block) {
    std::string name;
    auto bi = block.find("BEGIN KEY ");
    if (bi != std::string::npos) {
        std::string rest = block.substr(bi + 10);
        auto j = rest.find_first_of("\r\n");
        if (j != std::string::npos) name = trimWs(rest.substr(0, j));
    }
    std::vector<std::string> lines, out;
    {
        size_t st = 0;
        for (size_t i = 0; i <= existing.size(); i++)
            if (i == existing.size() || existing[i] == '\n') {
                lines.push_back(existing.substr(st, i - st));
                st = i + 1;
            }
    }
    for (size_t i = 0; i < lines.size(); i++) {
        if (!name.empty() && trimWs(lines[i]) == "BEGIN KEY " + name) {
            while (i < lines.size() && trimWs(lines[i]) != "END KEY") i++;
            continue;
        }
        out.push_back(lines[i]);
    }
    std::string body;
    for (size_t i = 0; i < out.size(); i++) body += out[i] + (i + 1 < out.size() ? "\n" : "");
    while (!body.empty() && body.back() == '\n') body.pop_back();
    if (!body.empty()) body += "\n";
    std::string blk = block;
    while (!blk.empty() && blk.back() == '\n') blk.pop_back();
    return body + blk + "\n";
}

}  // namespace fugue::account

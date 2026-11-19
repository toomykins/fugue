#include "fugue_env.h"
#include "pager/model.h"
#include "platform/file.h"
#include <filesystem>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include "account/keyblock.h"
#include "pager/text.h"

namespace fugue::pager {
namespace {
bool eqFold(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
bool isRegular(const std::string& p, bool needExec = false) {
    struct stat st;
    if (::stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
#ifdef _WIN32
    return true;  // Windows has no Unix executable permission bits.
#else
    return !needExec || (S_ISREG(st.st_mode) && (st.st_mode & 0111));
#endif
}
std::string getenvStr(const char* k) {
    const char* v = std::getenv(k);
    return v ? v : "";
}
std::string slurp(const std::string& p) {
    std::string o;
    if (FILE* f = std::fopen(p.c_str(), "rb")) {
        char b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof b, f)) > 0) o.append(b, n);
        std::fclose(f);
    }
    return o;
}
std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string o;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) o += sep;
        o += v[i];
    }
    return o;
}
const char* kNoKeyTail = " \xe2\x80\x94 run `fugue-qt pager -login` once, or point -keytxt / $FUGUE_KEYTXT at a copy";

// Finds the client's argv. Returns the argv or an error.
bool resolveClient(const std::string& explicitIn, const CheckOptions& opt, std::vector<std::string>* argv,
                   std::string* err) {
    std::vector<std::string> tried;
    std::string ex = explicitIn.empty() ? fugueEnvOr("CLIENT") : explicitIn;
    if (!ex.empty()) {
        if (isRegular(ex)) {
            *argv = {ex};
            return true;
        }
        tried.push_back(ex);
    }
    if (opt.selfIsClient && !opt.selfExe.empty()) {
        *argv = {opt.selfExe, "client"};
        return true;
    }
    if (!opt.selfExe.empty()) {
        std::string dir =
            opt.selfExe.substr(0, opt.selfExe.rfind('/') == std::string::npos ? 0 : opt.selfExe.rfind('/'));
        std::string cand = (dir.empty() ? std::string(".") : dir) + "/fugue";
        tried.push_back(cand);
        if (isRegular(cand)) {
            *argv = {cand};
            return true;
        }
    }
#ifdef _WIN32
    constexpr char separator = ';';
    constexpr const char* binary = "/fugue.exe";
#else
    constexpr char separator = ':';
    constexpr const char* binary = "/fugue";
#endif
    std::string path = getenvStr("PATH");
    for (size_t st = 0; st <= path.size();) {
        size_t e = path.find(separator, st);
        std::string d = path.substr(st, e == std::string::npos ? std::string::npos : e - st);
        if (!d.empty() && isRegular(d + binary, true)) {
            *argv = {d + binary};
            return true;
        }
        if (e == std::string::npos) break;
        st = e + 1;
    }
    tried.push_back("fugue on $PATH");
    *err = "no fugue-qt client binary found (tried: " + join(tried, ", ") +
           ") \xe2\x80\x94 install fugue-qt next to the pager, or point $FUGUE_CLIENT at it";
    return false;
}

bool isHex8(const std::string& n) {
    if (n.size() != 8) return false;
    for (char c : n)
        if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}
}  // namespace

bool resolveClientArgv(const std::string& explicitIn, const CheckOptions& opt, std::vector<std::string>* argv,
                       std::string* err) {
    return resolveClient(explicitIn, opt, argv, err);
}

bool Status::hasKey() const {
    for (auto& k : keys)
        if (eqFold(k, account)) return true;
    return false;
}

Status check(const account::Config& cfg, const CheckOptions& opt) {
    Status st;
    st.account = cfg.account;
    std::string err;
    if (auto p = account::resolveKeyTxt(cfg.keytxt, cfg.account, &err)) {
        st.keyTxtPath = *p;
        std::string raw = slurp(*p);
        st.keys = account::keyNames(raw);
    } else {
        st.keyTxtErr = err + kNoKeyTail;
    }
    if (!cfg.account.empty()) {
        std::string e2;
        std::string p = account::identityPath(cfg.account, &e2);
        if (!p.empty()) {
            st.identityPath = p;
            st.hasIdentity = account::loadAccount(cfg.account).has_value();
        }
    }
    std::vector<std::string> argv;
    if (resolveClient(cfg.client, opt, &argv, &err))
        st.clientDesc = join(argv, " ");
    else
        st.clientErr = err;
    return st;
}

std::optional<std::string> blocker(const Status& st, const account::Server& srv) {
    if (st.clientErr) return st.clientErr;
    // Guests are never refused here: whether a world takes guests is the world's own setting, and the server refuses.
    if (st.account.empty()) return std::nullopt;
    if (st.keyTxtErr) return st.keyTxtErr;
    if (!st.hasIdentity)
        return "no account identity for " + st.account +
               " on this box \xe2\x80\x94 run `fugue-qt pager -login` (or press \"Sign in\"); it is a one-time step "
               "per account per machine. "
               "A key.txt block alone is NOT enough: it is encrypted under the account's own password hash";
    return std::nullopt;
}

std::optional<std::string> plainBlocker(const Status& st, const account::Server& srv) {
    if (!blocker(st, srv)) return std::nullopt;
    if (st.clientErr) return "Fugue's game window is missing from this install. Reinstall Fugue.";
    return "Sign in again to play as " + st.account + ".";
}

std::vector<std::string> checklist(const Status& st) {
    std::vector<std::string> out;
    if (st.keyTxtErr)
        out.push_back("\xe2\x9c\x97 key.txt: " + *st.keyTxtErr + "  \xe2\x86\x92 run `fugue-qt pager -login`");
    else if (st.account.empty())
        out.push_back("\xc2\xb7 key.txt: " + st.keyTxtPath + " (keys: " + join(st.keys, ", ") +
                      ") \xe2\x80\x94 unused while playing as a guest");
    else if (st.hasKey())
        out.push_back("\xe2\x9c\x93 key.txt: " + st.keyTxtPath + " holds a block for " + st.account);
    else
        out.push_back("\xe2\x9c\x97 key.txt: " + st.keyTxtPath + " has no block for " + st.account +
                      " (it has: " + join(st.keys, ", ") + ")  \xe2\x86\x92 run `fugue-qt pager -login`");
    if (st.account.empty()) {
    } else if (st.hasIdentity)
        out.push_back("\xe2\x9c\x93 identity: " + st.identityPath + " (this is what logs you in)");
    else
        out.push_back("\xe2\x9c\x97 identity: none for " + st.account + "  \xe2\x86\x92 run `fugue-qt pager -login`");
    if (st.clientErr)
        out.push_back("\xe2\x9c\x97 client: " + *st.clientErr);
    else
        out.push_back("\xe2\x9c\x93 client: " + st.clientDesc);
    return out;
}

std::string windowWording(std::string l) {
    static const std::pair<std::string, std::string> kSay[] = {
        {kNoKeyTail,
         " \xe2\x80\x94 Sign in once with your BYOND account and Fugue writes it, or point -keytxt / $FUGUE_KEYTXT at "
         "a copy"},
        {"  \xe2\x86\x92 run `fugue-qt pager -login`", "  \xe2\x86\x92 Sign in"},
        {"run `fugue-qt pager -login` (or press \"Sign in\")", "Sign in with this account"},
    };
    for (const auto& [from, to] : kSay)
        for (size_t at; (at = l.find(from)) != std::string::npos;) l.replace(at, from.size(), to);
    return l;
}

std::vector<std::string> windowChecklist(const Status& st) {
    std::vector<std::string> out = checklist(st);
    for (auto& l : out) l = windowWording(l);
    return out;
}

std::string rscDir() {
    auto h = account::homeDir();
    return h ? *h + "/.fugue/rsc" : std::string();
}

CacheStats scanRscCache(const std::string& dir) {
    CacheStats cs;
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator sh(dir, ec), end; !ec && sh != end; sh.increment(ec)) {
        std::error_code e2;
        if (sh->path().filename().string().size() != 2 || !sh->is_directory(e2)) continue;
        for (fs::directory_iterator f(sh->path(), e2), fend; !e2 && f != fend; f.increment(e2)) {
            if (!isHex8(f->path().filename().string())) continue;
            std::error_code e3;
            auto size = f->file_size(e3);
            if (e3) continue;
            cs.files++;
            cs.bytes += size;
        }
    }
    return cs;
}

namespace {
std::string fmtDayMonth(int64_t sec) {  // day and month, e.g. "2 Jan", in UTC
    int64_t days = sec >= 0 ? sec / 86400 : -((-sec + 86399) / 86400);
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    static const char* mn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return std::to_string(d) + " " + mn[m - 1];
}
}  // namespace

std::string renderList(const account::Config& cfg, const Status& st) {
    std::string o = "account:  " + (cfg.account.empty() ? std::string("(guest)") : cfg.account) + "\n";
    for (auto& ln : checklist(st)) o += "  " + ln + "\n";
    o += "servers:\n";
    for (auto& s : cfg.sorted()) {
        std::string tag;
        if (s.hub) tag = "  [hub]";
        if (s.lastPrefetch.isZero())
            tag += "  [not warmed up]";
        else
            tag += "  [warm " + fmtDayMonth(s.lastPrefetch.sec) + "]";
        std::string blocked;
        if (auto b = blocker(st, s)) blocked = "\n      \xe2\x9a\xa0 " + *b;
        o += "  " + padRight(s.name, 28) + " " + s.addr + tag + blocked + "\n";
    }
    std::string err;
    std::string cp = account::configPath(&err);
    if (!cp.empty()) o += "config:   " + cp + "\n";
    std::string dir = rscDir();
    if (!dir.empty()) {
        CacheStats cs = scanRscCache(dir);
        char buf[256];
        std::snprintf(buf, sizeof buf, "rsc cache: %lld %s, %.1f MiB of %.0f MiB cap", cs.files,
                      cs.files == 1 ? "file" : "files", double(cs.bytes) / (1 << 20), double(cs.capBytes) / (1 << 20));
        o += "resources: " + dir + "\n           " + buf + "\n";
    }
    return o;
}

std::vector<LiveRow> liveRows(const std::vector<LiveWorld>& worlds) {
    std::vector<LiveRow> out;
    for (auto& w : worlds) {
        LiveRow r;
        r.title = w.title();
        auto l = w.lines();
        for (size_t i = 1; i < l.size(); i++) r.detail += (i > 1 ? " \xc2\xb7 " : "") + l[i];
        r.id = w.id;
        r.players = int(w.players.size());
        r.version = w.serverVersion;
        out.push_back(std::move(r));
    }
    return out;
}

bool parseWarmLine(const std::string& line, WarmProgress& out) {
    unsigned long long a = 0, b = 0, c = 0, d = 0;
    if (size_t at = line.find("prefetch: ");
        at != std::string::npos &&
        std::sscanf(line.c_str() + at, "prefetch: %llu/%llu resolved, %llu cached, %llu outstanding", &a, &b, &c, &d) ==
            4) {
        out = WarmProgress{};
        out.done = a;
        out.total = b;
        out.cached = c;
        out.outstanding = d;
        return true;
    }
    size_t at = line.find("PREFETCH ok=");
    if (at == std::string::npos) return false;
    out = WarmProgress{};
    out.summary = true;
    auto num = [&](const char* key) -> uint64_t {
        size_t k = line.find(std::string(" ") + key + "=", at);
        return k == std::string::npos ? 0 : std::strtoull(line.c_str() + k + std::strlen(key) + 2, nullptr, 10);
    };
    out.ok = line.compare(at, 13, "PREFETCH ok=1") == 0;
    out.offered = num("offered");
    out.cached = num("cached");
    out.stored = num("stored");
    out.bytes = num("bytes");
    if (size_t e = line.find(" error=", at); e != std::string::npos) out.error = line.substr(e + 7);
    return true;
}

std::string plainWarmProgress(const WarmProgress& p) {
    if (!p.summary) {
        if (p.total == 0) return "Downloading resources\xe2\x80\xa6";
        return "Downloading resources: " + std::to_string(p.done) + " of " + std::to_string(p.total) + " files";
    }
    if (!p.ok)
        return "The download stopped before it finished" + (p.error.empty() ? std::string(".") : ": " + p.error + ".") +
               " What arrived is kept; try again to finish.";
    if (p.stored == 0) return "All " + std::to_string(p.offered) + " resources were already downloaded.";
    char mb[32];
    std::snprintf(mb, sizeof mb, "%.1f", double(p.bytes) / (1024.0 * 1024.0));
    return "Resources downloaded: " + std::to_string(p.stored) + " new files (" + mb +
           " MiB). Joining now starts with them.";
}

std::string plainWarmState(const account::Server& s) {
    if (s.lastPrefetch.isZero())
        return "Resources not downloaded yet: the first join fetches them while you play, and the game looks "
               "unfinished "
               "until they arrive. Download resources fetches them now, without joining.";
    return "Resources downloaded " + fmtDayMonth(s.lastPrefetch.sec) + ".";
}

CloseAction closeAction(bool gameRunning, bool trayAvailable) {
    if (!gameRunning) return CloseAction::Quit;
    return trayAvailable ? CloseAction::HideToTray : CloseAction::Minimize;
}

}  // namespace fugue::pager

#include "pager/launch.h"
#include <signal.h>
#include "platform/file.h"
#include <QSettings>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include "net/guest.h"
#include "pager/model.h"

namespace fugue::pager {

const char* const kPasswordVars[] = {"FUGUE_BYOND_PASSWORD", nullptr};

std::vector<std::string> buildArgs(const account::Config& cfg, const account::Server& srv) {
    std::vector<std::string> a{"--connect", srv.addr};
    if (!cfg.account.empty()) {
        a.insert(a.end(), {"--account", cfg.account});
        if (srv.hub) a.push_back("--hub");
        std::string e;
        std::string p = account::identityPath(cfg.account, &e);
        if (!p.empty()) a.insert(a.end(), {"--identity-file", p});  // a PATH, 0600; the client reads the file itself
    }
    a.insert(a.end(), srv.flags.begin(), srv.flags.end());
    return a;
}

bool buildCommand(const account::Config& cfg, const account::Server& srv, const std::string& selfExe,
                  const std::vector<std::string>& passthrough, std::vector<std::string>* argv, std::string* err) {
    std::vector<std::string> prefix;
    if (!resolveClientArgv(cfg.client, CheckOptions{true, selfExe}, &prefix, err)) return false;
    *argv = prefix;
    for (auto& s : buildArgs(cfg, srv)) argv->push_back(s);
    for (auto& s : passthrough) argv->push_back(s);
    return true;
}

std::string displayCommand(const std::vector<std::string>& argv) {
    std::string o;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) o += ' ';
        o += argv[i];
    }
    return o;
}

QProcessEnvironment childEnvironment() {
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    for (const char* const* v = kPasswordVars; *v; v++) e.remove(QString::fromLatin1(*v));
    return e;
}

bool runSelf(const std::string& argv0, const std::vector<std::string>& args, bool detach, std::string* err,
             const std::vector<std::string>& extraEnv) {
#ifdef _WIN32
    // No exec on Windows: the next run is a new process and this one exits after it starts.
    (void)detach;
    QProcessEnvironment e = childEnvironment();
    for (const auto& kv : extraEnv) {
        const auto eq = kv.find('=');
        if (eq != std::string::npos)
            e.insert(QString::fromStdString(kv.substr(0, eq)), QString::fromStdString(kv.substr(eq + 1)));
    }
    QStringList qa;
    for (const auto& a : args) qa << QString::fromStdString(a);
    QProcess p;
    p.setProgram(QString::fromStdString(argv0));
    p.setArguments(qa);
    p.setProcessEnvironment(e);
    if (!p.startDetached()) {
        if (err) *err = "could not start " + argv0;
        return false;
    }
    return true;
#else
    std::vector<std::string> env = extraEnv;
    for (const QString& kv : childEnvironment().toStringList()) env.push_back(kv.toStdString());
    std::vector<char*> av{const_cast<char*>(argv0.c_str())}, ev;
    for (const auto& a : args) av.push_back(const_cast<char*>(a.c_str()));
    for (const auto& e : env) ev.push_back(const_cast<char*>(e.c_str()));
    av.push_back(nullptr);
    ev.push_back(nullptr);
    std::fflush(nullptr);
    auto run = [&] {
#ifdef __linux__
        ::close_range(3, ~0U, 0);
        ::execve("/proc/self/exe", av.data(), ev.data());
#else
        for (int fd = 3, n = int(::sysconf(_SC_OPEN_MAX)); fd < n && fd < 65536; fd++) ::close(fd);
        ::execve(argv0.c_str(), av.data(), ev.data());
#endif
    };
    if (!detach) {
        run();
        if (err) *err = std::string("exec: ") + std::strerror(errno);
        return false;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        if (err) *err = std::string("fork: ") + std::strerror(errno);
        return false;
    }
    if (pid == 0) {
        ::setsid();
        if (::fork() == 0) run();
        ::_exit(0);
    }
    ::waitpid(pid, nullptr, 0);
    return true;
#endif
}

std::string clientLogPath() {
    auto h = account::homeDir();
    return h ? *h + "/.fugue/client.log" : std::string();
}

std::string guestChoicePath() {
    std::string d = account::qtStateDir();
    return d.empty() ? std::string() : d + "/pager-guest";
}
bool guestRemembered() {
    std::string p = guestChoicePath();
    struct stat st;
    return !p.empty() && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool rememberGuest(bool on, std::string* err) {
    std::string p = guestChoicePath();
    if (p.empty()) {
        if (err) *err = "$HOME is not defined";
        return false;
    }
    if (on) return account::atomicWrite(p, "guest\n", err);
    if (platform::unlink(p.c_str()) != 0 && errno != ENOENT) {
        if (err) *err = "remove " + p + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

std::string autoSignInOffPath() {
    std::string d = account::qtStateDir();
    return d.empty() ? std::string() : d + "/pager-no-autosignin";
}
bool autoSignIn() {
    std::string p = autoSignInOffPath();
    struct stat st;
    return p.empty() || ::stat(p.c_str(), &st) != 0;
}
bool setAutoSignIn(bool on, std::string* err) {
    std::string p = autoSignInOffPath();
    if (p.empty()) {
        if (err) *err = "$HOME is not defined";
        return false;
    }
    if (!on) return account::atomicWrite(p, "off\n", err);
    if (platform::unlink(p.c_str()) != 0 && errno != ENOENT) {
        if (err) *err = "remove " + p + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

namespace {
std::atomic<int> gSignal{0};
void onSignal(int) { gSignal = 1; }
}  // namespace

int runHeadless(const std::vector<std::string>& argv, FILE* out) {
    if (argv.empty()) return 2;
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setProcessEnvironment(childEnvironment());
    QStringList args;
    for (size_t i = 1; i < argv.size(); i++) args << QString::fromStdString(argv[i]);
    p.start(QString::fromStdString(argv[0]), args);
    if (!p.waitForStarted(10000)) {
        std::fprintf(out, "[pager] could not start %s: %s\n", argv[0].c_str(), p.errorString().toUtf8().constData());
        return 127;
    }
    FILE* logf = nullptr;
    std::string lp = clientLogPath();
    if (!lp.empty()) {
        std::string dir = lp.substr(0, lp.rfind('/'));
        platform::mkdir(dir.c_str(), 0700);
        int fd = platform::open(lp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd >= 0) {
            logf = platform::fdopen(fd, "w");
            if (!logf) platform::close(fd);
        }
    }
    gSignal = 0;
    auto oldInt = ::signal(SIGINT, onSignal);
    auto oldTerm = ::signal(SIGTERM, onSignal);
    std::fprintf(out, "[pager] client pid %lld\n", (long long)p.processId());
    std::fflush(out);
    bool forwarded = false;
    auto drain = [&] {
        QByteArray b = p.readAll();
        if (b.isEmpty()) return;
        std::fwrite(b.constData(), 1, size_t(b.size()), out);
        std::fflush(out);
        if (logf) {
            std::fwrite(b.constData(), 1, size_t(b.size()), logf);
            std::fflush(logf);
        }
    };
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(100);
        drain();
        if (gSignal && !forwarded) {
            forwarded = true;
#ifdef _WIN32
            p.terminate();
            if (!p.waitForFinished(4000)) p.kill();
#else
            ::kill(pid_t(p.processId()), SIGINT);
#endif
        }
    }
    p.waitForFinished(5000);
    drain();
    if (logf) std::fclose(logf);
    ::signal(SIGINT, oldInt);
    ::signal(SIGTERM, oldTerm);
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : 128 + 2;
}

// ---------------- desktop ----------------
std::string desktopQuote(const std::string& s) {
    if (s.find_first_of(" \t\n\"'\\><~|&;$*?#()`%") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\')
            o += "\\\\";
        else if (c == '"')
            o += "\\\"";
        else if (c == '`')
            o += "\\`";
        else if (c == '$')
            o += "\\$";
        else if (c == '%')
            o += "%%";
        else
            o += c;
    }
    return o + "\"";
}

std::string desktopEntry(const std::string& exe) {
    // StartupWMClass: Qt's X11 WM_CLASS is the application name, "fugue-qt". A desktop that matches windows to .desktop
    // files by WM_CLASS needs it, because this file is named by app id.
    return "[Desktop Entry]\nType=Application\nName=Fugue\nComment=Join Space Station 13 servers\nExec=" +
           desktopQuote(exe) + " pager %u\nIcon=" + kAppId +
           "\nTerminal=false\nCategories=Game;\nMimeType=x-scheme-handler/byond;\nStartupWMClass=fugue-qt\n";
}

std::string urlHandlerCommand(const std::string& exe) { return "\"" + exe + "\" pager --url \"%1\""; }

std::optional<std::string> handlerUrl(const std::vector<std::string>& args, std::string* err) {
    auto fail = [&](const char* m) {
        if (err) *err = m;
        return std::nullopt;
    };
    if (args.size() != 2 || args[0] != "--url") return fail("--url takes exactly one link and nothing else");
    const std::string& u = args[1];
    if (u.empty()) return fail("--url: empty link");
    for (unsigned char c : u)
        if (c <= ' ' || c == '"' || c == 0x7f)
            return fail("--url: the link holds a quote, a space or a control character");
    return u;
}

DesktopResult installDesktop(const std::string& exe, const std::vector<std::pair<int, QByteArray>>& icons) {
    DesktopResult r;
#ifdef _WIN32
    // Per-user registration only; never requires elevation. Quote both executable and URL.
    const QString path = QDir::toNativeSeparators(QFileInfo(QString::fromStdString(exe)).absoluteFilePath());
    if (path.contains('"')) {
        r.err = "invalid executable path";
        return r;
    }
    QSettings reg("HKEY_CURRENT_USER\\Software\\Classes\\byond", QSettings::NativeFormat);
    reg.setValue(".", "URL:BYOND Protocol");
    reg.setValue("URL Protocol", "");
    reg.setValue("DefaultIcon/.", "\"" + path + "\",0");
    reg.setValue("shell/open/command/.", QString::fromStdString(urlHandlerCommand(path.toStdString())));
    reg.sync();
    r.ok = reg.status() == QSettings::NoError;
    r.desktopPath = "HKEY_CURRENT_USER\\Software\\Classes\\byond";
    if (!r.ok) r.err = "could not register the per-user byond:// handler";
    return r;
#else
    std::string base;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x)
        base = x;
    else if (auto h = account::homeDir())
        base = *h + "/.local/share";
    else {
        r.err = "no XDG_DATA_HOME and no HOME";
        return r;
    }
    QString dir = QString::fromStdString(base) + "/applications";
    if (!QDir().mkpath(dir)) {
        r.err = "cannot create " + dir.toStdString();
        return r;
    }
    r.desktopPath = dir.toStdString() + "/" + kDesktopFile;
    {
        QSaveFile f(QString::fromStdString(r.desktopPath));
        if (!f.open(QIODevice::WriteOnly)) {
            r.err = "cannot write " + r.desktopPath;
            return r;
        }
        f.write(QByteArray::fromStdString(desktopEntry(exe)));
        if (!f.commit()) {
            r.err = "cannot write " + r.desktopPath;
            return r;
        }
        ::chmod(r.desktopPath.c_str(), 0644);
    }
    // Remove a fugue.desktop whose content is a Fugue pager entry, so Fugue is not listed twice; any other file of that
    // name is kept.
    {
        QFile legacy(dir + "/" + kLegacyDesktopFile);
        if (legacy.open(QIODevice::ReadOnly)) {
            QByteArray b = legacy.readAll();
            legacy.close();
            if (b.startsWith("[Desktop Entry]\nType=Application\nName=Fugue\n") && b.contains(" pager %u\n"))
                legacy.remove();
        }
    }
    for (const auto& [px, png] : icons) {
        QString idir = QString::fromStdString(base) + QString("/icons/hicolor/%1x%1/apps").arg(px);
        if (!QDir().mkpath(idir)) {
            r.err = "cannot create " + idir.toStdString();
            return r;
        }
        std::string ip = idir.toStdString() + "/" + kAppId + ".png";
        QSaveFile f(QString::fromStdString(ip));
        if (!f.open(QIODevice::WriteOnly) || f.write(png) != png.size() || !f.commit()) {
            r.err = "cannot write " + ip;
            return r;
        }
        ::chmod(ip.c_str(), 0644);
        r.iconPaths.push_back(ip);
    }
    // Handler registration: the legacy-but-honoured per-user mimeapps.list under XDG_DATA_HOME/applications, merged in place.
    r.mimeappsPath = dir.toStdString() + "/mimeapps.list";
    QFile in(QString::fromStdString(r.mimeappsPath));
    QStringList lines;
    if (in.open(QIODevice::ReadOnly)) {
        lines = QString::fromUtf8(in.readAll()).split('\n');
        in.close();
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    const QString key = "x-scheme-handler/byond=";
    int sec = -1;
    for (int i = 0; i < lines.size(); i++)
        if (lines[i].trimmed() == "[Default Applications]") {
            sec = i;
            break;
        }
    if (sec < 0) {
        lines << "[Default Applications]" << key + kDesktopFile;
    } else {
        int j = sec + 1;
        bool done = false;
        for (; j < lines.size() && !lines[j].startsWith('['); j++)
            if (lines[j].startsWith(key)) {
                lines[j] = key + kDesktopFile;
                done = true;
                break;
            }
        if (!done) lines.insert(j, key + kDesktopFile);
    }
    QSaveFile mf(QString::fromStdString(r.mimeappsPath));
    if (!mf.open(QIODevice::WriteOnly)) {
        r.err = "cannot write " + r.mimeappsPath;
        return r;
    }
    mf.write((lines.join('\n') + "\n").toUtf8());
    if (!mf.commit()) {
        r.err = "cannot write " + r.mimeappsPath;
        return r;
    }
    r.ok = true;
    return r;
#endif
}

}  // namespace fugue::pager

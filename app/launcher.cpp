// fugue-qt: ONE binary, two halves (the pager and the client).
//   fugue-qt pager [flags] [byond://host:port]   the launcher: window by default, headless with -join/-list/-dry-run/...
//   fugue-qt client --connect host:port ...      the game window (app/main.cpp: clientMain)
//   fugue-qt version
//   fugue-qt webcheck                            WebEngine self-test for an installed package (app/webcheck.cpp)
//   dreamseeker "byond://host:port" -pager ...   fugue-qt installed AS DreamSeeker: DreamSeeker's own command line (pager/dsargv.h)
// No subcommand => the build's defaultMode: a FUGUE_RELEASE build is the pager (a player starting it from a menu passes no
// arguments); a development build stays the client.
// A join is `<self> client ...` run as a child with ARGV ONLY; the pager streams the child's log. Closing the pager while the
// game runs hides it to a tray icon (pagerwindow.h); only Exit ends the game.
// The pager reads a password in two places: -login ($FUGUE_BYOND_PASSWORD or an echo-off prompt) and the Sign in field.
// It uses the password for one hub call (pager/signin.h), wipes it, and never passes it on: not via argv, env, a file or a log.
#include <QApplication>
#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "fugue_env.h"
#include "account/store.h"
#include "fugue_version.h"
#include "pager/cache.h"
#include "pager/dsargv.h"
#include "pager/launch.h"
#include "pager/model.h"
#include "pager/signin.h"
#include "pager/target.h"
#include "pager/ui/instance.h"
#include "pager/ui/pagerwindow.h"
#include "shell/fugueicon.h"
#include "shell/urlopener.h"

int clientMain(int argc, char** argv);   // app/main.cpp
int webcheckMain(int argc, char** argv);   // app/webcheck.cpp

namespace {
using namespace fugue;

#ifdef FUGUE_RELEASE
constexpr bool kRelease = true;
#else
constexpr bool kRelease = false;
#endif

const char* kPagerUsage =
    "usage: fugue-qt pager [flags] [byond://host:port]\n"
    "  -join <target>      HEADLESS: join and stream the client's log (byond://host:port, +connect host:port or host:port)\n"
    "  -account <key>      BYOND key to play as for this run (empty = guest)\n"
    "  -hub                with -join: the target is hub-registered (a saved favourite remembers its own answer)\n"
    "  -dry-run            with -join: print the command that WOULD run and exit\n"
    "  -warm               with -join: pull the world's whole resource list into the shared ~/.fugue/rsc cache and exit (no window)\n"
    "  -list               print the saved server list and this box's readiness checklist, then exit\n"
    "  -login              sign in: fetch the account's key block from the hub (password: $FUGUE_BYOND_PASSWORD or prompt; stored nowhere)\n"
    "  -keytxt <path>      key.txt to use for this run\n"
#ifdef _WIN32
    "  -install-desktop    register Fugue as this user's byond:// handler (HKCU only; no Start menu shortcut)\n"
#elif defined(__APPLE__)
    "                      on macOS, copy Fugue.app to Applications and open it to register byond:// links\n"
#else
    "  -install-desktop    add Fugue (io.github.toomykins.Fugue.desktop + its icon) to this user's menu and make it the byond:// handler\n"
    "                      (writes under $XDG_DATA_HOME only)\n"
#endif
    "  --url <link>        join one clicked byond:// link; it must be the only argument (the Windows link handler)\n"
    "  --                  everything after is passed to the client verbatim\n";

struct PagerOpts {
    std::string join, account, keytxt, autoJoin, hubAddr;
    bool warm = false;   // -warm: run the client as a headless --prefetch (fill ~/.fugue/rsc) instead of a game window
    bool hub = false, dry = false, list = false, login = false, install = false, help = false, haveAccount = false;
    bool plain = false;   // no flags: at most a link, which a running pager can take (pager/ui/instance.h)
    double quitAfter = -1;
    std::vector<std::string> pass, positional;
    std::string bad;
};

PagerOpts parsePagerArgs(const std::vector<std::string>& a) {
    PagerOpts o;
    if (!a.empty() && a[0] == "--url") {
        if (auto u = pager::handlerUrl(a, &o.bad)) o.join = *u;
        o.plain = o.bad.empty();
        return o;
    }
    for (size_t i = 0; i < a.size(); i++) {
        std::string s = a[i];
        if (s == "--") { o.pass.assign(a.begin() + long(i) + 1, a.end()); break; }
        if (s.size() > 1 && s[0] == '-') {
            std::string n = s.substr(s[1] == '-' ? 2 : 1), val;
            bool hasVal = false;
            if (size_t eq = n.find('='); eq != std::string::npos) { val = n.substr(eq + 1); n = n.substr(0, eq); hasVal = true; }
            auto need = [&]() -> std::string {
                if (hasVal) return val;
                if (i + 1 >= a.size()) { o.bad = "flag needs a value: " + s; return {}; }
                return a[++i];
            };
            if (n == "join") o.join = need();
            else if (n == "account") { o.account = need(); o.haveAccount = true; }
            else if (n == "keytxt") o.keytxt = need();
            else if (n == "hub") o.hub = !hasVal || val != "false";
            else if (n == "dry-run") o.dry = true;
            else if (n == "warm") o.warm = true;
            else if (n == "list") o.list = true;
            else if (n == "login") o.login = true;
            else if (n == "install-desktop") o.install = true;
            else if (n == "h" || n == "help") o.help = true;
            else if (!kRelease && n == "auto-join") o.autoJoin = need();      // dev-only test hook
            else if (!kRelease && n == "quit-after") o.quitAfter = std::atof(need().c_str());   // dev-only test hook
            else if (!kRelease && n == "hub-addr") o.hubAddr = need();   // dev-only: sign in against a fake hub
            else o.bad = "unknown flag: " + s;
            continue;
        }
        o.positional.push_back(s);
    }
    // A bare argument is a server to join: how a clicked byond:// link arrives (`pager %u`).
    if (o.join.empty() && o.positional.size() == 1) o.join = o.positional[0];
    o.plain = a.size() == o.positional.size() && a.size() <= 1;
    return o;
}

// The password prompt for headless -login: echo off on the controlling terminal (via stty).
std::string readPassword() {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old = 0;
    bool tty = GetConsoleMode(input, &old) != 0;
    if (tty && !SetConsoleMode(input, old & ~ENABLE_ECHO_INPUT)) return {};
#else
    termios old{};
    bool tty = ::tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) { termios t = old; t.c_lflag &= ~tcflag_t(ECHO); ::tcsetattr(STDIN_FILENO, TCSANOW, &t); }
#endif
    std::string line;
    for (int c; (c = std::fgetc(stdin)) != EOF && c != '\n';) line.push_back(char(c));
#ifdef _WIN32
    if (tty) { SetConsoleMode(input, old); std::fputc('\n', stderr); }
#else
    if (tty) { ::tcsetattr(STDIN_FILENO, TCSANOW, &old); std::fputc('\n', stderr); }
#endif
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
}
void wipe(std::string& s) {
    volatile char* p = s.data();
    for (size_t i = 0; i < s.size(); i++) p[i] = 0;
    s.clear();
}

account::Server resolveTarget(const account::Config& cfg, const std::string& addr, bool hub) {
    int i = cfg.find(addr);
    if (i >= 0) return cfg.servers[size_t(i)];
    account::Server s;
    s.name = s.addr = addr;
    s.hub = hub;
    return s;
}

int pagerMain(int argc, char** argv, const std::vector<std::string>& args) {
    PagerOpts o = parsePagerArgs(args);
    if (!o.bad.empty()) { std::fprintf(stderr, "pager: %s\n%s", o.bad.c_str(), kPagerUsage); return 2; }
    if (o.help) { std::fputs(kPagerUsage, stdout); return 0; }
    const bool headless = o.install || o.list || o.login || !o.join.empty();
    std::unique_ptr<QCoreApplication> app;
    if (headless) app = std::make_unique<QCoreApplication>(argc, argv);
    else {
        app = std::make_unique<QApplication>(argc, argv);
        QApplication::setWindowIcon(fugue::shell::fugueIcon());   // the pager and its dialogs: Fugue's icon (shell/fugueicon.h)
        QGuiApplication::setDesktopFileName(fugue::shell::kDesktopFileName);
    }
    app->setApplicationName("fugue-qt");
    app->setApplicationVersion(fugue::kVersion);
    const std::string self = QCoreApplication::applicationFilePath().toStdString();
    if (o.plain && pager::forwardToRunning(pager::instanceName(), QString::fromStdString(o.join))) return 0;

    if (o.install) {
#ifdef __APPLE__
        std::fprintf(stderr, "pager: on macOS, copy Fugue.app to Applications and open it to register byond:// links.\n");
        return 1;
#endif
        // An AppImage runs from a temporary mount, so the menu entry must name the AppImage file itself.
        const QByteArray appImage = qgetenv("APPIMAGE");
        const std::string entryExe = appImage.isEmpty() ? self : appImage.toStdString();
        auto r = pager::installDesktop(entryExe, fugue::shell::fugueIconPngs());   // Fugue's icon into hicolor (shell/fugueicon.h)
        if (!r.ok) { std::fprintf(stderr, "pager: install-desktop: %s\n", r.err.c_str()); return 1; }
        std::printf("wrote %s -> %s\n", r.desktopPath.c_str(), entryExe.c_str());
        for (const auto& ip : r.iconPaths) std::printf("icon %s\n", ip.c_str());
        std::printf("byond:// links: registered in %s\nbyond:// links now open Fugue\n", r.mimeappsPath.c_str());
        return 0;
    }

    std::string err;
    auto loaded = account::loadConfig(&err);
    if (!loaded) { std::fprintf(stderr, "pager: config: %s\n", err.c_str()); return 1; }
    account::Config cfg = *loaded;
    const std::string savedAccount = cfg.account;
    if (o.haveAccount && !o.account.empty()) cfg.account = o.account;   // this run only: the saved one is put back before any write
    if (!o.keytxt.empty()) cfg.keytxt = o.keytxt;
    pager::CheckOptions copt;
    copt.selfIsClient = true;
    copt.selfExe = self;

    const std::string hubAddr = o.hubAddr.empty() ? pager::kHubLinkAddr : o.hubAddr;
    if (o.login) {
        if (cfg.account.empty()) { std::fprintf(stderr, "pager: no account set -- run with -account <YourBYONDKey>\n"); return 1; }
        // Read the password here, use it for this one call, then drop it.
        std::string pw;
        if (auto e = fugueEnv("BYOND_PASSWORD"); e && !e->empty()) pw = *e;
        else {
            std::fprintf(stderr, "BYOND password for %s (used once to sign in, stored nowhere): ", cfg.account.c_str());
            pw = readPassword();
        }
        pager::SignInResult r = pager::signIn(cfg.account, pw, cfg.keytxt, hubAddr);
        wipe(pw);
        if (!r.ok) { std::fprintf(stderr, "pager: %s (%s)\n", pager::plainSignInMessage(r.failure).c_str(), r.detail.c_str()); return 1; }
        cfg.account = r.account;
        if (!o.keytxt.empty()) cfg.keytxt = loaded->keytxt;
        std::string serr;
        if (!account::saveConfig(cfg, &serr)) std::fprintf(stderr, "pager: warning: could not save the account name: %s\n", serr.c_str());
        std::printf("wrote %s's key block to %s (0600). You can play as %s now.\n", r.account.c_str(), r.keyTxtPath.c_str(), r.account.c_str());
        return 0;
    }
    if (o.list) {
        std::string s = pager::renderList(cfg, pager::check(cfg, copt));
        std::fwrite(s.data(), 1, s.size(), stdout);
        return 0;
    }
    if (!o.join.empty()) {
        std::string terr;
        auto addr = pager::parseTarget(o.join, &terr);
        if (!addr) { std::fprintf(stderr, "pager: %s\n", terr.c_str()); return 1; }
        account::Server srv = resolveTarget(cfg, *addr, o.hub);
        pager::Status st = pager::check(cfg, copt);
        if (auto b = pager::blocker(st, srv)) { std::fprintf(stderr, "pager: %s\n", b->c_str()); return 1; }
        std::vector<std::string> cmd;
        if (o.warm) o.pass.push_back("--prefetch");
        if (!pager::buildCommand(cfg, srv, self, o.pass, &cmd, &err)) { std::fprintf(stderr, "pager: %s\n", err.c_str()); return 1; }
        if (o.dry) {
            std::printf("%s\n", pager::displayCommand(cmd).c_str());
            for (const char* const* v = pager::kPasswordVars; *v; v++)
                if (std::getenv(*v)) std::fprintf(stderr, "(%s is set in this environment; it is withheld from the client)\n", *v);
            return 0;
        }
        cfg.remember(srv);
        account::Config persist = cfg;
        persist.account = savedAccount;
        if (!o.keytxt.empty()) persist.keytxt = loaded->keytxt;
        std::string serr;
        if (!account::saveConfig(persist, &serr)) std::fprintf(stderr, "pager: warning: could not save the server list: %s\n", serr.c_str());
        int rc = pager::runHeadless(cmd, stdout);
        if (o.warm && rc == 0) {   // record the warm-up time on success
            persist.markPrefetched(srv);
            if (!account::saveConfig(persist, &serr)) std::fprintf(stderr, "pager: warning: could not save the warm-up time: %s\n", serr.c_str());
        }
        return rc;
    }

    // ---- the window ----
    pager::PagerWindow win;
    std::string persistedAccount = savedAccount;   // what pager.json says; an -account override is this run only
    auto persist = [&] {
        account::Config p = cfg;
        p.account = persistedAccount;
        if (!o.keytxt.empty()) p.keytxt = loaded->keytxt;
        std::string se;
        if (!account::saveConfig(p, &se)) win.appendLog("[pager] could not save the server list: " + QString::fromStdString(se));
    };
    auto refresh = [&] { win.setState(cfg, pager::check(cfg, copt)); };
    // After a remembered "Play as guest" the window opens on the server list rather than Sign in.
    if (cfg.account.empty() && pager::guestRemembered()) win.setGuestChosen(true);
    win.setAutoSignIn(pager::autoSignIn());
#ifdef FUGUE_GPU
    win.setRendererSettings(QString::fromStdString(pager::displaySettingsPath(pager::clientStateDir(o.pass))));
#endif
    refresh();
    QProcess* child = nullptr;
    auto stopChild = [&] {
        if (child && child->state() != QProcess::NotRunning) {
#ifdef _WIN32
            child->terminate();
#else
            ::kill(pid_t(child->processId()), SIGINT);   // a clean disconnect lets the same account straight back in
#endif
            if (!child->waitForFinished(4000)) child->kill();
        }
    };
    // Warm-up process (Download resources). Stopping it loses nothing: each file is stored by temp+rename once verified,
    // so a later pass resumes where this one stopped.
    QProcess* warmer = nullptr;
    bool warmCancelled = false;
    auto stopWarmer = [&] {
        if (warmer && warmer->state() != QProcess::NotRunning) {
            warmCancelled = true;
#ifdef _WIN32
            warmer->terminate();
#else
            ::kill(pid_t(warmer->processId()), SIGINT);
#endif
            if (!warmer->waitForFinished(3000)) warmer->kill();
        }
    };
    QObject::connect(&win, &pager::PagerWindow::joinRequested, &win, [&](const QString& qaddr, bool hub, const QString&) {
        account::Server srv = resolveTarget(cfg, qaddr.toStdString(), hub);
        pager::Status s2 = pager::check(cfg, copt);
        if (auto b = pager::blocker(s2, srv)) {
            win.appendLog("pager: " + QString::fromStdString(*b));
            win.setStatus(QString::fromStdString(pager::plainBlocker(s2, srv).value_or(*b)), true);
            return;
        }
        // Joining stops any download: the game's own connection fetches the rest, and two connections would share one link.
        stopWarmer();
        std::vector<std::string> cmd;
        std::string e2;
        if (!pager::buildCommand(cfg, srv, self, o.pass, &cmd, &e2)) {
            win.appendLog("pager: " + QString::fromStdString(e2));
            win.setStatus("Could not start the game. Open Details to see why.", true);
            return;
        }
        cfg.remember(srv);
        persist();
        delete child;
        child = new QProcess(&win);
        child->setProcessChannelMode(QProcess::MergedChannels);
        child->setProcessEnvironment(pager::childEnvironment());
        QObject::connect(child, &QProcess::readyRead, &win, [&win, c = child] { win.appendLog(QString::fromUtf8(c->readAll())); });
        QObject::connect(child, &QProcess::started, &win, [&win, c = child] {
            win.appendLog(QString("[pager] client pid %1").arg(qint64(c->processId())));
            win.setClientRunning(true);
        });
        QObject::connect(child, &QProcess::errorOccurred, &win, [&win, c = child](QProcess::ProcessError) {
            if (c->state() == QProcess::NotRunning) {
                win.appendLog("[pager] could not start the client: " + c->errorString());
                win.setClientRunning(false);
                win.setStatus("Could not start the game. Open Details to see why.", true);
            }
        });
        const bool guestOnHub = cfg.account.empty() && srv.hub;
        QObject::connect(child, &QProcess::finished, &win, [&win, c = child, guestOnHub](int code, QProcess::ExitStatus es) {
            win.appendLog(QString::fromUtf8(c->readAll()));
            win.appendLog(QString("[pager] client exited (%1)").arg(code));
            win.setClientRunning(false);
            if (code == 0 && es == QProcess::NormalExit) win.setStatus("The game closed.");
            // A world that bans guests closes the connection silently, so name that as a likely cause.
            else if (guestOnHub) win.setStatus("The game closed unexpectedly. If this server doesn't allow guests, sign in to play here.", true);
            else win.setStatus("The game closed unexpectedly. Open Details to see why.", true);
        });
        QStringList qargs;
        for (size_t i = 1; i < cmd.size(); i++) qargs << QString::fromStdString(cmd[i]);
        win.appendLog("[pager] " + QString::fromStdString(pager::displayCommand(cmd)));
        win.setStatus("Starting the game\xe2\x80\xa6");
        child->start(QString::fromStdString(cmd[0]), qargs);
    });
    // Download resources: the client's headless --prefetch pass for one saved server, with progress on the game tab.
    // One at a time, never while a game runs; Cancel or Join stops it.
    QObject::connect(&win, &pager::PagerWindow::warmRequested, &win, [&](const QString& qaddr, bool hub, const QString&) {
        if (warmer && warmer->state() != QProcess::NotRunning) return;
        warmCancelled = false;
        account::Server srv = resolveTarget(cfg, qaddr.toStdString(), hub);
        pager::Status s2 = pager::check(cfg, copt);
        if (auto b = pager::blocker(s2, srv)) { win.setStatus(QString::fromStdString(pager::plainBlocker(s2, srv).value_or(*b)), true); return; }
        std::vector<std::string> pass = o.pass, cmd;
        pass.push_back("--prefetch");
        std::string e2;
        if (!pager::buildCommand(cfg, srv, self, pass, &cmd, &e2)) { win.appendLog("pager: " + QString::fromStdString(e2)); win.setStatus("Could not start the download. Open Details to see why.", true); return; }
        delete warmer;
        warmer = new QProcess(&win);
        warmer->setProcessChannelMode(QProcess::MergedChannels);
        warmer->setProcessEnvironment(pager::childEnvironment());
        auto buf = std::make_shared<std::string>();
        auto last = std::make_shared<pager::WarmProgress>();
        auto feed = [&win, buf, last](const QByteArray& b) {
            win.appendLog(QString::fromUtf8(b));
            buf->append(b.constData(), size_t(b.size()));
            size_t nl;
            while ((nl = buf->find('\n')) != std::string::npos) {
                std::string line = buf->substr(0, nl);
                buf->erase(0, nl + 1);
                pager::WarmProgress wp;
                if (!pager::parseWarmLine(line, wp)) continue;
                *last = wp;
                if (!wp.summary) win.setWarming(true, QString::fromStdString(pager::plainWarmProgress(wp)));
            }
        };
        QObject::connect(warmer, &QProcess::readyRead, &win, [feed, c = warmer] { feed(c->readAll()); });
        QObject::connect(warmer, &QProcess::errorOccurred, &win, [&win, c = warmer](QProcess::ProcessError) {
            if (c->state() == QProcess::NotRunning) { win.appendLog("[pager] could not start the download: " + c->errorString()); win.setWarming(false, "Could not start the download. Open Details to see why.", true); }
        });
        QObject::connect(warmer, &QProcess::finished, &win, [&, feed, last, srv, c = warmer](int code, QProcess::ExitStatus es) {
            feed(c->readAll() + "\n");
            win.appendLog(QString("[pager] download exited (%1)").arg(code));
            const bool ok = code == 0 && es == QProcess::NormalExit && last->summary && last->ok;
            if (ok) { cfg.markPrefetched(srv); persist(); refresh(); }
            QString msg = last->summary ? QString::fromStdString(pager::plainWarmProgress(*last))
                        : warmCancelled ? QString("Download stopped. What arrived is kept; Download resources picks up where it stopped.")
                                        : QString("The download stopped unexpectedly. Open Details to see why.");
            if (warmCancelled && !ok) { win.setWarming(false, msg); return; }
            win.setWarming(false, msg, !ok);
        });
        QStringList qargs;
        for (size_t i = 1; i < cmd.size(); i++) qargs << QString::fromStdString(cmd[i]);
        win.appendLog("[pager] " + QString::fromStdString(pager::displayCommand(cmd)));
        win.setWarming(true, "Connecting to download resources\xe2\x80\xa6");
        warmer->start(QString::fromStdString(cmd[0]), qargs);
    });
    QObject::connect(&win, &pager::PagerWindow::warmCancelRequested, &win, [&] { stopWarmer(); });
    // Sign in runs the hub round trip on a worker thread. The password is moved into that thread, wiped when the call
    // returns, and never written anywhere.
    QObject::connect(&win, &pager::PagerWindow::signInRequested, &win, [&](const QString& user, const QString& password) {
        win.setSigningIn(true);
        auto result = std::make_shared<pager::SignInResult>();
        QThread* t = QThread::create([result, u = user.toStdString(), pw = password.toStdString(), kt = cfg.keytxt, hubAddr]() mutable {
            *result = pager::signIn(u, pw, kt, hubAddr);
            wipe(pw);
        });
        QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);
        QObject::connect(t, &QThread::finished, &win, [&, result] {
            if (result->ok) {
                cfg.account = persistedAccount = result->account;
                persist();
                if (std::string ge; !pager::rememberGuest(false, &ge)) win.appendLog("[pager] could not forget the guest choice: " + QString::fromStdString(ge));
                win.appendLog("[pager] sign-in: " + QString::fromStdString(result->detail));
                refresh();
                win.setSigningIn(false);
                win.closeSignIn();
                win.setStatus({});
            } else {
                win.appendLog("[pager] sign-in failed: " + QString::fromStdString(result->detail));
                win.setSigningIn(false);
                win.setSignInError(QString::fromStdString(pager::plainSignInMessage(result->failure)));
            }
        });
        t->start();
    });
    // Sign in without a password, using this computer's saved sign-in for the account: nothing to fetch, only the
    // account the pager plays as changes.
    QObject::connect(&win, &pager::PagerWindow::savedSignInChosen, &win, [&](const QString& acct) {
        cfg.account = persistedAccount = acct.toStdString();
        persist();
        if (std::string ge; !pager::rememberGuest(false, &ge)) win.appendLog("[pager] could not forget the guest choice: " + QString::fromStdString(ge));
        win.appendLog("[pager] sign-in: using this computer's saved sign-in for " + acct);
        refresh();
        win.setStatus({});
    });
    QObject::connect(&win, &pager::PagerWindow::autoSignInChanged, &win, [&](bool on) {
        if (std::string ae; !pager::setAutoSignIn(on, &ae)) win.appendLog("[pager] could not save the sign-in preference: " + QString::fromStdString(ae));
    });
    QObject::connect(&win, &pager::PagerWindow::guestRequested, &win, [&] {
        cfg.account.clear();
        persistedAccount.clear();
        persist();
        if (std::string ge; !pager::rememberGuest(true, &ge)) win.appendLog("[pager] could not remember the guest choice: " + QString::fromStdString(ge));
        refresh();
    });
    QObject::connect(&win, &pager::PagerWindow::signOutRequested, &win, [&] {
        // Sign out forgets which account this pager plays as. The cached identity file stays; only Clear cache removes it.
        cfg.account.clear();
        persistedAccount.clear();
        persist();
        // Signing out is not a guest choice: forget any earlier one, so the next launch opens on Sign in.
        if (std::string ge; !pager::rememberGuest(false, &ge)) win.appendLog("[pager] could not forget the guest choice: " + QString::fromStdString(ge));
        refresh();
        win.showSignIn();
    });
    // Clear cache removes the identity-<ckey> files, the remembered-guest flag, ~/.fugue/rsc and ~/.fugue/cauth.txt
    // (pager/cache.h); pager.json and the sid files stay. A removed identity sends the window back to Sign in. Refused while a client runs.
    QObject::connect(&win, &pager::PagerWindow::clearCacheRequested, &win, [&] {
        if (child && child->state() != QProcess::NotRunning) return;
        std::string ce;
        pager::CachePlan done = pager::clearCache(&ce);
        for (auto& i : done.items) win.appendLog("[pager] clear cache: removed " + QString::fromStdString(i.path));
        if (!ce.empty()) win.appendLog("[pager] clear cache: " + QString::fromStdString(ce));
        refresh();
        win.setStatus(ce.empty() ? QString("Cleared the cache (%1).").arg(QString::fromStdString(pager::humanBytes(done.bytes())))
                                 : QString("Some cached files could not be removed. Open Details to see why."), !ce.empty());
    });
    QObject::connect(&win, &pager::PagerWindow::serverAdded, &win, [&](const QString& qaddr) {
        // remember() stamps LastPlayed; an added server has not been played, so clear that stamp.
        std::string addr = qaddr.toStdString();
        bool existed = cfg.find(addr) >= 0;
        account::Server s;
        s.name = s.addr = addr;
        if (existed) s = cfg.servers[size_t(cfg.find(addr))];
        cfg.remember(s);
        if (int i = cfg.find(addr); i >= 0 && !existed) cfg.servers[size_t(i)].lastPlayed = account::Timestamp{};
        persist();
        refresh();
    });
    QObject::connect(&win, &pager::PagerWindow::serverRemoved, &win, [&](const QString& qaddr) {
        cfg.remove(qaddr.toStdString());
        persist();
        refresh();
    });
    QObject::connect(&win, &pager::PagerWindow::accountApplied, &win, [&](const QString& acct) {
        cfg.account = persistedAccount = acct.toStdString();
        persist();
        refresh();
    });
    // The pager quits on Exit or on closing it with no game running; a window hidden to the tray is not a last window closed.
    QApplication::setQuitOnLastWindowClosed(false);
    QObject::connect(&win, &pager::PagerWindow::quitRequested, app.get(), &QCoreApplication::quit);
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &win, stopChild);
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &win, stopWarmer);   // a download in progress keeps what arrived
    // A link opened while the pager runs joins like the Join button, and like it is refused while a game runs.
    auto openLink = [&](const std::string& link) {
        win.showPager();
        if (child && child->state() != QProcess::NotRunning) { win.setStatus("Close the game before joining another server.", true); return; }
        std::string error;
        auto addr = pager::parseTarget(link, &error);
        if (!addr) { win.setStatus(QString::fromStdString(error), true); return; }
        emit win.joinRequested(QString::fromStdString(*addr), false, QString());
    };
#ifdef Q_OS_MACOS
    fugue::shell::UrlOpener urlOpener(&win, [&](const QUrl& url) { openLink(url.toString(QUrl::FullyEncoded).toStdString()); });
#endif
    pager::InstanceServer instance([&](const QString& link) {
        if (link.isEmpty()) win.showPager();
        else openLink(link.toStdString());
    });
    instance.listen(pager::instanceName());
    win.show();
    // Signed out (and not a remembered guest), the Sign in window opens over the pager. With "Sign in automatically"
    // off, a saved account waits there to be confirmed. An -account override is not held.
    if (!o.haveAccount && !cfg.account.empty() && !pager::autoSignIn()) QTimer::singleShot(0, &win, [&] { win.holdSignIn(); });
    else if (win.page() == pager::PagerWindow::SignInPage) QTimer::singleShot(0, &win, [&] { win.showSignIn(); });
    if (!kRelease) {   // dev-only hooks, compiled out of FUGUE_RELEASE
        if (!o.autoJoin.empty()) {
            QTimer::singleShot(300, &win, [&] {
                std::string terr;
                auto a = pager::parseTarget(o.autoJoin, &terr);
                emit win.joinRequested(QString::fromStdString(a ? *a : o.autoJoin), false, QString());
            });
        }
        if (o.quitAfter >= 0)
            QTimer::singleShot(int(o.quitAfter * 1000), &win, [&] {
                stopChild();
                std::printf("LOGPANE-BEGIN\n%s\nLOGPANE-END\n", win.logText().toUtf8().constData());
                std::fflush(stdout);
                QCoreApplication::quit();
            });
    }
    return app->exec();
}
}  // namespace

int main(int argc, char** argv) {
#ifdef Q_OS_MACOS
    // BYOND's CTRL macros refer to the physical Control key, not Command.
    QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
#endif
    // Mode selection: pager/dsargv.h chooseMode. Installed as dreamseeker (a symlink or copy, any case), the whole
    // command line is DreamSeeker's, so the mode is the client even in a release build.
    std::vector<std::string> args;
    for (int i = 1; i < argc; i++) args.push_back(argv[i]);
    fugue::pager::Mode m = fugue::pager::chooseMode(argc > 0 ? argv[0] : "", args, kRelease);
    const std::string& mode = m.mode;
    std::vector<std::string>& rest = m.rest;
    if (mode == "version") { std::printf("%s\n", kRelease ? kVersionRelease : kVersionDev); return 0; }
    if (mode == "pager") return pagerMain(argc, argv, rest);
    if (mode == "webcheck") return webcheckMain(argc, argv);
    // client: hand clientMain an argv with the subcommand removed
    std::vector<char*> cargv{argv[0]};
    for (auto& s : rest) cargv.push_back(s.data());
    cargv.push_back(nullptr);
    return clientMain(int(cargv.size()) - 1, cargv.data());
}

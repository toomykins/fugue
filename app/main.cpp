// fugue-qt client --connect host:port [--state-dir d] [--size WxH]
// dreamseeker "byond://host:port" [-pager] [-trusted] ...   (DreamSeeker's own command line: pager/dsargv.h)
// dreamseeker                                              (no argument: DreamSeeker's Open Location box)
//
// Thread model:
//   GUI thread     widgets.
//   SessionWorker  owns net::Session + model::Session and emits value-type signals.
#include <QApplication>
#include <QIcon>
#include <QPalette>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QMetaType>
#include <QCommandLineParser>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>
#include <QInputDialog>
#include <QLineEdit>
#include <QFileDialog>
#include <QDir>
#include <QCursor>
#include <QMouseEvent>
#include <functional>
#include <cstdio>
#include "fugue_version.h"
#include "app/sessionworker.h"
#include "shell/gpubackend.h"
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/fugueicon.h"
#include "shell/urlopener.h"
#include "pager/launch.h"
#include "shell/inputrouter.h"
#include "shell/mapwidget.h"
#include "shell/screenshot.h"
#include "shell/macrodialog.h"
#include "model/browsing.h"
#include "net/guest.h"
#include "net/input.h"
#include "shell/serverkey.h"
#include "shell/skinsync.h"
#include "shell/skinwindow.h"
#include "shell/ttmetrics.h"
#include "skin/usermacros.h"
#include "skin/winset.h"
#include "net/guest.h"
#include <QPointer>
#include <signal.h>
#include <cstdlib>
#ifdef __linux__
#include <sys/prctl.h>
#elif defined(__APPLE__)
#include <sys/resource.h>
#endif
#include "account/login.h"
#include "account/records.h"
#include "account/store.h"
#include "app/defaultskin.h"  // kDefaultSkin: used until (or unless) the world sends a skin
#include "pager/clientaccount.h"
#include "pager/hubcert.h"
#include "pager/dsargv.h"
#include "pager/launch.h"
#include "pager/target.h"
#include "pager/worldlink.h"
#include <optional>
#include <set>
#include <string>
#include <vector>

bool prefetchMain(int argc, char** argv, int* rc);  // app/prefetchmode.cpp

static volatile sig_atomic_t gInt = 0;  // SIGINT/SIGTERM: quit
static bool gLinkSpawned = false;       // this window opened its second client

int clientMain(int argc, char** argv) {  // called by app/launcher.cpp's main() (mode switch)
    {
        int prc = 0;
        if (prefetchMain(argc, argv, &prc)) return prc;
    }  // --prefetch: headless cache warm-up, no QApplication
#ifdef FUGUE_RELEASE
#ifdef __linux__
    // A release client holds a bearer token in memory: refuse ptrace and /proc/<pid>/{environ,mem} reads.
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
#elif defined(__APPLE__)
    // macOS has no PR_SET_DUMPABLE. Disable core dumps; debugger access is governed by macOS code signing.
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
#endif
#endif
    // Both MUST precede QApplication: byond:// interception and the shared GL context WebEngine wants.
    fugue::shell::registerSchemes();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    // Keep argv as DreamSeeker counts it: its +connect bound includes Qt's own arguments, which QApplication removes.
    // Which ones were removed is read by pointer (pager/dsargv.h takenByHost), since Qt compacts argv in place.
    std::vector<std::string> preQtArgs;
    std::vector<const char*> preQtPtrs;
    for (int i = 1; i < argc; i++) {
        preQtArgs.push_back(argv[i]);
        preQtPtrs.push_back(argv[i]);
    }
    QApplication app(argc, argv);
    // Vulkan on Wayland presents without vsync: KWin releases a vsync (FIFO) present only every 50 ms while the screen is
    // captured or streamed, holding the window near 15 frames a second. Wayland compositors do not tear unless asked
    // to, and the map paces itself (it redraws only when a newer frame is published). Before any window exists.
    if (fugue::shell::gpuApi() == "vulkan" && QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        format.setSwapInterval(0);
        QSurfaceFormat::setDefaultFormat(format);
    }
    fugue::shell::installTrueTypeMetrics();  // MeasureText's TrueType faces (render/dsmaptext.h)
    fugue::shell::installMaptextGlyphs();    // map text glyphs (render/maptextpaint.h)
    // DreamSeeker is light whatever the desktop theme: its controls use fixed system colours. Fusion with this palette
    // matches stock DreamSeeker 516 (window #F5F5F5, panes #FFFFFF, push face #E1E1E1, selection #0078D7).
    // A skin's own background-color / text-color still wins per control (shell/skinwindow.cpp).
    app.setStyle(QStyleFactory::create("Fusion"));
    {
        QPalette p;
        p.setColor(QPalette::Window, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::WindowText, Qt::black);
        p.setColor(QPalette::Base, Qt::white);
        p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::Text, Qt::black);
        p.setColor(QPalette::Button, QColor(0xe1, 0xe1, 0xe1));
        p.setColor(QPalette::ButtonText, Qt::black);
        p.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xd7));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
        p.setColor(QPalette::ToolTipText, Qt::black);
        p.setColor(QPalette::PlaceholderText, QColor(0x6a, 0x6a, 0x6a));
        p.setColor(QPalette::Light, Qt::white);
        p.setColor(QPalette::Mid, QColor(0xa6, 0xa6, 0xa6));
        p.setColor(QPalette::Dark, QColor(0xa0, 0xa0, 0xa0));
        p.setColor(QPalette::Shadow, QColor(0x69, 0x69, 0x69));
        for (auto r : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            p.setColor(QPalette::Disabled, r, QColor(0x6d, 0x6d, 0x6d));
        app.setPalette(p);
    }
    qRegisterMetaType<fugue::shell::PromptRequest>();
    qRegisterMetaType<fugue::shell::StatView>();
    qRegisterMetaType<fugue::shell::MapMenu>();
    app.setApplicationName("fugue-qt");
    app.setApplicationVersion(fugue::kVersion);
    // Fugue's icon for windows without the world's own. The desktop file name is the app id that desktops use for the
    // icon (shell/fugueicon.h).
    app.setWindowIcon(fugue::shell::fugueIcon());
    QGuiApplication::setDesktopFileName(fugue::shell::kDesktopFileName);

    QCommandLineParser cp;
    cp.addHelpOption();
    QCommandLineOption oConnect("connect", "host:port of the world", "host:port");
    // An explicit --state-dir never plays as the pager's signed-in account (pager/clientaccount.h).
    QCommandLineOption oState(
        "state-dir",
        "identity dir (default ~/.fugue); with it the resource cache is <dir>/rsc, and the run never "
        "uses the pager's signed-in account (a guest unless --account is given)",
        "dir");
    QCommandLineOption oRsc(
        "rsc-dir", "resource cache dir (content-addressed by resource id; default: the SHARED ~/.fugue/rsc)", "dir");
    QCommandLineOption oSize("size", "initial window size WxH (default: the skin's)", "WxH");
    QCommandLineOption oPace("no-pacing", "no human-pacing delays during login");
    // Account join: the pager passes the key and the path of the 0600 identity file. The client reads that file and
    // never takes a password.
    QCommandLineOption oAccount("account", "BYOND key to log in as (identity from the pager's cache; empty = guest)",
                                "key");
    QCommandLineOption oIdent("identity-file",
                              "path of the account's 0600 identity file (default ~/.fugue/identity-<ckey>)", "path");
    QCommandLineOption oHub("hub",
                            "the target is hub-registered (accepted; every account login answers a hub-registered "
                            "server's login-cert challenge (CertChallenge) regardless)");
    QCommandLineOption oAudio(
        "audio",
        "sound output: auto (system output, the default), none, wav:<path> (record the mix), "
        "cmd:a,b,c (Linux/macOS: pipe s16le stereo 44.1 kHz to your own player). Empty = $FUGUE_AUDIO, then auto",
        "spec");
    cp.addOptions({oConnect, oState, oRsc, oSize, oPace, oAccount, oIdent, oHub, oAudio});
    // DreamSeeker's own command line is accepted too (pager/dsargv.h), so `dreamseeker "byond://host:port" -pager` works
    // as a launch command. QApplication has already removed Qt's own arguments. When invoked as DreamSeeker, an unknown
    // --word is dropped, as DreamSeeker drops it.
    const bool asDreamSeeker = fugue::pager::invokedAsDreamSeeker(argv[0]);
    // The option table used by the DreamSeeker parse and the account rule (pager/clientaccount.h clientOptions) must
    // match this parser. Checked here, so an option added above without updating it fails loudly.
    const fugue::pager::FugueOptions fopts = fugue::pager::clientOptions(asDreamSeeker);
    {
        fugue::pager::FugueOptions mine;
        mine.flag = {"help", "help-all"};  // cp.addHelpOption()
        for (const QCommandLineOption& o : {oConnect, oState, oRsc, oSize, oPace, oAccount, oIdent, oHub, oAudio})
            for (const QString& n : o.names())
                (o.valueName().isEmpty() ? mine.flag : mine.value).insert(n.toStdString());
        if (mine.flag != fopts.flag || mine.value != fopts.value) {
            std::fprintf(stderr, "fugue-qt: internal error: app/main.cpp's options and pager::clientOptions differ\n");
            return 2;
        }
    }
    std::vector<const char*> postQtPtrs;
    for (int i = 1; i < argc; i++) postQtPtrs.push_back(argv[i]);
    const std::vector<bool> hostTaken = fugue::pager::takenByHost(preQtPtrs, postQtPtrs);
    const fugue::pager::DsArgv ds = fugue::pager::parseDsArgv(preQtArgs, fopts, hostTaken);
    {
        QStringList own{QString::fromLocal8Bit(argv[0])};
        for (const std::string& a : ds.fugue) own << QString::fromStdString(a);
        cp.process(own);
    }
    for (const std::string& a : ds.dropped)
        std::fprintf(stderr, "[app] DreamSeeker argument ignored, as DreamSeeker ignores it: %s\n", a.c_str());
    // Accepted with nothing to change in Fugue:
    //   -pager                  Fugue has no splash window and no pager channel.
    //   -trusted / -safe / -ultrasafe   restrict only the world DreamSeeker hosts itself; Fugue never hosts a world.
    //   -isserver               no channel a world has into the skin reflects it (winget "IsServer" answers "").
    //   -trace, -debug          no effect on the client.
    if (ds.trace || ds.pager || ds.security >= 0 || ds.isServer || ds.debug)
        std::fprintf(stderr, "[app] DreamSeeker flags:%s%s%s%s%s (accepted; nothing to change in Fugue)\n",
                     ds.pager ? " -pager" : "", ds.trace ? " -trace" : "",
                     ds.security == 0   ? " -trusted"
                     : ds.security == 1 ? " -safe"
                     : ds.security == 2 ? " -ultrasafe"
                                        : "",
                     ds.isServer ? " -isserver" : "", ds.debug ? " -debug" : "");
    // --connect wins over a DreamSeeker target. Both are normalised as the pager does (pager/target.h).
    std::string rawTarget = cp.isSet(oConnect) ? cp.value(oConnect).toStdString() : ds.target;
    std::optional<std::string> target;
    std::string terr;
    if (rawTarget.empty()) {
        switch (fugue::pager::noTargetAction(preQtArgs.size() + 1, ds)) {
            case fugue::pager::NoTarget::QuietExit: return 0;  // DreamSeeker opens no window and exits
            case fugue::pager::NoTarget::Usage:
                std::fprintf(stderr,
                             "usage: fugue-qt --connect host:port [...]   or   dreamseeker \"byond://host:port\" "
                             "[-pager] [...]\n");
                return 2;
            case fugue::pager::NoTarget::OpenLocation:  // DreamSeeker's lone "Open Location" box
                break;
        }
        // Like stock DreamSeeker with no argument: one dialog, OK joins the address, Escape exits. The prompt text is
        // stock's, minus the hub identifier Fugue refuses (pager/target.h).
        QString prompt = "Enter the server/port identifier (eg: foobar.com:5000) you would like to visit.";
        for (;;) {
            bool ok = false;
            QString t = QInputDialog::getText(nullptr, "Open Location", prompt, QLineEdit::Normal, QString(), &ok);
            if (!ok) return 0;  // Escape exits without a window
            if (t.trimmed().isEmpty()) continue;
            target = fugue::pager::parseTarget(t.trimmed().toStdString(), &terr);
            if (target) break;
            prompt = QString::fromStdString(terr) +
                     "\n\nEnter the server/port identifier (eg: foobar.com:5000) you would like to visit.";
        }
    } else {
        target = fugue::pager::parseTarget(rawTarget, &terr);
        if (!target) {
            std::fprintf(stderr, "fugue-qt: %s\n", terr.c_str());
            return 2;
        }
    }
    QString conn = QString::fromStdString(*target);
    int colon = conn.lastIndexOf(':');

    fugue::net::SessionConfig cfg;
    cfg.host = conn.left(colon).toStdString();
    cfg.port = uint16_t(conn.mid(colon + 1).toInt());
    cfg.stateDir = cp.value(oState).toStdString();
    cfg.rscDir = cp.value(oRsc).toStdString();
    if (cp.isSet(oPace)) cfg.humanPacing = false;
    // Account rule: pager/clientaccount.h. Only a DreamSeeker-style launch uses the pager's signed-in account;
    // --connect and --state-dir are guests unless --account names one.
    if (cp.isSet(oIdent)) qputenv("FUGUE_IDENTITY", cp.value(oIdent).toUtf8());  // a path, not a secret
#ifdef FUGUE_RELEASE
    const bool testHook = false;  // test hooks are compiled out of a release build
#else
    const bool testHook =
        qEnvironmentVariableIsSet("FUGUE_AUTO_PICK") || qEnvironmentVariableIsSet("FUGUE_AUTO_RIGHTCLICK");
#endif
    const fugue::pager::AccountRequest areq = fugue::pager::accountRequestFor(asDreamSeeker, ds, fopts, testHook);
    if (areq.accountGiven != cp.isSet(oAccount) ||
        areq.account != cp.value(oAccount).toStdString()) {  // the same parse, or no run
        std::fprintf(stderr,
                     "fugue-qt: internal error: --account read differently by QCommandLineParser and "
                     "pager::accountRequestFor\n");
        return 2;
    }
    fugue::pager::AccountDecision ad = fugue::pager::decideAccount(areq);
    if (ad.kind == fugue::pager::AccountDecision::Fail) {
        std::fprintf(stderr, "account login: %s\n", ad.reason.c_str());
        return 2;
    }
    if (ad.kind == fugue::pager::AccountDecision::Account) {
        cfg.accountRecord = ad.login->record;
        cfg.accountLabel = fugue::account::ckey(ad.account);
        std::fprintf(stderr, "[app] account %s%s: identity from %s\n", cfg.accountLabel.c_str(),
                     ad.fromPager ? " (signed in through the pager)" : "", ad.login->source.c_str());
        // Hub-ready for every account login: a hub server answers the Login with a CertChallenge and initialises the
        // world only after our hub-minted CertAnswer (pager/hubcert.h). No secret is logged.
        std::string hwhy;
        if (fugue::pager::wireHubCert(cfg, *ad.login, &hwhy))
            std::fprintf(stderr,
                         "[app] hub-registered servers: a login-cert challenge (CertChallenge) is answered with a "
                         "hub-minted token\n");
        else
            std::fprintf(stderr, "[app] hub-registered servers will NOT initialise the world for this account: %s\n",
                         hwhy.c_str());
    } else {
        std::fprintf(stderr, "[app] %s\n", ad.reason.c_str());
    }

    SessionWorker worker(cfg);
    fugue::shell::BrowseHost::setStorageDir(
        QString::fromStdString((cfg.stateDir.empty() ? fugue::net::defaultStateDir() : cfg.stateDir) + "/web"));
    fugue::shell::SkinWindow win;
    worker.setMapMetrics(win.mapMetrics());
    worker.setMapMailbox(win.mapMailbox());
    worker.setMapRoutes(win.mapRoutes());
    fugue::shell::FocusFilter filt;
    filt.setClientWindow(&win);
    app.installEventFilter(&filt);

    worker.setLiveSkin(win.live());  // wire skin / winset / winget traffic drives the SAME live skin the window paints
    worker.setRscStore(win.rscStore());
    worker.setAudioSpec(cp.value(oAudio).toStdString());  // sound output (audio/sink.h)
    auto applyMacros = [&] { fugue::shell::applySkinMacros(win, filt); };
    auto loadSkin = [&](const std::string& dmf, const char* why) {
        QString e = win.loadSkin(dmf);
        if (!e.isEmpty()) {
            std::fprintf(stderr, "[app] skin (%s) rejected: %s\n", why, e.toUtf8().constData());
            return false;
        }
        std::fprintf(stderr, "[app] skin loaded (%s): %d controls\n%s", why, win.controlCount(),
                     win.describe().toUtf8().constData());
        applyMacros();
        return true;
    };
    loadSkin(fugue::app::kDefaultSkin, "built-in default (byond.dmf)");
    win.markBuiltinSkin();
    win.resize(win.designSize());
    bool skinSized = false;
    if (cp.isSet(oSize)) fugue::shell::applyUserSize(win, cp.value(oSize), skinSized);

    // ---- input: the GUI thread posts each action to the session thread; it never writes to the socket.
    auto selectMacroSet = [&](const std::string& name) {
        auto t = win.macros();
        auto it = t.sets.find(name);
        if (it == t.sets.end()) {
            std::fprintf(stderr, "[app] macro set \"%s\" not in this skin\n", name.c_str());
            return;
        }
        filt.setMacros(it->second);
        std::fprintf(stderr, "[app] macro set -> %s\n", name.c_str());
    };
    std::function<void(const std::string&, const std::string&, bool)> routeAndRunRef;
    // A client-side winset goes through the same live skin as a wire winset.
    auto clientWinset = [&](const std::string& body) {
        fugue::skin::WinsetResult r = win.live()->clientWinset(body);  // evaluates `cond ? a : b`
        win.syncFromLive();
        std::fprintf(stderr, "[app] .winset \"%s\" -> applied %zu, unhandled %zu%s\n", body.c_str(), r.applied.size(),
                     r.unhandled.size(),
                     r.unhandled.empty()
                         ? ""
                         : (" [" + r.unhandled.front() + (r.unhandled.size() > 1 ? ", ..." : "") + "]").c_str());
        for (const std::string& c : r.commands)
            if (routeAndRunRef) routeAndRunRef(c, "", false);
    };
    QString worldTitle;  // world.name, not the skin/window title
    const QString shotDir =
        QString::fromStdString((cfg.stateDir.empty() ? fugue::net::defaultStateDir() : cfg.stateDir) + "/screenshots");
    auto takeScreenshot = [&](bool autoSave) {
        QString error;
        auto* map = dynamic_cast<fugue::shell::MapWidget*>(win.map());
        // Read the composed native texture before opening a dialog. A queued
        // command keeps the synchronous QRhi readback outside frame recording.
        const QImage shot = map ? map->screenshot(&error) : QImage();
        if (shot.isNull()) {
            if (error.isEmpty()) error = "no map control to capture";
            win.appendChat({"<font color=\"red\">Could not capture screenshot: " + error.toHtmlEscaped() + "</font>"});
            return;
        }
        QString path;
        if (autoSave) {
            path = fugue::shell::screenshotAutoPath(shotDir, worldTitle, QDateTime::currentDateTime());
        } else {
            QDir().mkpath(shotDir);
            path = fugue::shell::withPngSuffix(QFileDialog::getSaveFileName(
                &win, "Save Screenshot As", QDir(shotDir).filePath("scrnshot1.png"), "PNG Files (*.png)"));
            if (path.isEmpty()) return;
        }
        const bool ok = fugue::shell::saveScreenshot(shot, path, &error);
        if (auto* rec = worker.recorder())
            rec->marker("screenshot", "\"file\":" + fugue::model::Recorder::quote(path.toStdString()) + ",\"w\":" +
                                          std::to_string(shot.width()) + ",\"h\":" + std::to_string(shot.height()) +
                                          ",\"ok\":" + (ok ? "true" : "false"));
        if (!ok)
            win.appendChat({"<font color=\"red\">Could not save screenshot to " + path.toHtmlEscaped() + ": " +
                            error.toHtmlEscaped() + "</font>"});
        else if (autoSave)
            win.appendChat({"<font color=\"red\">Saved screenshot to " + path.toHtmlEscaped() + "</font>"});
    };
    // The player's own macros (skin/usermacros.h), kept under the state dir: one list per server address, and a global one.
    const std::string macroDir = cfg.stateDir.empty() ? fugue::net::defaultStateDir() : cfg.stateDir;
    std::string gameMacrosPath = fugue::skin::userMacrosPath(macroDir, fugue::skin::worldKeyFor(cfg.host, cfg.port));
    const std::string globalMacrosPath = fugue::skin::userMacrosPath(macroDir, "");
    fugue::skin::MacroSet userGame = fugue::skin::loadUserMacros(gameMacrosPath),
                          userGlobal = fugue::skin::loadUserMacros(globalMacrosPath);
    filt.setUserMacros(userGame, userGlobal);
    std::fprintf(stderr, "[app] user macros: %zu for this game (%s), %zu global (%s)\n", userGame.entries.size(),
                 gameMacrosPath.c_str(), userGlobal.entries.size(), globalMacrosPath.c_str());
    QPointer<fugue::shell::MacroDialog> macroDlg;
    QPointer<fugue::shell::OptionsWindow> optionsWin;
    auto openMacros = [&] {
        if (macroDlg) {
            macroDlg->raise();
            macroDlg->activateWindow();
            return;
        }
        auto* d = new fugue::shell::MacroDialog(userGlobal, userGame, true, &win);
        d->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(d, &QDialog::accepted, &win, [&, d] {
            using fugue::skin::formatUserMacros;
            bool gameChanged = formatUserMacros(d->gameSet()) != formatUserMacros(userGame);
            bool globalChanged = formatUserMacros(d->globalSet()) != formatUserMacros(userGlobal);
            userGame = d->gameSet();
            userGlobal = d->globalSet();
            filt.setUserMacros(userGame, userGlobal);
            std::string e1, e2;  // only a changed list is written
            bool ok = (!gameChanged || fugue::skin::saveUserMacros(gameMacrosPath, userGame, &e1)) &&
                      (!globalChanged || fugue::skin::saveUserMacros(globalMacrosPath, userGlobal, &e2));
            std::fprintf(stderr, "[app] user macros saved: %zu for this game, %zu global%s%s\n",
                         userGame.entries.size(), userGlobal.entries.size(),
                         ok ? "" : " - SAVE FAILED: ", (e1 + e2).c_str());
        });
        macroDlg = d;
        d->open();
    };
    auto openOptions = [&] {
        if (!optionsWin) {
            optionsWin = new fugue::shell::OptionsWindow(&win);
            QObject::connect(optionsWin, &fugue::shell::OptionsWindow::command, &win,
                             [&](const QString& c) { routeAndRunRef(c.toStdString(), "", false); });
        }
        optionsWin->show();
        optionsWin->raise();
        optionsWin->activateWindow();
    };
    // An embedded winget in a command (`[[map.mouse-pos]]`): the live skin's answer, and the pointer for mouse-pos,
    // which is a GUI fact the skin does not hold.
    const fugue::shell::SkinLookup skinLookup = [&](const std::string& control, const std::string& param,
                                                    std::string& out) {
        if (param == "mouse-pos" || param == "inner-mouse-pos" || param == "outer-mouse-pos") {
            const fugue::skin::Control* c = win.tree().byName(control);
            QWidget* w = c ? win.controlWidget(QString::fromStdString(c->window + "." + c->name)) : nullptr;
            if (!w) return false;
            const QPoint q = w->mapFromGlobal(QCursor::pos());
            out = std::to_string(q.x()) + "," + std::to_string(q.y());
            return true;
        }
        return win.live() && win.live()->winget(control, param, out);
    };
    // A line typed into the input bar is the player's text, not a skin command: it gets no embedded winget.
    auto routeAndRun = [&](const std::string& command, const std::string& key, bool up, bool fromWorld = false,
                           bool typed = false) {
        const fugue::shell::SkinLookup none;
        const fugue::shell::SkinLookup& look = typed ? none : skinLookup;
        fugue::shell::Route r = fromWorld ? fugue::shell::routeWorldCommand(command, look)
                                          : fugue::shell::routeCommand(command, key, up, look);
        using R = fugue::shell::Route;
        switch (r.kind) {
            case R::Swallow: break;
            case R::Command: worker.postCommand(QString::fromStdString(r.text)); break;
            // A skin's `.north`...`.southwest` and `.center` are the stock move: one sub-0 command per firing.
            case R::Move: worker.postStockMove(r.dir); break;
            case R::SetMacroSet:
                if (!r.winsetBody.empty())
                    clientWinset(r.winsetBody);
                else
                    selectMacroSet(r.text);
                break;
            case R::Winset: clientWinset(r.winsetBody.empty() ? r.text : r.winsetBody); break;
            case R::Quit: QTimer::singleShot(0, &app, [&] { app.quit(); }); break;
            case R::Reconnect: worker.requestReconnect(); break;
            case R::Screenshot:
                QTimer::singleShot(0, &win, [&, autoSave = r.text == "auto"] { takeScreenshot(autoSave); });
                break;
            case R::Macros: openMacros(); break;
            case R::Options: openOptions(); break;
            case R::Output: {
                if (win.clientOutput(QString::fromStdString(r.text), QString::fromStdString(r.arg))) {
                } else if (win.live() && win.live()->printsOutput(r.text)) {
                    win.appendChat({QString::fromStdString(r.arg).toHtmlEscaped()});
                } else {
                    std::fprintf(stderr, "[app] .output to \"%s\": not an output or a page call - dropped\n",
                                 r.text.c_str());
                }
                break;
            }
            case R::Unsupported:
                std::fprintf(stderr, "[app] client command not supported (ignored, never sent as a verb): %s\n",
                             r.text.c_str());
                break;
        }
    };
    routeAndRunRef = [&](const std::string& c, const std::string& k, bool up) { routeAndRun(c, k, up); };
    QObject::connect(&worker, &SessionWorker::worldCommand, &win, [&](const QString& c) {
        std::fprintf(stderr, "[app] world winset command => %s\n", c.left(160).toUtf8().constData());
        for (const std::string& line : fugue::shell::macroCommandLines(c.toStdString())) routeAndRun(line, "", false);
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::macroChanged, &win, [&](const QString& name) {
        std::fprintf(stderr, "[app] active macro set -> %s\n", name.toUtf8().constData());
        applyMacros();
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::buttonCommand, &win, [&](const QString& c) {
        std::fprintf(stderr, "[app] BUTTON => %s\n", c.toUtf8().constData());
        routeAndRun(c.toStdString(), "", false);
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::menuCommand, &win, [&](const QString& c) {
        std::fprintf(stderr, "[app] MENU => %s\n", c.toUtf8().constData());
        routeAndRun(c.toStdString(), "", false);
    });
    // The skin's on-size command runs (with [[inner-size]] filled in) whenever the client area changes.
    QObject::connect(&win, &fugue::shell::SkinWindow::clientResized, &win, [&](const QSize& cl) {
        std::string cmd = win.live()->onSizeCommand();
        if (cmd.empty()) return;
        cmd = fugue::skin::expandSkinParams(cmd, cl.width(), cl.height());
        std::fprintf(stderr, "[app] on-size => %s\n", cmd.c_str());
        routeAndRun(cmd, "", false);
    });
    QObject::connect(&worker, &SessionWorker::loggedIn, &win, [&](const QString& err) {
        std::fprintf(stderr, "[app] %s\n",
                     err.isEmpty() ? "logged in" : ("LOGIN FAILED: " + err.toStdString()).c_str());
        if (!err.isEmpty()) QTimer::singleShot(0, &app, [&] { app.exit(3); });
    });
    QObject::connect(&worker, &SessionWorker::skinLoaded, &win, [&] {
        fugue::shell::worldSkinLoaded(win, filt, skinSized);
        std::fprintf(stderr, "[app] skin loaded (from world): %d controls\n%s", win.controlCount(),
                     win.describe().toUtf8().constData());
    });
    QObject::connect(&worker, &SessionWorker::chatLines, &win, &fugue::shell::SkinWindow::appendChat);
    // INFO control: tab, row and verb clicks are posted to the session thread.
    QObject::connect(&worker, &SessionWorker::statView, &win, &fugue::shell::SkinWindow::setStatView);
    QObject::connect(&worker, &SessionWorker::mapViewSize, &win, &fugue::shell::SkinWindow::setMapViewSize);
    QObject::connect(&worker, &SessionWorker::hoverName, &win, &fugue::shell::SkinWindow::setHoverName);
    QObject::connect(&worker, &SessionWorker::mapFrameAvailable, &win, &fugue::shell::SkinWindow::mapFrameAvailable);
    QObject::connect(&win, &fugue::shell::SkinWindow::mapPointer, &win,
                     [&](const fugue::shell::MapPointer& p) { worker.postMapPointer(p); });
    QObject::connect(&win, &fugue::shell::SkinWindow::mapMenuPicked, &win,
                     [&](quint64 token, int index) { worker.postMapMenuPick(token, index); });
    QObject::connect(&worker, &SessionWorker::mapMenuReady, &win, &fugue::shell::SkinWindow::showMapMenu);
#ifdef FUGUE_RELEASE
    const QString autoPick;  // test hook, compiled out of release builds
#else
    QString autoPick = qEnvironmentVariable("FUGUE_AUTO_PICK");
#endif
    QObject::connect(&worker, &SessionWorker::mapMenuReady, &win, [&](const fugue::shell::MapMenu& menu) {
        if (!menu.token) return;  // not a right click: the old world's popup being closed
        QStringList names;
        for (const auto& it : menu.items) names << it.label;
        std::printf("VERBMENU atom=\"%s\" n=%d: %s\n",
                    menu.items.isEmpty() ? "" : menu.items.front().atom.toUtf8().constData(), int(menu.items.size()),
                    names.join(" | ").toUtf8().constData());
        std::fflush(stdout);
        for (int i = 0; !autoPick.isEmpty() && i < menu.items.size(); ++i) {
            if (menu.items[i].label != autoPick) continue;
            std::fprintf(stderr, "[app] AUTO-PICK %s\n", autoPick.toUtf8().constData());
            QTimer::singleShot(0, &win, [&, token = menu.token, i] {
                worker.postMapMenuPick(token, i);
                win.showMapMenu({});  // closes the popup the pick stands for
            });
            break;
        }
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::statTabRequested, &win, [&](int i) { worker.postStatTab(i); });
    QObject::connect(&win, &fugue::shell::SkinWindow::statRowClicked, &win,
                     [&](int r, const QString& control, const QString& button, int mods, bool dbl, quint64 generation,
                         bool icon, int iconX, int iconY) {
                         worker.postStatRow(r, control.toStdString(), button.toStdString(), mods, dbl, generation, icon,
                                            iconX, iconY);
                     });
    QObject::connect(&win, &fugue::shell::SkinWindow::statRowHovered, &win,
                     [&](int r, const QString& control, int mods, quint64 generation, bool icon, int iconX, int iconY) {
                         worker.postStatHover(r, control.toStdString(), mods, generation, icon, iconX, iconY);
                     });
    QObject::connect(&win, &fugue::shell::SkinWindow::statHoverLeft, &win, [&] { worker.postStatHoverLeft(); });
    // A stat-panel verb runs by its command name (`Set-Default-Language`), its nullable arguments prompted.
    QObject::connect(&win, &fugue::shell::SkinWindow::statVerbClicked, &win, [&](const QString& v) {
        worker.postCommand(QString::fromStdString(fugue::net::verbCommandName(v.toStdString())), true);
    });
    // alert()/input() go through one PromptHost (shell/prompthost.h). Every request ends in exactly one reply.
    fugue::shell::PromptHost prompts(&win);
    QObject::connect(&worker, &SessionWorker::promptRequested, &prompts, &fugue::shell::PromptHost::present);
    QObject::connect(&worker, &SessionWorker::promptWithdrawn, &prompts, &fugue::shell::PromptHost::withdraw);
    QObject::connect(&prompts, &fugue::shell::PromptHost::accepted, &win,
                     [&](quint32 id, bool alert, const QString& t) { worker.postPromptAnswer(id, alert, t); });
    QObject::connect(&prompts, &fugue::shell::PromptHost::cancelled, &win,
                     [&](quint32 id, bool alert) { worker.postPromptCancel(id, alert); });
    QObject::connect(&prompts, &fugue::shell::PromptHost::argAccepted, &win,
                     [&](quint32 id, const QString& t) { worker.postArgAnswer(id, t); });
    QObject::connect(&prompts, &fugue::shell::PromptHost::argCancelled, &win,
                     [&](quint32 id) { worker.postArgCancel(id); });
    QObject::connect(&prompts, &fugue::shell::PromptHost::argNone, &win, [&](quint32 id) { worker.postArgNone(id); });
    QObject::connect(&worker, &SessionWorker::browseHtml, &win, &fugue::shell::SkinWindow::showBrowse);
    QObject::connect(&worker, &SessionWorker::browseClosed, &win, &fugue::shell::SkinWindow::closeBrowse);
    QObject::connect(&worker, &SessionWorker::outputFile, &win, &fugue::shell::SkinWindow::showOutputFile);
    QObject::connect(&worker, &SessionWorker::pageCall, &win, &fugue::shell::SkinWindow::pageCall);
    QObject::connect(&worker, &SessionWorker::worldName, &win, [&](const QString& name) {
        worldTitle = name;
        win.setWorldName(name);
    });
    // Window icon: the world's, else Fugue's (skin/winicon.h).
    QObject::connect(&worker, &SessionWorker::windowIcon, &win, &fugue::shell::SkinWindow::setSkinIcon);
    win.setSpaceCompleter([&](const QString& line, QString& out) { return worker.spaceCompletion(line, out); });
    QObject::connect(&win, &fugue::shell::SkinWindow::commandEntered, &win, [&](const QString& t) {
        std::fprintf(stderr, "[app] INPUT \"%s\"\n", t.toUtf8().constData());
        if (!t.trimmed().isEmpty()) routeAndRun(t.toStdString(), "", false, false, true);
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::worldCommand, &win, [&](const QString& t) {
        std::fprintf(stderr, "[app] on-close \"%s\"\n", t.toUtf8().constData());
        routeAndRun(t.toStdString(), "", false, true);
    });
    QObject::connect(&win, &fugue::shell::SkinWindow::byondLink, &win, [&](const QUrl& u) {
        std::fprintf(stderr, "[app] TOPIC %s\n", u.toString().left(200).toUtf8().constData());
        worker.postTopic(QString::fromUtf8(u.toEncoded()));  // raw encoding: toString() would decode %20 and friends
    });
    // A link() the world pushes joins the target in this window (SessionWorker). A clicked one opens a second client, as
    // stock DreamSeeker does; a page can click without the player, so one window opens at most one more, and that one
    // opens none.
    const std::string argv0 = argv[0];
    gLinkSpawned = qEnvironmentVariableIsSet("FUGUE_LINK_CLIENT");
    auto followWorld = [&](const QString& url) {
        std::string why;
        auto args = fugue::pager::worldLinkArgs(preQtArgs, hostTaken, asDreamSeeker, url.toStdString(), &why);
        std::fprintf(stderr, "[app] world link %s: %s\n", url.left(200).toUtf8().constData(),
                     !args ? why.c_str() : "opening it in a second client");
        if (!args) {
            win.appendChat(QStringList{"<font color=\"red\">" +
                                       ("Link not followed: " + QString::fromStdString(why)).toHtmlEscaped() +
                                       "</font>"});
            return;
        }
        if (gLinkSpawned) {
            win.appendChat(QStringList{
                "<font color=\"red\">Link not followed: a link already opened a second game window.</font>"});
            return;
        }
        gLinkSpawned = true;
        if (!fugue::pager::runSelf(argv0, *args, true, &why, {"FUGUE_LINK_CLIENT=1"}))
            std::fprintf(stderr, "[app] %s\n", why.c_str());
    };
    // The player's per-server macro list follows the window to a linked server, and stays across a link to the same one.
    fugue::shell::ServerKey serverKey(QString::fromStdString(cfg.host), cfg.port);
    QObject::connect(&worker, &SessionWorker::worldAddress, &serverKey, &fugue::shell::ServerKey::moved);
    QObject::connect(&serverKey, &fugue::shell::ServerKey::changed, &win, [&](const QString& host, quint16 port) {
        gameMacrosPath = fugue::skin::userMacrosPath(macroDir, fugue::skin::worldKeyFor(host.toStdString(), port));
        userGame = fugue::skin::loadUserMacros(gameMacrosPath);
        filt.setUserMacros(userGame, userGlobal);
        std::fprintf(stderr, "[app] user macros: %zu for this game (%s)\n", userGame.entries.size(),
                     gameMacrosPath.c_str());
    });
    QObject::connect(&serverKey, &fugue::shell::ServerKey::kept, &win, [&](const QString& host, quint16 port) {
        std::fprintf(stderr, "[app] user macros: %s:%u is the same server, kept %s\n", host.toUtf8().constData(), port,
                     gameMacrosPath.c_str());
    });
    // A byond:// link opened while the game runs is a clicked link.
    auto openLink = [&](const QUrl& u) { followWorld(u.toString(QUrl::FullyEncoded)); };
#ifdef Q_OS_MACOS
    fugue::shell::UrlOpener urlOpener(&app, openLink);
#else
    (void)openLink;
#endif
    QObject::connect(&win, &fugue::shell::SkinWindow::worldLink, &win, [&](const QString& u) { followWorld(u); });
    QObject::connect(&worker, &SessionWorker::externalLink, &win, [](const QString& u) {
        if (!QDesktopServices::openUrl(QUrl(u, QUrl::TolerantMode)))
            std::fprintf(stderr, "[app] no handler opened %s\n", u.left(200).toUtf8().constData());
    });
    QObject::connect(&filt, &fugue::shell::FocusFilter::macroFired, &win,
                     [&](const QString& k, const QString& c, bool up) {
                         std::fprintf(stderr, "[app] MACRO %s%s => %s\n", k.toUtf8().constData(), up ? "+UP" : "",
                                      c.toUtf8().constData());
                         for (const std::string& line : fugue::shell::macroCommandLines(c.toStdString()))
                             routeAndRun(win.expandMapCommandParams(line), k.toStdString(), up);
                     });
    QObject::connect(&worker, &SessionWorker::frameTiming, &filt, &fugue::shell::FocusFilter::setFrameTiming);
    // Refusals are printed in red, as stock DreamSeeker does. The font tag also makes the pane parse the line as rich
    // text; a bare escaped line would show as plain text.
    QObject::connect(&worker, &SessionWorker::localOutput, &win, [&](const QString& l) {
        win.appendChat(QStringList{"<font color=\"red\">" + l.toHtmlEscaped() + "</font>"});
    });

    win.show();
    worker.start();

#ifndef FUGUE_RELEASE
    // Dev hook: FUGUE_AUTO_RIGHTCLICK="x,y[,delaySec]" sends a right click at map-image pixel x,y after the delay
    // (default 8 s). With FUGUE_AUTO_PICK=<verb name> it drives the right-click chain without a pointer.
    if (QString ar = qEnvironmentVariable("FUGUE_AUTO_RIGHTCLICK"); !ar.isEmpty()) {
        QStringList p = ar.split(',');
        if (p.size() >= 2) {
            double delay = p.size() > 2 ? p[2].toDouble() : 8.0;
            int ix = p[0].toInt(), iy = p[1].toInt();
            QTimer::singleShot(int(delay * 1000), &win, [&win, ix, iy] {
                fugue::shell::MapWidget* mw = nullptr;  // the first map on screen with a scene: the default one
                for (QWidget* child : win.findChildren<QWidget*>())
                    if (auto* map = dynamic_cast<fugue::shell::MapWidget*>(child);
                        map && !mw && map->isVisible() && !map->presentedViewSize().isEmpty())
                        mw = map;
                if (!mw) {
                    std::fprintf(stderr, "[app] AUTO-RIGHTCLICK: no map scene yet\n");
                    return;
                }
                const QPoint local = mw->sceneToWidget(QPointF(ix + 0.5, iy + 0.5)).toPoint();
                std::fprintf(stderr, "[app] AUTO-RIGHTCLICK image (%d,%d) -> widget (%d,%d)\n", ix, iy, local.x(),
                             local.y());
                QMouseEvent move(QEvent::MouseMove, local, mw->mapToGlobal(local), Qt::NoButton, Qt::NoButton,
                                 Qt::NoModifier);
                QCoreApplication::sendEvent(mw, &move);
                QMouseEvent press(QEvent::MouseButtonPress, local, mw->mapToGlobal(local), Qt::RightButton,
                                  Qt::RightButton, Qt::NoModifier);
                QCoreApplication::sendEvent(mw, &press);
                // The release goes out on a later turn of the event loop: the worker pairs it with the press it has seen.
                QTimer::singleShot(100, mw, [mw, local] {
                    QMouseEvent rel(QEvent::MouseButtonRelease, local, mw->mapToGlobal(local), Qt::RightButton,
                                    Qt::NoButton, Qt::NoModifier);
                    QCoreApplication::sendEvent(mw, &rel);
                });
            });
        }
    }
#endif  // !FUGUE_RELEASE

    // Quit through the event loop on SIGINT/SIGTERM so the socket closes cleanly.
#ifdef _WIN32
    ::signal(SIGINT, [](int) { gInt = 1; });
    ::signal(SIGTERM, [](int) { gInt = 1; });
#else
    struct sigaction sa{};
    sa.sa_handler = [](int) { gInt = 1; };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#endif
    QTimer intPoll;
    QObject::connect(&intPoll, &QTimer::timeout, &app, [&] {
        if (gInt) app.quit();
    });
    intPoll.start(100);
    int rc = app.exec();
    worker.requestStop();
    if (!worker.wait(4000)) {
        std::fprintf(stderr, "[app] session thread did not stop; terminating\n");
        worker.terminate();
        worker.wait(1000);
    }
    return rc;
}

// fugue-pager-shot <outdir>: renders each screen of the pager window offscreen to PNGs and prints their paths.
// Forces QT_QPA_PLATFORM=offscreen and points HOME at a fresh temporary directory holding synthetic state only (a fake
// account, key.txt block, identity and a stub client). No network.
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include "platform/file.h"
#include "account/store.h"
#include "pager/ui/pagerwindow.h"

using namespace fugue;
using namespace fugue::pager;

static void put(const QString& path, const QByteArray& body) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(body) != body.size()) {
        std::fprintf(stderr, "cannot write %s: %s\n", qPrintable(path), qPrintable(f.errorString()));
        std::exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME"})
        qunsetenv(v);
    QApplication app(argc, argv);
    QString out = argc > 1 ? argv[1] : ".";
    QDir().mkpath(out);

    put(home.path() + "/bin/fugue", "#!/bin/sh\n");
    if (platform::chmod((home.path() + "/bin/fugue").toUtf8().constData(), 0755) != 0) {
        std::perror("chmod synthetic client");
        return EXIT_FAILURE;
    }
    qputenv("FUGUE_CLIENT", (home.path() + "/bin/fugue").toUtf8());
    put(home.path() + "/.byond/cfg/key.txt", "BEGIN KEY Fake Tester\nsynthetic-line-1\n`\nEND KEY\n");

    account::Config cfg;
    cfg.servers = account::defaultServers();
    account::Server s;
    s.name = "Friday night box";
    s.addr = "10.0.0.5:6000";
    s.lastPlayed = account::Timestamp{1760000000, 0};
    s.lastPrefetch = account::Timestamp{1759000000, 0};
    cfg.servers.push_back(s);

    auto snap = [&](PagerWindow& w, const char* name) {
        w.show();
        app.processEvents();
        QString p = out + "/" + name + ".png";
        w.grab().save(p);
        std::printf("%s\n", p.toUtf8().constData());
    };
    auto snapDialog = [&](QWidget* d, const char* name) {
        app.processEvents();
        QString p = out + "/" + name + ".png";
        d->grab().save(p);
        std::printf("%s\n", p.toUtf8().constData());
    };

    // 1. Signed out: the pager, its Sign in window, the plain error texts and the busy state.
    account::Config guest = cfg;
    {
        PagerWindow w;
        w.setState(guest, check(guest));
        snap(w, "pager-1-signed-out");
        w.showSignIn();
        snapDialog(w.signInDialog(), "pager-1b-sign-in-window");
        w.setSignInError("Wrong username or password.");
        snapDialog(w.signInDialog(), "pager-2-sign-in-wrong-password");
        w.setSignInError("Could not reach BYOND. Check your internet connection and try again.");
        snapDialog(w.signInDialog(), "pager-3-sign-in-unreachable");
        w.submitSignIn("Fake Tester", "not-a-real-password");
        w.setSigningIn(true);
        snapDialog(w.signInDialog(), "pager-4-signing-in");
    }
    // 2. guest: the server list; the hub server says why it cannot be joined, in plain words
    {
        PagerWindow w;
        w.setGuestChosen(true);
        w.setState(guest, check(guest));
        snap(w, "pager-5-guest-servers");
        w.submitAddServer("Some.World");
        snap(w, "pager-6-add-server-error");
    }
    // 3. signed in (key.txt block + identity): every server joinable
    cfg.account = "Fake Tester";
    account::saveAccount(cfg.account, account::Account{{1, 2, 3}, 0x262781, 1});
    {
        PagerWindow w;
        w.setState(cfg, check(cfg));
        snap(w, "pager-7-signed-in-servers");
        w.selectBookmark(0);
        QMetaObject::invokeMethod(w.findChild<QListWidget*>("servers"), "itemClicked",
                                  Q_ARG(QListWidgetItem*, w.findChild<QListWidget*>("servers")->item(0)));
        snap(w, "pager-7b-game-tab");
        w.showSignIn();  // signed in already: the window offers the saved sign-in, no password
        snapDialog(w.signInDialog(), "pager-7c-sign-in-saved");
        w.signInDialog()->hide();
        {  // the gear menu, popped at the gear button
            QMenu* m = w.gearMenu();
            m->popup(w.mapToGlobal(QPoint(w.width() - 20, 30)));
            app.processEvents();
            QString p = out + "/pager-7d-gear-menu.png";
            m->grab().save(p);
            std::printf("%s\n", p.toUtf8().constData());
            m->hide();
        }
        {  // Preferences + the Clear cache confirmation (synthetic cache: one resource file)
            put(home.path() + "/.fugue/rsc/ab/abcdef01", QByteArray(150000, 'x'));
            w.showPreferences();
            app.processEvents();
            QString p = out + "/pager-7e-preferences.png";
            w.preferencesDialog()->grab().save(p);
            std::printf("%s\n", p.toUtf8().constData());
            w.setConfirmHook([&](const QString& title, const QString& text) {
                QMessageBox box(QMessageBox::Warning, title, "Remove these cached files?",
                                QMessageBox::Yes | QMessageBox::No, &w);
                box.setInformativeText(text);
                box.setDefaultButton(QMessageBox::No);
                box.show();
                app.processEvents();
                QString q = out + "/pager-7f-clear-cache-confirm.png";
                box.grab().save(q);
                std::printf("%s\n", q.toUtf8().constData());
                return false;
            });
            w.requestClearCache();
            w.preferencesDialog()->hide();
        }
        w.setClientRunning(true);
        snap(w, "pager-8-game-running");
        w.setClientRunning(false);
        w.setStatus("The game closed unexpectedly. Open Details to see why.", true);
        w.appendLog("[pager] client exited (1)");
        snap(w, "pager-9-game-closed");
        // 4. Details: the technical half.
        w.showTab(PagerWindow::SetupTab);
        snap(w, "pager-10-details-setup");
        w.showTab(PagerWindow::LogTab);
        snap(w, "pager-12-details-log");
    }
    return 0;
}

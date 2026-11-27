// pager_ui: the window draws the model; sign-in, guest, join and add-server are signals.
// Technical parts sit under Details. Runs offscreen with a temp HOME.
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include <algorithm>
#include <functional>
#include <future>
#ifndef WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include "platform/file.h"
#include "pager/launch.h"
#include "pager/ui/instance.h"
#include "pager/ui/pagerwindow.h"

using namespace fugue;
using namespace fugue::pager;

class PagerUi : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
private slots:
    void initTestCase() {
        qputenv("HOME", home_.path().toUtf8());
        for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME"})
            qunsetenv(v);
        QDir().mkpath(home_.path() + "/bin");
        QFile f(home_.path() + "/bin/fugue");
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
        QCOMPARE(f.write("x"), qint64(1));
        f.close();
        QCOMPARE(platform::chmod((home_.path() + "/bin/fugue").toUtf8().constData(), 0755), 0);
        qputenv("FUGUE_CLIENT", (home_.path() + "/bin/fugue").toUtf8());
    }
    void signedOutOpensOnSignInAndGuestIsOneClick() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setState(cfg, check(cfg));
        QCOMPARE(w.page(), PagerWindow::SignInPage);
        QVERIFY(!w.detailsVisible());  // nothing technical on screen
        // the Home row carries no username/password fields: signing in is the separate Sign in window
        QVERIFY(w.findChild<QLabel*>("notsignedin"));
        QVERIFY(!w.signInVisible());
        for (QLineEdit* e : w.findChildren<QLineEdit*>())
            QVERIFY2(e->window() != &w || e->echoMode() != QLineEdit::Password, qPrintable(e->objectName()));
        QSignalSpy guest(&w, &PagerWindow::guestRequested);
        w.findChild<QPushButton*>("guest")->click();
        QCOMPARE(guest.count(), 1);
        w.setState(cfg, check(cfg));  // what the launcher does after saving
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        QCOMPARE(w.accountText(), QString("Playing as a guest"));
        QCOMPARE(w.findChild<QPushButton*>("account")->text(), QString("Sign in"));
        w.findChild<QPushButton*>("account")->click();
        QVERIFY(w.signInVisible());
        QVERIFY(w.signInDialog()->windowModality() != Qt::NonModal);
        // Cancel leaves the guest a guest
        w.signInDialog()->findChild<QPushButton*>("signincancel")->click();
        QVERIFY(!w.signInVisible());
        QCOMPARE(w.page(), PagerWindow::ServersPage);
    }
    // The Sign in window follows stock BYOND 516's login dialog: Account, Password, Get an account / Forgot password?
    // links, an automatic sign-in box, and no "Save password" (Fugue never keeps a password).
    void signInWindowEmitsAndSpeaksPlainly() {
        PagerWindow w;
        account::Config cfg;
        w.setState(cfg, check(cfg));
        w.showSignIn();
        QDialog* d = w.signInDialog();
        QVERIFY(d && d->isVisible());
        QVERIFY(d->findChild<QComboBox*>("username")->isEditable());
        QCOMPARE(d->findChild<QLineEdit*>("password")->echoMode(), QLineEdit::Password);
        QVERIFY(d->findChild<QLabel*>("getaccount")->text().contains("https://secure.byond.com/Join"));
        QVERIFY(d->findChild<QLabel*>("forgotpassword")->text().contains("command=forgot_password"));
        QVERIFY(d->findChild<QCheckBox*>("autosignin"));
        for (QCheckBox* c : d->findChildren<QCheckBox*>())
            QVERIFY2(!c->text().contains("password", Qt::CaseInsensitive), qPrintable(c->text()));
        QVERIFY(d->findChild<QPushButton*>("dlgguest"));
        QSignalSpy spy(&w, &PagerWindow::signInRequested);
        QSignalSpy saved(&w, &PagerWindow::savedSignInChosen);
        w.submitSignIn("", "");
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.signInErrorText(), QString("Enter your BYOND username and password."));
        w.submitSignIn("NobodySavedHere", "");  // no saved sign-in on this computer: a password is needed
        QCOMPARE(spy.count(), 0);
        QCOMPARE(saved.count(), 0);
        QCOMPARE(w.signInErrorText(), QString("Enter your BYOND username and password."));
        w.submitSignIn("  Some Key ", "hunter2");
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("Some Key"));
        QCOMPARE(spy.at(0).at(1).toString(), QString("hunter2"));
        w.setSigningIn(true);
        QVERIFY(!d->findChild<QPushButton*>("signin")->isEnabled());
        QVERIFY(!d->findChild<QPushButton*>("dlgguest")->isEnabled());
        QVERIFY(!w.findChild<QPushButton*>("guest")->isEnabled());
        w.setSigningIn(false);
        QVERIFY(d->findChild<QLineEdit*>("password")->text().isEmpty());  // never kept past one attempt
        w.setSignInError("Wrong username or password.");
        QCOMPARE(w.signInErrorText(), QString("Wrong username or password."));
        QVERIFY(d->isVisible());  // a failure keeps the window up, with the plain sentence in it
        QCOMPARE(w.page(), PagerWindow::SignInPage);
        // the dialog's own Play as guest closes it and asks for guest
        QSignalSpy guest(&w, &PagerWindow::guestRequested);
        d->findChild<QPushButton*>("dlgguest")->click();
        QCOMPARE(guest.count(), 1);
        QVERIFY(!d->isVisible());
    }
    void signedInShowsServersAndSignOut() {
        PagerWindow w;
        account::Config cfg;
        cfg.account = "UiTester";
        cfg.servers = account::defaultServers();
        account::Account a;
        a.identity = Bytes(8 + 13, 5);
        a.installId = 1;
        a.innerKey = 2;
        QVERIFY(account::saveAccount(cfg.account, a));
        w.setState(cfg, check(cfg));
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        QVERIFY(w.accountText().contains("UiTester"));
        QSignalSpy out(&w, &PagerWindow::signOutRequested);
        auto* b = w.findChild<QPushButton*>("account");
        QCOMPARE(b->text(), QString("Logout"));  // the account row's action label
        b->click();
        QCOMPARE(out.count(), 1);
        // an account saved but with no identity on this box asks for the password, with the name filled in
        account::Config other = cfg;
        other.account = "NoIdentityHere";
        PagerWindow w2;
        w2.setState(other, check(other));
        QCOMPARE(w2.page(), PagerWindow::SignInPage);
        w2.showSignIn();
        QCOMPARE(w2.signInDialog()->findChild<QComboBox*>("username")->currentText(), QString("NoIdentityHere"));
        QCOMPARE(w2.signInErrorText(), QString("Sign in again to play as NoIdentityHere."));
        QFile::remove(QString::fromStdString(account::identityPath("UiTester")));
    }
    // Sign in ONCE: a computer that holds an account's identity (Logout keeps it) signs back in with no password and no hub call.
    void savedSignInNeedsNoPassword() {
        QVERIFY(account::saveAccount("Saved One", account::Account{Bytes(21, 3), 1, 2}));
        const QString keyTxt = home_.path() + "/.fugue/key.txt";
        {
            QFile f(keyTxt);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("BEGIN KEY Saved One\nsynthetic\n`\nEND KEY\n");
        }
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setState(cfg, check(cfg));  // signed out (after a Logout): no account in pager.json
        w.showSignIn();
        auto* user = w.signInDialog()->findChild<QComboBox*>("username");
        QVERIFY(user->findText("savedone") >= 0);  // offered from this computer's saved sign-ins
        user->setEditText("Saved One");
        QVERIFY2(w.signInDialog()->findChild<QLabel*>("signinnote")->text().contains("no password needed"),
                 qPrintable(w.signInDialog()->findChild<QLabel*>("signinnote")->text()));
        QSignalSpy hub(&w, &PagerWindow::signInRequested);
        QSignalSpy saved(&w, &PagerWindow::savedSignInChosen);
        w.submitSignIn("Saved One", "");
        QCOMPARE(hub.count(), 0);
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.at(0).at(0).toString(), QString("Saved One"));
        QVERIFY(!w.signInVisible());
        cfg.account = "Saved One";  // what the launcher does with it
        w.setState(cfg, check(cfg));
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        // typing a password still signs in at BYOND (e.g. after a password change)
        w.submitSignIn("Saved One", "new-password");
        QCOMPARE(hub.count(), 1);
        w.closeSignIn();
        QVERIFY(!w.signInVisible());
        QFile::remove(keyTxt);
        QFile::remove(QString::fromStdString(account::identityPath("Saved One")));
    }
    // One "Sign in automatically when Fugue starts" setting, shared by the Sign in window and Preferences. Off, a saved
    // account is held in the Sign in window until the player confirms it.
    void autoSignInIsOnePreferenceAndHoldsTheAccount() {
        QVERIFY(account::saveAccount("Held One", account::Account{Bytes(21, 4), 1, 2}));
        const QString keyTxt = home_.path() + "/.fugue/key.txt";
        {
            QFile f(keyTxt);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("BEGIN KEY Held One\nsynthetic\n`\nEND KEY\n");
        }
        PagerWindow w;
        account::Config cfg;
        cfg.account = "Held One";
        cfg.servers = account::defaultServers();
        w.setAutoSignIn(false);
        w.setState(cfg, check(cfg));
        w.showPreferences();
        auto* pa = w.preferencesDialog()->findChild<QCheckBox*>("prefsautosignin");
        auto* da = w.signInDialog()->findChild<QCheckBox*>("autosignin");
        QVERIFY(!pa->isChecked() && !da->isChecked());
        QSignalSpy changed(&w, &PagerWindow::autoSignInChanged);
        da->setChecked(true);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toBool(), true);
        QVERIFY(pa->isChecked());
        pa->setChecked(false);
        QCOMPARE(changed.count(), 2);
        QVERIFY(!da->isChecked());
        // what Preferences says about the password, in plain words
        const QString acct = w.preferencesDialog()->findChild<QLabel*>("prefsaccount")->text();
        QVERIFY2(acct.contains("never saves your password") && acct.contains("sign in only once"), qPrintable(acct));
        w.preferencesDialog()->hide();
        // the launcher holds a saved account at start when the setting is off
        w.holdSignIn();
        QCOMPARE(w.page(), PagerWindow::SignInPage);
        QVERIFY(w.signInVisible());
        w.selectBookmark(1);
        QVERIFY(!w.joinEnabled());
        QSignalSpy saved(&w, &PagerWindow::savedSignInChosen);
        w.submitSignIn("Held One", "");
        QCOMPARE(saved.count(), 1);
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        QVERIFY(w.joinEnabled());
        QFile::remove(keyTxt);
        QFile::remove(QString::fromStdString(account::identityPath("Held One")));
    }
    void unsupportedControlsAreHidden() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        for (auto n : {"show", "published", "search", "livegames", "live"}) QVERIFY2(!w.findChild<QWidget*>(n), n);
        auto* left = w.findChild<QTabWidget*>("lefttabs");
        QCOMPARE(left->count(), 1);
        QCOMPARE(left->tabText(0), QString("Games"));
        auto* right = w.findChild<QTabWidget*>("right");
        for (int i = 0; i < right->count(); i++) QVERIFY(right->isTabEnabled(i) && right->tabText(i) != "Feed");
        for (QPushButton* b : w.findChildren<QPushButton*>())
            QVERIFY2(b->window() != &w || b->isEnabled() || b->objectName() == "join" ||
                         b->objectName() == "warm",  // both follow the selection
                     qPrintable(b->objectName() + " " + b->text()));
        for (QAction* x : w.gearMenu()->actions()) QVERIFY2(x->isSeparator() || x->isEnabled(), qPrintable(x->text()));
        w.show();
        w.showTab(PagerWindow::LogTab);  // Details has the Setup and Log tabs
        QVERIFY(w.findChild<QPlainTextEdit*>("log")->isVisible());
    }
    void joinIsASignalAndAGuestMayTryAHubServer() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        QCOMPARE(w.bookmarkCount(), 2);
        // sorted: never-played entries by lowercase name, the hub world before the local server
        w.selectBookmark(0);
        QVERIFY(w.joinEnabled());  // a hub server is not refused to a guest up front
        QVERIFY(w.joinBlockerText().isEmpty());
        w.selectBookmark(1);
        QVERIFY(w.joinEnabled());
        QVERIFY(w.joinBlockerText().isEmpty());
        QSignalSpy spy(&w, &PagerWindow::joinRequested);
        auto joins = w.findChildren<QPushButton*>("join");
        QCOMPARE(joins.size(), 1);  // one Join, on the selected game's tab
        w.selectBookmark(0);
        joins[0]->click();  // a guest joins a hub server too: the world decides whether it takes guests
        QCOMPARE(spy.count(), 1);
        auto hub = std::find_if(cfg.servers.begin(), cfg.servers.end(), [](const account::Server& s) { return s.hub; });
        QVERIFY(hub != cfg.servers.end());
        QCOMPARE(spy.at(0).at(0).toString(), QString::fromStdString(hub->addr));
        QCOMPARE(spy.at(0).at(1).toBool(), true);
        w.selectBookmark(1);
        joins[0]->click();
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toString(), QString("127.0.0.1:6000"));
        QCOMPARE(spy.at(1).at(1).toBool(), false);
        w.setClientRunning(true);
        QVERIFY(!w.joinEnabled());
        QCOMPARE(w.statusText(), QString("The game is running."));
        w.joinRow(1);
        QCOMPARE(spy.count(), 2);  // no second join while the game runs
    }
    // Download resources: a signal for the selected game (the caller runs the client's --prefetch pass), blocked like Join while a game runs.
    // While it runs the button reads Cancel download. Join stays usable (the caller stops the download first), and its progress shows in the game tab's note.
    void downloadResourcesIsASignalAndCanBeCancelledOrJoined() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        QSignalSpy warm(&w, &PagerWindow::warmRequested);
        QSignalSpy join(&w, &PagerWindow::joinRequested);
        QSignalSpy cancel(&w, &PagerWindow::warmCancelRequested);
        auto btns = w.findChildren<QPushButton*>("warm");
        QCOMPARE(btns.size(), 1);
        w.selectBookmark(1);
        w.setClientRunning(true);  // a game is running: blocked like Join
        QVERIFY(!w.warmEnabled());
        w.warmRow(1);
        QCOMPARE(warm.count(), 0);
        w.setClientRunning(false);
        QVERIFY(w.warmEnabled());
        QVERIFY2(w.gameNoteText().contains("not downloaded yet"), qPrintable(w.gameNoteText()));
        btns[0]->click();
        QCOMPARE(warm.count(), 1);
        QCOMPARE(warm.at(0).at(0).toString(), QString("127.0.0.1:6000"));
        w.setWarming(true, "Downloading resources: 10 of 983 files");
        QVERIFY(w.warmEnabled());
        QCOMPARE(btns[0]->text(), QString("Cancel download"));
        QVERIFY(w.joinEnabled());
        QCOMPARE(w.gameNoteText(), QString("Downloading resources: 10 of 983 files"));
        btns[0]->click();  // Cancel download: no second download
        QCOMPARE(cancel.count(), 1);
        QCOMPARE(warm.count(), 1);
        w.joinRow(1);  // Join while downloading: the caller stops the download, then joins
        QCOMPARE(join.count(), 1);
        QCOMPARE(warm.count(), 1);
        cfg.markPrefetched(cfg.servers[1]);  // what the caller does on success, then setState
        w.setState(cfg, check(cfg));
        w.setWarming(false, "Resources downloaded: 982 new files (7.1 MiB). Joining now starts with them.");
        QVERIFY(w.joinEnabled() && w.warmEnabled());
        QCOMPARE(btns[0]->text(), QString("Download resources"));
        w.selectBookmark(1);
        QVERIFY2(w.gameNoteText().startsWith("Resources downloaded "), qPrintable(w.gameNoteText()));
    }
    void addServerTakesAddressesAndRefusesPlainly() {
        PagerWindow w;
        account::Config cfg;
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        QSignalSpy spy(&w, &PagerWindow::serverAdded);
        QCOMPARE(
            w.submitAddServer("localhost"),
            QString(
                "That doesn't look like a server address. Type it like example.com:1234 or byond://example.com:1234."));
        // "x.y" is a world key's shape; a plain host missing its port looks the same, so the sentence covers both
        QCOMPARE(w.submitAddServer("example.com"), QString("That needs a port number, like example.com:1234. (BYOND "
                                                           "hub links like Name.World cannot be opened here yet.)"));
        QCOMPARE(w.submitAddServer("byond://Some.World"),
                 QString("That needs a port number, like example.com:1234. (BYOND hub links like Name.World cannot be "
                         "opened here yet.)"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.submitAddServer("byond://Example.com:1234/"), QString());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("Example.com:1234"));
        QCOMPARE(w.submitAddServer("127.0.0.1:47925"), QString());
        QCOMPARE(spy.at(1).at(0).toString(), QString("127.0.0.1:47925"));
    }
    // An added server is labelled once, by its address.
    void addedServerIsLabelledOnce() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        connect(&w, &PagerWindow::serverAdded, &w,
                [&](const QString& a) {  // as the launcher does: remember, save, redraw
                    account::Server s;
                    s.name = s.addr = a.toStdString();
                    cfg.remember(s);
                    w.setState(cfg, check(cfg));
                });
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.submitAddServer("byond://127.0.0.1:47925"), QString());
        QCOMPARE(w.bookmarkCount(), 3);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(50);
        auto* view = w.findChild<QListWidget*>("servers");
        QVERIFY(view->viewport()->findChildren<QWidget*>().isEmpty());
        int shown = 0;
        for (int i = 0; i < view->count(); i++) shown += view->item(i)->text() == "127.0.0.1:47925";
        QCOMPARE(shown, 1);
        // the add selected it, and its tab names it once and shows the address once
        QCOMPARE(view->item(view->currentRow())->text(), QString("127.0.0.1:47925"));
    }
    void technicalPartsLiveUnderDetails() {
        PagerWindow w;
        account::Config cfg;
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        w.show();
        QVERIFY(!w.findChild<QPlainTextEdit*>("checklist")->isVisible());
        QVERIFY(!w.findChild<QPlainTextEdit*>("log")->isVisible());
        w.showTab(PagerWindow::SetupTab);
        QVERIFY(w.detailsVisible());
        QVERIFY(w.findChild<QPlainTextEdit*>("checklist")->isVisible());
        w.setDetailsVisible(false);
        QVERIFY(!w.findChild<QPlainTextEdit*>("checklist")->isVisible());
    }
    // ---- the BYOND-shaped window list, game tab, gear menu, Preferences, Clear cache ----
    void doubleClickInTheListJoins() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        auto* view = w.findChild<QListWidget*>("servers");
        QSignalSpy spy(&w, &PagerWindow::joinRequested);
        // row 0 (a hub server) is blocked for a guest: a double-click does nothing
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, view->visualItemRect(view->item(0)).center());
        QCOMPARE(spy.count(), 0);
        // a single click opens the game's tab, named after it
        QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->visualItemRect(view->item(1)).center());
        QCOMPARE(w.gameTabTitle(), QString("local server"));
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, view->visualItemRect(view->item(1)).center());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QString("127.0.0.1:6000"));
        // closing the game tab returns to Home
        auto* right = w.findChild<QTabWidget*>("right");
        QCOMPARE(right->tabText(right->currentIndex()), QString("local server"));
        emit right->tabCloseRequested(right->currentIndex());
        QCOMPARE(w.gameTabTitle(), QString());
        QCOMPARE(right->currentIndex(), 0);
        QCOMPARE(right->tabText(0), QString("Home"));
    }
    void signedOutCannotJoinUntilSignInOrGuest() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setState(cfg, check(cfg));
        QCOMPARE(w.page(), PagerWindow::SignInPage);
        QCOMPARE(w.bookmarkCount(), 2);  // the list is on screen while signed out, as in BYOND
        w.selectBookmark(1);
        QVERIFY(!w.joinEnabled());
        QSignalSpy spy(&w, &PagerWindow::joinRequested);
        w.joinRow(1);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(w.statusText(), QString("Sign in, or choose Play as guest, to join a game."));
        w.findChild<QPushButton*>("guest")->click();
        w.setState(cfg, check(cfg));
        QVERIFY(w.joinEnabled());
    }
    void filterNarrowsTheList() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(cfg, check(cfg));
        QCOMPARE(w.visibleGameCount(), 2);
        w.setFilter("LOCAL SERV");
        QCOMPARE(w.visibleGameCount(), 1);
        w.setFilter("127.0.0");  // the address counts too
        QCOMPARE(w.visibleGameCount(), 1);
        w.setFilter("nothing like it");
        QCOMPARE(w.visibleGameCount(), 0);
        w.setFilter("");
        QCOMPARE(w.visibleGameCount(), 2);
    }
    void gearMenuHasBYONDsItemsAndLogoutSignsOut() {
        PagerWindow w;
        account::Config cfg;
        cfg.account = "MenuTester";
        cfg.servers = account::defaultServers();
        QVERIFY(account::saveAccount(cfg.account, account::Account{Bytes(21, 7), 1, 2}));
        w.setState(cfg, check(cfg));
        QStringList texts;
        for (QAction* x : w.gearMenu()->actions())
            if (!x->isSeparator()) texts << x->text();
        QCOMPARE(texts, QStringList({"About Fugue...", "Open Location...", "Logout", "Preferences...", "Show Resources",
                                     "Stay on Top", "Exit"}));
        for (auto n : {"openfile", "dreamdaemon", "dreammaker", "downloads", "updates"})
            QVERIFY2(!w.menuAction(n), n);  // not built in Fugue
        for (auto n : {"about", "openloc", "logout", "prefs", "resources", "ontop", "exit"})
            QVERIFY2(w.menuAction(n)->isEnabled(), n);
        QCOMPARE(w.menuAction("openloc")->shortcut(), QKeySequence("Ctrl+O"));
        QCOMPARE(w.menuAction("logout")->shortcut(), QKeySequence("Ctrl+L"));
        QCOMPARE(w.menuAction("prefs")->shortcut(), QKeySequence("Ctrl+P"));
        QCOMPARE(w.menuAction("exit")->shortcut(), QKeySequence("Ctrl+Q"));
        QSignalSpy out(&w, &PagerWindow::signOutRequested);
        w.menuAction("logout")->trigger();
        QCOMPARE(out.count(), 1);
        // Logout never deletes the identity (that is Clear cache's job)
        QVERIFY(QFile::exists(QString::fromStdString(account::identityPath("MenuTester"))));
        // a guest's menu offers Sign in instead
        account::Config g;
        g.servers = account::defaultServers();
        w.setGuestChosen(true);
        w.setState(g, check(g));
        QCOMPARE(w.menuAction("logout")->text(), QString("Sign in..."));
        w.menuAction("logout")->trigger();
        QVERIFY(w.signInVisible());  // a guest's "Sign in..." opens the Sign in window and changes nothing else
        QCOMPARE(w.page(), PagerWindow::ServersPage);
        QCOMPARE(out.count(), 1);
        w.signInDialog()->hide();
        // Open Location... = add a server by address
        w.menuAction("openloc")->trigger();
        QVERIFY(!w.findChild<QLineEdit*>("addaddr")->isHidden());
        // Show Resources / Preferences' checkbox = the Details tab
        w.menuAction("resources")->trigger();
        QVERIFY(w.detailsVisible());
    }
    void clearCacheListsWhatGoesAndAsksFirst() {
        PagerWindow w;
        account::Config cfg;
        cfg.account = "CacheTester";
        cfg.servers = account::defaultServers();
        QVERIFY(account::saveAccount(cfg.account, account::Account{Bytes(21, 9), 1, 2}));
        QDir().mkpath(home_.path() + "/.fugue/rsc/ab");
        QFile f(home_.path() + "/.fugue/rsc/ab/abcdef01");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(3000, 'x'));
        f.close();
        w.setState(cfg, check(cfg));
        w.menuAction("prefs")->trigger();
        QVERIFY(w.preferencesDialog() && w.preferencesDialog()->isVisible());
        auto* clear = w.preferencesDialog()->findChild<QPushButton*>("clearcache");
        QVERIFY(clear && clear->isEnabled());
        QVERIFY2(w.preferencesDialog()->findChild<QLabel*>("prefscache")->text().contains("2.9 KiB of game resources"),
                 qPrintable(w.preferencesDialog()->findChild<QLabel*>("prefscache")->text()));
        QString asked;
        bool answer = false;
        w.setConfirmHook([&](const QString&, const QString& text) {
            asked = text;
            return answer;
        });
        QSignalSpy spy(&w, &PagerWindow::clearCacheRequested);
        clear->click();
        QVERIFY2(asked.contains("Saved sign-in for cachetester"), qPrintable(asked));
        QVERIFY2(asked.contains(home_.path() + "/.fugue/identity-cachetester"), qPrintable(asked));
        QVERIFY2(asked.contains("Downloaded game resources — 2.9 KiB"), qPrintable(asked));
        QVERIFY2(asked.contains("Kept: your saved servers and settings"), qPrintable(asked));
        QCOMPARE(spy.count(), 0);  // "No": nothing asked for
        answer = true;
        clear->click();
        QCOMPARE(spy.count(), 1);
        // the Details checkbox in Preferences is the Details tab
        auto* cb = w.preferencesDialog()->findChild<QCheckBox*>("prefsdetails");
        cb->setChecked(true);
        QVERIFY(w.detailsVisible());
        cb->setChecked(false);
        QVERIFY(!w.detailsVisible());
        // while the game runs the cache is in use: no clearing
        w.setClientRunning(true);
        QVERIFY(!clear->isEnabled());
        w.requestClearCache();
        QCOMPARE(spy.count(), 1);
        QDir(home_.path() + "/.fugue/rsc").removeRecursively();
        QFile::remove(home_.path() + "/.fugue/identity-cachetester");
    }
    void checklistMatchesModel() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        auto st = check(cfg);
        w.setState(cfg, st);
        std::string want;
        for (auto& l : windowChecklist(st)) want += l + "\n";
        QCOMPARE(w.checklistText().toStdString(), want);
    }
    // No key.txt: the window points at its own Sign in, not at a command-line login.
    // (checklist() keeps the command-line wording.)
    void checklistPointsAtSignIn() {
        account::Config cfg;
        cfg.account = "NoKeyHere";
        cfg.servers = account::defaultServers();
        auto st = check(cfg);
        QVERIFY(st.keyTxtErr.has_value());  // the temp HOME has no key.txt
        std::string go;
        for (auto& l : checklist(st)) go += l + "\n";
        QVERIFY(go.find("fugue-qt pager -login") != std::string::npos);
        QVERIFY(go.find("run `fugue-qt pager -login` once") != std::string::npos);
        PagerWindow w;
        w.setState(cfg, check(cfg));
        QString text = w.checklistText();
        QVERIFY2(!text.contains("fugue-qt pager"), qPrintable(text));
        QVERIFY2(text.contains("\u2717 key.txt: no key.txt found"), qPrintable(text));
        QVERIFY2(text.contains("Sign in once with your BYOND account and Fugue writes it"), qPrintable(text));
        QVERIFY2(text.contains("\u2717 identity: none for NoKeyHere  \u2192 Sign in"), qPrintable(text));
        QCOMPARE(text.count("\n"), go.size() ? int(std::count(go.begin(), go.end(), '\n')) : 0);
    }
    void closingWhileAGameRunsHidesToTheTray() {
        PagerWindow w;
        account::Config cfg;
        cfg.servers = account::defaultServers();
        w.setState(cfg, check(cfg));
        QPixmap px(16, 16);
        px.fill(Qt::blue);
        w.setWindowIcon(QIcon(px));
        w.setTrayHook([] { return true; });
        QSignalSpy quit(&w, &PagerWindow::quitRequested);
        w.show();
        w.setClientRunning(true);
        QSystemTrayIcon* tray = w.trayIcon();
        QVERIFY(tray && tray->isVisible());
        QStringList texts;
        for (QAction* a : tray->contextMenu()->actions())
            if (!a->isSeparator()) texts << a->text();
        QCOMPARE(texts, QStringList({"Open Fugue", "Exit"}));
        QVERIFY(!w.close());
        QVERIFY(w.hiddenToTray());
        QCOMPARE(quit.count(), 0);
        tray->contextMenu()->findChild<QAction*>("trayopen")->trigger();
        QVERIFY(w.isVisible() && !w.hiddenToTray());
        QVERIFY(!w.close());
        // the game ends: the window comes back and the icon goes
        w.setClientRunning(false);
        QVERIFY(w.isVisible());
        QVERIFY(!tray->isVisible());
        QVERIFY(w.close());
        QCOMPARE(quit.count(), 1);
    }
    void withNoTrayClosingMinimizes() {
        PagerWindow w;
        account::Config cfg;
        w.setState(cfg, check(cfg));
        w.setTrayHook([] { return false; });
        QSignalSpy quit(&w, &PagerWindow::quitRequested);
        w.show();
        w.setClientRunning(true);
        QVERIFY(!w.trayIcon());
        QVERIFY(!w.close());
        QVERIFY(w.isMinimized());
        QCOMPARE(quit.count(), 0);
        w.setClientRunning(false);
        QVERIFY(!w.isMinimized() && w.isVisible());
    }
    void exitAsksBeforeEndingAGame() {
        PagerWindow w;
        account::Config cfg;
        w.setState(cfg, check(cfg));
        QPixmap px(16, 16);
        px.fill(Qt::blue);
        w.setWindowIcon(QIcon(px));
        w.setTrayHook([] { return true; });
        int asked = 0;
        bool answer = false;
        w.setConfirmHook([&](const QString&, const QString& text) {
            asked++;
            return answer && text.contains("closes the game");
        });
        QSignalSpy quit(&w, &PagerWindow::quitRequested);
        w.menuAction("exit")->trigger();
        QCOMPARE(asked, 0);
        QCOMPARE(quit.count(), 1);
        w.setClientRunning(true);
        w.menuAction("exit")->trigger();
        QCOMPARE(asked, 1);
        QCOMPARE(quit.count(), 1);
        answer = true;
        w.trayIcon()->contextMenu()->findChild<QAction*>("trayexit")->trigger();
        QCOMPARE(asked, 2);
        QCOMPARE(quit.count(), 2);
    }
    void theTrayNoticeAppearsOnce() {
        PagerWindow w;
        account::Config cfg;
        w.setState(cfg, check(cfg));
        w.setTrayHook([] { return true; });
        int notices = 0;
        w.setNoticeHook([&](const QString& title, const QString&) {
            notices++;
            QCOMPARE(title, QString("Fugue is still running"));
        });
        w.show();
        w.setClientRunning(true);
        for (int i = 0; i < 3; i++) {
            QVERIFY(!w.close());
            QVERIFY(w.hiddenToTray());
            w.showPager();
        }
        QCOMPARE(notices, 1);
    }
    void theWindowComesBackWhenTheTrayGoes() {
        PagerWindow w;
        account::Config cfg;
        w.setState(cfg, check(cfg));
        bool tray = true;
        w.setTrayHook([&] { return tray; });
        w.setNoticeHook([](const QString&, const QString&) {});
        w.show();
        w.setClientRunning(true);
        QVERIFY(!w.close());
        QVERIFY(w.hiddenToTray());
        tray = false;
        QTRY_VERIFY_WITH_TIMEOUT(w.isVisible(), 5000);
        QVERIFY(!w.hiddenToTray());
    }
    void aSecondPagerHandsOverItsLink() {
        QTemporaryDir dir;
        const QString name = dir.path() + "/pager";
        QStringList got;
        InstanceServer first([&](const QString& link) { got << link; });
        QVERIFY(first.listen(name));
        InstanceServer second([](const QString&) { QFAIL("the running pager keeps the channel"); });
        QVERIFY(!second.listen(name));
        auto forward = [&](const QString& link) {
            auto f = std::async(std::launch::async, [&] { return forwardToRunning(name, link); });
            while (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
                QCoreApplication::processEvents();
            return f.get();
        };
        QVERIFY(forward("byond://127.0.0.1:53321"));
        QVERIFY(forward(""));
        QTRY_COMPARE(got, QStringList({"byond://127.0.0.1:53321", ""}));
        QVERIFY(!forwardToRunning(dir.path() + "/nobody", "", 500));
    }
#ifndef WIN32
    void aDeadPagersSocketIsReplaced() {
        QTemporaryDir dir;
        const QString name = dir.path() + "/pager";
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un a{};
        a.sun_family = AF_UNIX;
        std::snprintf(a.sun_path, sizeof a.sun_path, "%s", name.toUtf8().constData());
        QCOMPARE(::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a), 0);
        ::close(fd);  // the file stays, as after a crash
        QVERIFY(QFile::exists(name));
        QVERIFY(!forwardToRunning(name, "", 500));
        QStringList got;
        InstanceServer s([&](const QString& link) { got << link; });
        QVERIFY(s.listen(name));
        auto f = std::async(std::launch::async, [&] { return forwardToRunning(name, "byond://127.0.0.1:53322"); });
        while (f.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) QCoreApplication::processEvents();
        QVERIFY(f.get());
        QTRY_COMPARE(got, QStringList({"byond://127.0.0.1:53322"}));
    }
#endif
    // last: a quit event leaves QCoreApplication's quit flag set for any later event loop
    void aQuitFromThePlatform() {
        for (bool tray : {true, false}) {
            PagerWindow w;
            account::Config cfg;
            w.setState(cfg, check(cfg));
            w.setTrayHook([tray] { return tray; });
            w.setNoticeHook([](const QString&, const QString&) { QFAIL("no notice while quitting"); });
            int asked = 0;
            bool answer = false;
            w.setConfirmHook([&](const QString&, const QString&) {
                asked++;
                return answer;
            });
            w.show();
            w.setClientRunning(true);
            QEvent quit(QEvent::Quit);
#ifdef Q_OS_MACOS
            // Cmd+Q or the Dock's Quit: asks like Exit
            QCoreApplication::sendEvent(qApp, &quit);
            QVERIFY(!quit.isAccepted());
            QCOMPARE(asked, 1);
            QVERIFY(w.isVisible());
            answer = true;
            quit.setAccepted(true);
            QCoreApplication::sendEvent(qApp, &quit);
            QCOMPARE(asked, 2);
#else
            // the session ending: never refused, never asked
            QCoreApplication::sendEvent(qApp, &quit);
            QCOMPARE(asked, 0);
#endif
            QVERIFY(quit.isAccepted());
            QVERIFY(!w.isVisible());
        }
    }
};
FUGUE_TEST_MAIN(PagerUi)
#include "pager_ui_test.moc"

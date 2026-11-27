// pager/launch: the argv a join runs, the scrubbed child environment, the desktop entry and its install.
#include <QtTest>
#include "tests/testmain.h"
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include "account/store.h"
#include "pager/cache.h"
#include "pager/launch.h"
#include "tests/privatefile.h"

using namespace fugue;

class PagerLaunchTest : public QObject {
    Q_OBJECT
private slots:
    // "Play as guest" is remembered under ~/.fugue/qt (never in pager.json), 0600, and a sign-in forgets it.
    void guestChoiceIsRememberedUnderQtState() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("FUGUE_PAGER_CONFIG");
        QCOMPARE(pager::guestChoicePath(), (home.path() + "/.fugue/qt/pager-guest").toStdString());
        QVERIFY(!pager::guestRemembered());
        std::string e;
        QVERIFY2(pager::rememberGuest(false, &e), e.c_str());  // forgetting nothing is not an error
        QVERIFY2(pager::rememberGuest(true, &e), e.c_str());
        QVERIFY(pager::guestRemembered());
        QVERIFY(isPrivateFile(pager::guestChoicePath()));
        QVERIFY(!QFile::exists(home.path() + "/.fugue/pager.json"));  // pager.json is not written
        QVERIFY2(pager::rememberGuest(false, &e), e.c_str());
        QVERIFY(!pager::guestRemembered());
    }
    // "Sign in automatically when Fugue starts": ON unless a marker under ~/.fugue/qt says off; never in pager.json; a setting, so
    // Clear cache keeps it.
    void autoSignInPreferencePersists() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("FUGUE_PAGER_CONFIG");
        QCOMPARE(pager::autoSignInOffPath(), (home.path() + "/.fugue/qt/pager-no-autosignin").toStdString());
        QVERIFY(pager::autoSignIn());  // default on
        std::string e;
        QVERIFY2(pager::setAutoSignIn(true, &e), e.c_str());  // turning on what is on is not an error
        QVERIFY2(pager::setAutoSignIn(false, &e), e.c_str());
        QVERIFY(!pager::autoSignIn());
        QVERIFY(!QFile::exists(home.path() + "/.fugue/pager.json"));
        pager::CachePlan plan = pager::planClearCache();
        for (auto& i : plan.items) QVERIFY2(i.path != pager::autoSignInOffPath(), i.path.c_str());
        pager::clearCache();
        QVERIFY(!pager::autoSignIn());  // survived Clear cache
        QVERIFY2(pager::setAutoSignIn(true, &e), e.c_str());
        QVERIFY(pager::autoSignIn());
    }
    void buildArgsNeverCarriesASecret() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("FUGUE_IDENTITY");
        const std::string pw = "hunter2-synthetic";
        qputenv("FUGUE_BYOND_PASSWORD", pw.c_str());
        account::Config cfg;
        cfg.account = "SomeKey";
        account::Server srv;
        srv.name = "vg";
        srv.addr = "game.example:7777";
        srv.hub = true;
        srv.flags = {"--size", "800x600"};
        account::Account a;
        a.installId = 0x262781;
        a.innerKey = 7;
        a.identity = {'S', 'o', 'm', 'e', 'K', 'e', 'y', 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
        std::string e;
        QVERIFY2(account::saveAccount("SomeKey", a, &e), e.c_str());
        auto args = pager::buildArgs(cfg, srv);
        std::string idp = account::identityPath("SomeKey", &e);
        QVERIFY(!idp.empty());
        std::vector<std::string> want{"--connect", "game.example:7777", "--account", "SomeKey",
                                      "--hub",     "--identity-file",   idp,         "--size",
                                      "800x600"};
        QCOMPARE(args, want);
        std::string joined = pager::displayCommand(args);
        QVERIFY(joined.find(pw) == std::string::npos);
        QVERIFY(joined.find("010203") == std::string::npos);  // no identity hex either
        // the identity file is what the path points at, 0600
        QVERIFY(isPrivateFile(idp));
        // a full command: prefix + args + passthrough, still no secret
        std::vector<std::string> cmd;
        QVERIFY(pager::buildCommand(cfg, srv, "/opt/fugue/fugue-qt", {"--no-pacing"}, &cmd, &e));
        QCOMPARE(cmd[0], std::string("/opt/fugue/fugue-qt"));
        QCOMPARE(cmd[1], std::string("client"));
        QCOMPARE(cmd.back(), std::string("--no-pacing"));
        for (auto& s : cmd) QVERIFY(s.find(pw) == std::string::npos);
        qunsetenv("FUGUE_BYOND_PASSWORD");
    }
    void guestAndNonHub() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        account::Server s;
        s.addr = "127.0.0.1:6000";
        QCOMPARE(pager::buildArgs(account::Config{}, s), (std::vector<std::string>{"--connect", "127.0.0.1:6000"}));
        account::Config c;
        c.account = "K";
        auto a = pager::buildArgs(c, s);
        QVERIFY(std::find(a.begin(), a.end(), "--hub") == a.end());  // a bare server never gets --hub
    }
    void childEnvironmentDropsEveryPasswordVariable() {
        qputenv("FUGUE_BYOND_PASSWORD", "x1");
        qputenv("HARMLESS_VAR", "keep");
        auto env = pager::childEnvironment();
        QVERIFY(!env.contains("FUGUE_BYOND_PASSWORD"));
        QCOMPARE(env.value("HARMLESS_VAR"), QString("keep"));  // control: it copies the rest
        qunsetenv("FUGUE_BYOND_PASSWORD");
        qunsetenv("HARMLESS_VAR");
    }
    void desktopQuoting() {
        QCOMPARE(pager::desktopQuote("/opt/fugue/fugue-qt"), std::string("/opt/fugue/fugue-qt"));
        QCOMPARE(pager::desktopQuote("/home/a b/f"), std::string("\"/home/a b/f\""));
        QCOMPARE(pager::desktopQuote("/h/$x`y\\z\"q%"), std::string("\"/h/\\$x\\`y\\\\z\\\"q%%\""));
        QCOMPARE(pager::desktopEntry("/home/a b/fugue-qt"),
                 std::string("[Desktop Entry]\nType=Application\nName=Fugue\nComment=Join Space Station 13 "
                             "servers\nExec=\"/home/a b/fugue-qt\" pager %u\n"
                             "Icon=io.github.toomykins.Fugue\nTerminal=false\nCategories=Game;\nMimeType=x-scheme-"
                             "handler/byond;\nStartupWMClass=fugue-qt\n"));
    }
    // The Windows handler passes the link as `pager --url "%1"`; a link that could have been split into more arguments
    // is refused, never parsed as pager flags.
    void urlHandlerTakesOneLinkOnly() {
        QCOMPARE(pager::urlHandlerCommand("C:\\Fugue\\fugue-qt.exe"),
                 std::string("\"C:\\Fugue\\fugue-qt.exe\" pager --url \"%1\""));
        std::string e;
        QCOMPARE(pager::handlerUrl({"--url", "byond://127.0.0.1:6000"}, &e).value_or(""),
                 std::string("byond://127.0.0.1:6000"));
        QCOMPARE(pager::handlerUrl({"--url", "byond://host:1/?a=%22b"}, &e).value_or(""),
                 std::string("byond://host:1/?a=%22b"));
        for (const std::vector<std::string>& bad :
             std::vector<std::vector<std::string>>{{"--url"},
                                                   {"--url", ""},
                                                   {"--url", "byond://h:1", "-keytxt", "\\\\host\\share\\key.txt"},
                                                   {"--url", "byond://h:1\" -keytxt \\\\host\\share\\key.txt"},
                                                   {"--url", "byond://h:1 -keytxt x"},
                                                   {"--url", "byond://h:1\t"},
                                                   {"--url", "byond://h:1\n-login"},
                                                   {"-keytxt", "x", "--url", "byond://h:1"},
                                                   {"--url=byond://h:1"}}) {
            e.clear();
            QVERIFY(!pager::handlerUrl(bad, &e));
            QVERIFY(!e.empty());
        }
    }
#ifndef WIN32
    void installWritesOnlyUnderXdgData() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        QString xdg = home.path() + "/data";
        qputenv("XDG_DATA_HOME", xdg.toUtf8());
        // a fugue.desktop holding our entry is removed; a foreign file of that name is kept
        QVERIFY(QDir().mkpath(xdg + "/applications"));
        QFile legacy(xdg + "/applications/fugue.desktop");
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(QByteArray::fromStdString(pager::desktopEntry("/old/fugue-qt"))
                         .replace("Icon=io.github.toomykins.Fugue\n", ""));
        legacy.close();
        auto r = pager::installDesktop("/x/fugue-qt", {{16, QByteArray("png16")}, {256, QByteArray("png256")}});
        QVERIFY2(r.ok, r.err.c_str());
        QVERIFY(QFile::exists(xdg + "/applications/io.github.toomykins.Fugue.desktop"));
        QVERIFY(!QFile::exists(xdg + "/applications/fugue.desktop"));
        QVERIFY(QFile::exists(xdg + "/applications/mimeapps.list"));
        QCOMPARE(r.iconPaths.size(), size_t(2));
        QFile i16(xdg + "/icons/hicolor/16x16/apps/io.github.toomykins.Fugue.png");
        QVERIFY(i16.open(QIODevice::ReadOnly));
        QCOMPARE(i16.readAll(), QByteArray("png16"));
        QVERIFY(QFile::exists(xdg + "/icons/hicolor/256x256/apps/io.github.toomykins.Fugue.png"));
        QFile other(xdg + "/applications/fugue.desktop");
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write("[Desktop Entry]\nType=Application\nName=Something else\n");
        other.close();
        QVERIFY(pager::installDesktop("/x/fugue-qt").ok);
        QVERIFY(QFile::exists(xdg + "/applications/fugue.desktop"));  // not ours: kept
        QDir h(home.path());
        QCOMPARE(h.entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{"data"});
        QVERIFY(r.desktopPath.rfind(xdg.toStdString(), 0) == 0);
        qunsetenv("XDG_DATA_HOME");
    }
#endif
};

FUGUE_TEST_MAIN(PagerLaunchTest)
#include "pager_launch_test.moc"

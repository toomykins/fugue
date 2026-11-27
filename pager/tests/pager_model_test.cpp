// pager_model: readiness sentences, blockers and the -list renderer under a temp HOME.
// Never reads the real ~/.fugue or ~/.byond.
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include "platform/file.h"
#include "account/store.h"
#include "pager/model.h"

using namespace fugue;
using namespace fugue::pager;

namespace {
struct Home {
    QTemporaryDir dir;
    Home() {
        qputenv("HOME", dir.path().toUtf8());
        for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME",
                       "FUGUE_CLIENT"})
            qunsetenv(v);
    }
    void put(const QString& rel, const QByteArray& b, int mode = 0600) {
        QString p = dir.path() + "/" + rel;
        QDir().mkpath(QFileInfo(p).absolutePath());
        QFile f(p);
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
        QCOMPARE(f.write(b), qint64(b.size()));
        f.close();
        QCOMPARE(platform::chmod(p.toUtf8().constData(), mode), 0);
    }
    void client() {
        put("bin/fugue", "x", 0755);
        qputenv("FUGUE_CLIENT", (dir.path() + "/bin/fugue").toUtf8());
    }
};
}  // namespace

class PagerModel : public QObject {
    Q_OBJECT
private slots:
    void guestOnHubIsBlocked() {
        Home h;
        h.client();
        auto cfg = *account::loadConfig();  // seeded defaults: a hub server and a local server
        auto st = check(cfg);
        QVERIFY(st.keyTxtErr);
        QVERIFY(!st.clientErr);
        QVERIFY(!blocker(st, cfg.servers[0]));  // a guest may try a hub server: the world decides, not the hub
        QVERIFY(!blocker(st, cfg.servers[1]));
        QVERIFY(QString::fromStdString(renderList(cfg, st))
                    .startsWith("account:  (guest)\n  \xe2\x9c\x97 key.txt: no key.txt found"));
    }
    void accountStates() {
        Home h;
        h.client();
        h.put(".byond/cfg/key.txt", "BEGIN KEY Fake Tester\nline\n`\nEND KEY\n");
        account::Config cfg;
        cfg.servers = account::defaultServers();
        cfg.account = "fake tester";
        auto st = check(cfg);
        QVERIFY(st.hasKey() && !st.hasIdentity);
        auto b = blocker(st, cfg.servers[1]);
        QVERIFY(b && QString::fromStdString(*b).startsWith("no account identity for fake tester"));
        QVERIFY(account::saveAccount(cfg.account, account::Account{{9, 9}, 7, 7}));
        st = check(cfg);
        QVERIFY(st.hasIdentity && !blocker(st, cfg.servers[0]));
        cfg.account = "someone else";
        st = check(cfg);
        QVERIFY(!st.hasKey());
        QVERIFY(
            QString::fromStdString(checklist(st)[0]).contains("has no block for someone else (it has: Fake Tester)"));
    }
    void cacheScanRule() {
        Home h;
        h.put(".fugue/rsc/ab/abcdef01", QByteArray(2048, 'x'));
        h.put(".fugue/rsc/ab/notahash", "zz");
        h.put(".fugue/rsc/abc/abcdef02", "zz");
        auto cs = scanRscCache(rscDir());
        QCOMPARE(cs.files, 1LL);
        QCOMPARE(cs.bytes, 2048LL);
        // -list prints what the scan knows and the client's own cap; per-session counters are not on disk.
        account::Config cfg;
        const QString list = QString::fromStdString(renderList(cfg, check(cfg)));
        QVERIFY2(list.endsWith("rsc cache: 1 file, 0.0 MiB of 1024 MiB cap\n"), qPrintable(list));
    }
    // The warm-up output, as app/prefetchmode.cpp and net/prefetch.cpp print it.
    void warmLinesParse() {
        WarmProgress p;
        QVERIFY(parseWarmLine("[prefetch] prefetch: 959/983 resolved, 0 cached, 1 outstanding", p));
        QVERIFY(!p.summary);
        QCOMPARE(p.done, uint64_t(959));
        QCOMPARE(p.total, uint64_t(983));
        QCOMPARE(p.outstanding, uint64_t(1));
        QCOMPARE(plainWarmProgress(p), std::string("Downloading resources: 959 of 983 files"));
        QVERIFY(parseWarmLine(
            "PREFETCH ok=1 offered=983 cached=0 declared=983 stored=982 unverified=0 bytes=7457553 seconds=26.87", p));
        QVERIFY(p.summary && p.ok);
        QCOMPARE(p.offered, uint64_t(983));
        QCOMPARE(p.stored, uint64_t(982));
        QCOMPARE(p.bytes, uint64_t(7457553));
        QCOMPARE(plainWarmProgress(p),
                 std::string("Resources downloaded: 982 new files (7.1 MiB). Joining now starts with them."));
        QVERIFY(parseWarmLine(
            "PREFETCH ok=1 offered=983 cached=983 declared=0 stored=0 unverified=0 bytes=0 seconds=1.60", p));
        QCOMPARE(plainWarmProgress(p), std::string("All 983 resources were already downloaded."));
        QVERIFY(
            parseWarmLine("PREFETCH ok=0 offered=983 cached=0 declared=128 stored=90 unverified=0 bytes=1 "
                          "seconds=95.00 error=server closed during prefetch",
                          p));
        QVERIFY(p.summary && !p.ok);
        QCOMPARE(p.error, std::string("server closed during prefetch"));
        QVERIFY(plainWarmProgress(p).find("What arrived is kept") != std::string::npos);
        QVERIFY(!parseWarmLine("[prefetch] connected tcp 127.0.0.1:1", p));
        account::Server s;
        QVERIFY(plainWarmState(s).find("not downloaded yet") != std::string::npos);
        s.lastPrefetch = account::Timestamp{1759000000, 0};
        QCOMPARE(plainWarmState(s), std::string("Resources downloaded 27 Sep."));
    }
    void closingNeverEndsARunningGame() {
        QCOMPARE(closeAction(false, true), CloseAction::Quit);
        QCOMPARE(closeAction(false, false), CloseAction::Quit);
        QCOMPARE(closeAction(true, true), CloseAction::HideToTray);
        QCOMPARE(closeAction(true, false), CloseAction::Minimize);
    }
};
FUGUE_TEST_APPLESS_MAIN(PagerModel)
#include "pager_model_test.moc"

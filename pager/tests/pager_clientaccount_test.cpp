// Which account a client run plays as (pager/clientaccount.h). Each case runs under a scratch HOME holding a signed-in
// pager.json and identity file, so a guest run must ignore that record.
#include <QtTest>
#include "tests/testmain.h"
#include <QTemporaryDir>
#include <set>
#include "account/login.h"
#include "account/store.h"
#include "pager/clientaccount.h"
#include "pager/launch.h"

using namespace fugue;
using pager::AccountDecision;
using pager::AccountRequest;

namespace {
const std::string kKey = "fakeaccount";
void signIn(const QTemporaryDir& home, bool withIdentity = true) {
    qputenv("HOME", home.path().toUtf8());
    for (const char* v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_KEYTXT", "FUGUE_BYOND_SID", "XDG_STATE_HOME"})
        qunsetenv(v);
    account::Config c;
    c.account = kKey;
    std::string e;
    QVERIFY2(account::saveConfig(c, &e), e.c_str());
    if (withIdentity) {
        account::Account a;
        for (char ch : kKey + "0123456789abc") a.identity.push_back(uint8_t(ch));
        a.installId = 0x1234;  // saveAccount refuses zero hub credentials
        a.innerKey = 0x5678;
        QVERIFY2(account::saveAccount(kKey, a, &e), e.c_str());
    }
}
AccountRequest player() {
    AccountRequest r;
    r.playerLaunch = true;
    return r;
}  // no --account: `dreamseeker "byond://h:p"`

// The whole client command line, as the binary really takes it: pager/dsargv.cpp's chooseMode, then app/main.cpp's parseDsArgv
// (with Qt's arguments, given by index, removed by pointer as QApplication removes them) and accountRequestFor.
struct Run {
    AccountRequest req;
    AccountDecision d;
    int mints = 0;  // how many times an identity was loaded
};
Run launch(std::vector<std::string> argv, bool testHook = false, std::set<size_t> qtRemoves = {},
           bool release = false) {
    Run r;
    const std::string argv0 = argv.front();
    pager::Mode m = pager::chooseMode(argv0, std::vector<std::string>(argv.begin() + 1, argv.end()), release);
    if (m.mode != "client") qFatal("not a client run: %s", m.mode.c_str());
    std::vector<const char*> before, after;
    for (size_t j = 0; j < m.rest.size(); j++) {
        before.push_back(m.rest[j].c_str());
        if (!qtRemoves.count(j)) after.push_back(m.rest[j].c_str());
    }
    const pager::FugueOptions fo = pager::clientOptions(m.asDreamSeeker);
    const pager::DsArgv ds = pager::parseDsArgv(m.rest, fo, pager::takenByHost(before, after));
    r.req = pager::accountRequestFor(m.asDreamSeeker, ds, fo, testHook);
    r.d = pager::decideAccount(r.req, [&](const std::string& k, std::string* e) {
        r.mints++;
        return account::mintLocalAccountLogin(k, "", "", e);
    });
    return r;
}
bool isGuest(const Run& r) { return r.d.kind == AccountDecision::Guest && !r.d.login && r.mints == 0; }
bool isPagerAccount(const Run& r) {
    return r.d.kind == AccountDecision::Account && r.d.fromPager && r.d.account == kKey && r.d.login;
}
}  // namespace

class PagerClientAccount : public QObject {
    Q_OBJECT
private slots:
    void playerLaunchUsesThePagerAccount() {
        QTemporaryDir home;
        signIn(home);
        AccountDecision d = pager::decideAccount(player());
        QCOMPARE(int(d.kind), int(AccountDecision::Account));
        QCOMPARE(d.account, kKey);
        QVERIFY(d.fromPager);
        QVERIFY(d.login && !d.login->record.empty());
        QCOMPARE(d.login->source, std::string("identity cache"));
    }
    void nonPlayerRunIsAGuest() {  // only a player launch reads the signed-in account
        QTemporaryDir home;
        signIn(home);
        AccountRequest r;  // not a player launch: --state-dir, --connect, a test hook
        bool minted = false;
        AccountDecision d = pager::decideAccount(r, [&](const std::string&, std::string*) {
            minted = true;
            return std::optional<account::AccountLogin>{};
        });
        QCOMPARE(int(d.kind), int(AccountDecision::Guest));
        QVERIFY(!d.login);
        QVERIFY(d.account.empty());
        QVERIFY(!minted);  // no identity was even loaded
        QVERIFY(QFile::exists(home.path() + "/.fugue/identity-" +
                              QString::fromStdString(kKey)));  // and it was there to load
    }
    void nonPlayerRunStillHonoursAnExplicitAccount() {
        QTemporaryDir home;
        signIn(home);
        AccountRequest r;
        r.accountGiven = true;
        r.account = kKey;
        AccountDecision d = pager::decideAccount(r);
        QCOMPARE(int(d.kind), int(AccountDecision::Account));
        QVERIFY(!d.fromPager);
    }
    void rememberedGuestChoice() {
        QTemporaryDir home;
        signIn(home);
        std::string e;
        QVERIFY2(pager::rememberGuest(true, &e), e.c_str());
        AccountDecision d = pager::decideAccount(player());
        QCOMPARE(int(d.kind), int(AccountDecision::Guest));
        QVERIFY(!d.login);
    }
    void explicitEmptyAccountIsAGuest() {
        QTemporaryDir home;
        signIn(home);
        AccountRequest r;
        r.accountGiven = true;  // --account ""
        AccountDecision d = pager::decideAccount(r);
        QCOMPARE(int(d.kind), int(AccountDecision::Guest));
        QVERIFY(!d.login);
    }
    void nobodySignedIn() {
        QTemporaryDir home;
        qputenv("HOME", home.path().toUtf8());
        qunsetenv("FUGUE_PAGER_CONFIG");
        AccountDecision d = pager::decideAccount(player());
        QCOMPARE(int(d.kind), int(AccountDecision::Guest));
    }
    void signedInWithoutIdentityIsAGuest() {
        QTemporaryDir home;
        signIn(home, false);
        AccountDecision d = pager::decideAccount(player());
        QCOMPARE(int(d.kind), int(AccountDecision::Guest));
        QVERIFY(d.fromPager);
        QCOMPARE(d.account, kKey);
        QVERIFY(!d.login);
    }
    void explicitAccountWithoutIdentityFails() {  // app/main.cpp: exit 2
        QTemporaryDir home;
        signIn(home, false);
        AccountRequest r;
        r.accountGiven = true;
        r.account = "someoneelse";
        AccountDecision d = pager::decideAccount(r);
        QCOMPARE(int(d.kind), int(AccountDecision::Fail));
        QVERIFY(!d.reason.empty());
        QVERIFY(!d.login);
    }

    // ---- the player-launch rule, from real argv vectors ----
    void fugueConnectIsAGuest() {
        QTemporaryDir home;
        signIn(home);
        QVERIFY(isGuest(launch({"fugue-qt", "--connect", "127.0.0.1:52411", "--audio", "none"})));
        QVERIFY(isGuest(launch({"fugue-qt", "client", "--connect", "127.0.0.1:52411"})));
        QVERIFY(
            isGuest(launch({"fugue-qt", "client", "--connect", "127.0.0.1:52411"}, false, {}, true)));  // release build
        QVERIFY(isGuest(launch({"fugue-qt", "--connect=127.0.0.1:52411"})));
        QVERIFY(isGuest(launch({"/opt/fugue/fugue-qt", "--no-pacing", "--connect", "h:1", "-pager"})));
        // a DreamSeeker target beside --connect: --connect wins the target (app/main.cpp), and it is not a DS-style launch
        QVERIFY(isGuest(launch({"fugue-qt", "byond://h:1", "--connect", "h:2"})));
        QCOMPARE(launch({"fugue-qt", "--connect", "h:1"}).req.why, std::string("--connect"));
        // under the dreamseeker name too: Fugue's --connect is never part of a real DreamSeeker launch
        QVERIFY(isGuest(launch({"dreamseeker", "--connect", "127.0.0.1:52411"})));
        QVERIFY(isGuest(launch({"DreamSeeker.exe", "byond://h:1", "--connect=h:2"})));
    }
    void noTargetIsAGuest() {  // the client with no target: DS's Open Location box, or the quiet exit
        QTemporaryDir home;
        signIn(home);
        QVERIFY(isGuest(launch({"fugue-qt"})));
        QVERIFY(isGuest(launch({"fugue-qt", "client"}, false, {}, true)));
        QVERIFY(isGuest(launch({"fugue-qt", "-trace"})));
        QVERIFY(isGuest(launch({"fugue-qt", "+connect", "h:1"})));  // the +connect bound fails, so there is no target
    }
    void dreamSeekerStyleLaunchUsesThePagerAccount() {
        QTemporaryDir home;
        signIn(home);
        QVERIFY(isPagerAccount(launch({"dreamseeker", "byond://127.0.0.1:52411", "-pager", "-trusted"})));
        QVERIFY(isPagerAccount(launch({"C:\\BYOND\\bin\\DreamSeeker.exe", "+connect", "h:1", "-trace"})));
        QVERIFY(isPagerAccount(launch({"/usr/bin/dreamseeker", "h:1"}, false, {}, true)));  // release: still the client
        QVERIFY(isPagerAccount(launch({"dreamseeker"})));  // the Open Location box, then the account
        // as fugue-qt, a DreamSeeker-grammar target with no --connect
        QVERIFY(isPagerAccount(launch({"fugue-qt", "byond://h:1", "-pager"})));
        QVERIFY(isPagerAccount(launch({"fugue-qt", "client", "h:1"}, false, {}, true)));
        QVERIFY(isPagerAccount(launch({"fugue-qt", "+connect", "h:1", "-trace"})));
        QVERIFY(isPagerAccount(launch({"fugue-qt", "--audio", "none", "h:1"})));
        // Qt's arguments counted for the +connect bound, never a target: `+connect h:1 -platform offscreen`
        QVERIFY(isPagerAccount(launch({"dreamseeker", "+connect", "h:1", "-platform", "offscreen"}, false, {2, 3})));
    }
    void stateDirAndHooksAreGuests() {
        QTemporaryDir home;
        signIn(home);
        QVERIFY(isGuest(launch({"dreamseeker", "byond://h:1", "-pager", "--state-dir", "/tmp/x"})));
        QVERIFY(isGuest(launch({"dreamseeker", "byond://h:1", "--state-dir=/tmp/x"})));
        QVERIFY(isGuest(launch({"dreamseeker", "byond://h:1"}, true)));  // test hook
        QCOMPARE(launch({"dreamseeker", "h:1", "--state-dir", "d"}).req.why, std::string("--state-dir"));
        // a Qt argument that LOOKS like Fugue's is Qt's: removed before the parse, so it isolates nothing
        QVERIFY(isPagerAccount(launch({"dreamseeker", "h:1", "-style", "--state-dir"}, false, {1, 2})));
    }
    void explicitAccountOnEveryShape() {
        QTemporaryDir home;
        signIn(home);
        for (std::vector<std::string> a : std::vector<std::vector<std::string>>{
                 {"fugue-qt", "client", "--connect", "h:1", "--account", kKey,
                  "--no-pacing"},  // the pager's join (launch.cpp)
                 {"fugue-qt", "--connect", "h:1", "--state-dir", "d", "--account=" + kKey},
                 {"dreamseeker", "h:1", "--account", kKey}}) {
            Run r = launch(a);
            QCOMPARE(int(r.d.kind), int(AccountDecision::Account));
            QVERIFY(!r.d.fromPager);
            QCOMPARE(r.d.account, kKey);
        }
        Run g = launch({"dreamseeker", "byond://h:1", "--account="});  // an explicit guest on a player launch
        QVERIFY(isGuest(g));
        QVERIFY(g.req.accountGiven);
        Run last = launch(
            {"fugue-qt", "--connect", "h:1", "--account", "someoneelse", "--account", kKey});  // the last one given
        QCOMPARE(last.req.account, kKey);
        QCOMPARE(int(launch({"fugue-qt", "--connect", "h:1", "--account", "nobodyhere"}).d.kind),
                 int(AccountDecision::Fail));
    }
};

FUGUE_TEST_GUILESS_MAIN(PagerClientAccount)
#include "pager_clientaccount_test.moc"

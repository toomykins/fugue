// DreamSeeker's command line (pager/dsargv.h). Each row is a shape whose stock behaviour is read from stock DreamSeeker 516.1684.
#include <QtTest>
#include <set>
#include "pager/clientaccount.h"
#include "pager/dsargv.h"
#include "pager/target.h"

using namespace fugue::pager;

namespace {
// The client's own option names: the table app/main.cpp checks its QCommandLineOptions against at startup.
FugueOptions opts(bool keepUnknown) { return clientOptions(!keepUnknown); }
DsArgv p(std::vector<std::string> a) { return parseDsArgv(a, opts(true)); }    // invoked as fugue-qt
DsArgv ds(std::vector<std::string> a, std::vector<bool> taken = {}) { return parseDsArgv(a, opts(false), taken); }   // as dreamseeker
// Qt's argv compaction, simulated: `before` is argv[1..]; Qt removes the indices in `removed` by moving POINTERS
// (QCoreApplicationPrivate: argv[j++] = argv[i]). Returns takenByHost over the real pointers, as app/main.cpp computes it.
std::vector<bool> qtTakes(const std::vector<std::string>& before, const std::set<size_t>& removed) {
    std::vector<const char*> b, a;
    for (size_t j = 0; j < before.size(); j++) {
        b.push_back(before[j].c_str());
        if (!removed.count(j)) a.push_back(before[j].c_str());
    }
    return takenByHost(b, a);
}
std::string dial(const DsArgv& d) {   // what Fugue dials for that target ("" = refused / none)
    if (d.target.empty()) return {};
    auto t = parseTarget(d.target);
    return t ? *t : std::string("REFUSED");
}
}

class PagerDsArgv : public QObject {
    Q_OBJECT
private slots:
    void pagerLaunchShape() {  // byond.exe pager launch shape: "<bin>\dreamseeker.exe" "<target>" -pager [+ "-trace"] [+ "-trusted"]
        DsArgv d = p({"byond://127.0.0.1:6000", "-pager", "-trace", "-trusted"});
        QCOMPARE(d.target, std::string("byond://127.0.0.1:6000"));
        QVERIFY(d.pager);
        QVERIFY(d.trace);
        QCOMPARE(d.security, 0);
        QVERIFY(d.dropped.empty());
        QVERIFY(d.fugue.empty());
        QCOMPARE(dial(d), std::string("127.0.0.1:6000"));
    }
    void flagsAreCaseInsensitive() {   // -TRUSTED, -Pager, -TrAcE and -Is-Server match case-insensitively
        DsArgv d = p({"-TRUSTED", "-Pager", "-TrAcE", "-Is-Server", "BYOND://h:1"});
        QCOMPARE(d.security, 0);
        QVERIFY(d.pager && d.trace && d.isServer);
        QCOMPARE(d.target, std::string("BYOND://h:1"));   // a bare target is stored RAW
        QCOMPARE(dial(d), std::string("h:1"));          // DreamSeeker dials an upper-case bare BYOND:// target
        QVERIFY(p({"-ISSERVER"}).isServer);
    }
    void securityLastWins() {
        QCOMPARE(p({"-trusted", "-safe"}).security, 1);
        QCOMPARE(p({"-safe", "-ultrasafe"}).security, 2);
        QCOMPARE(p({"-ultrasafe", "-trusted"}).security, 0);
        QCOMPARE(p({"h:1"}).security, -1);
    }
    void debugIsCaseSensitive() {   // -debug is compared byte for byte, not case-insensitively
        QVERIFY(p({"-debug"}).debug);
        DsArgv d = p({"-DEBUG"});
        QVERIFY(!d.debug);
        QCOMPARE(d.dropped, (std::vector<std::string>{"-DEBUG"}));
    }
    void bareTargetAfterFlags() {   // a bare host:port, and one after an unknown flag
        DsArgv d = p({"-trace", "-foo", "game.example:7777"});
        QCOMPARE(d.target, std::string("game.example:7777"));
        QCOMPARE(d.dropped, (std::vector<std::string>{"-foo"}));   // an unknown -flag is never the target
        QCOMPARE(dial(d), std::string("game.example:7777"));
    }
    void firstBareWins() {   // the second bare argument is dropped
        DsArgv d = p({"127.0.0.1:1", "127.0.0.1:6000"});
        QCOMPARE(d.target, std::string("127.0.0.1:1"));
        QCOMPARE(d.dropped, (std::vector<std::string>{"127.0.0.1:6000"}));
    }
    void connectPrependsScheme() {   // +connect host:port with an argument after it
        DsArgv d = p({"+connect", "h:6000", "-trace"});
        QCOMPARE(d.target, std::string("byond://h:6000"));
        QCOMPARE(dial(d), std::string("h:6000"));
        QCOMPARE(p({"+connect", "byond://h:6000", "-x"}).target, std::string("byond://h:6000"));
    }
    void connectSchemeTestIsCaseSensitive() {   // the scheme test is case-sensitive: BYOND:// is prefixed again, as DreamSeeker does
        DsArgv d = p({"+connect", "BYOND://h:6000", "-trace"});
        QCOMPARE(d.target, std::string("byond://BYOND://h:6000"));
        QCOMPARE(dial(d), std::string("REFUSED"));
    }
    void connectNeedsAFollowingArgument() {   // `+connect h:p` as the last two arguments joins nothing (i+2 < argc)
        DsArgv d = p({"+connect", "h:6000"});
        QVERIFY(d.target.empty());
        QCOMPARE(d.dropped, (std::vector<std::string>{"+connect", "h:6000"}));
    }
    void connectIsCaseSensitiveAndPlusSwallows() {   // +CONNECT and +foo swallow the next argument, which is not a target
        DsArgv d = p({"+CONNECT", "h:6000", "-trace"});
        QVERIFY(d.target.empty());
        QVERIFY(d.trace);
        QCOMPARE(d.dropped, (std::vector<std::string>{"+CONNECT", "h:6000"}));
        DsArgv e = p({"+foo", "h:6000"});
        QVERIFY(e.target.empty());
        DsArgv f = p({"+foo", "h:1", "h:2"});   // the argument after the swallowed one is a bare target again
        QCOMPARE(f.target, std::string("h:2"));
    }
    void connectReplacesEarlierTarget() {   // a bare target, then +connect: the +connect target wins
        QCOMPARE(p({"127.0.0.1:1", "+connect", "127.0.0.1:6000", "-trace"}).target, std::string("byond://127.0.0.1:6000"));
        QCOMPARE(p({"+connect", "a:1", "+connect", "b:2", "-x"}).target, std::string("byond://b:2"));
        DsArgv d = p({"+connect", "a:1", "b:2"});   // a bare argument after it is dropped
        QCOMPARE(d.target, std::string("byond://a:1"));
        QCOMPARE(d.dropped, (std::vector<std::string>{"b:2"}));
    }
    void updaterLinesAreUnknownFlags() {   // inert in stock dreamseeker.exe (only a standalone hub-game build acts on them)
        DsArgv d = p({"-install", "h:1", "x", "y"});
        QCOMPARE(d.target, std::string("h:1"));
        QCOMPARE(d.dropped, (std::vector<std::string>{"-install", "x", "y"}));
        DsArgv e = p({"-updated"});
        QVERIFY(e.target.empty());
        QCOMPARE(e.dropped, (std::vector<std::string>{"-updated"}));
    }
    void fugueOptionsPassThrough() {
        DsArgv d = p({"--state-dir", "/tmp/x", "byond://h:1", "-pager", "--audio=none", "--no-pacing", "--connect", "c:2"});
        QCOMPARE(d.target, std::string("byond://h:1"));   // a value of a Fugue option is never the target
        QVERIFY(d.pager);
        QCOMPARE(d.fugue, (std::vector<std::string>{"--state-dir", "/tmp/x", "--audio=none", "--no-pacing", "--connect", "c:2"}));
        DsArgv e = p({"--audio", "none"});
        QVERIFY(e.target.empty());
    }
    void rendererOptionPassesThrough() {
        DsArgv d = p({"--renderer", "gpu", "--connect", "localhost:13337"});
        QVERIFY(d.target.empty());
        QCOMPARE(d.fugue, (std::vector<std::string>{"--renderer", "gpu", "--connect", "localhost:13337"}));
        DsArgv e = ds({"--renderer=gpu", "byond://localhost:13337", "-pager"});
        QCOMPARE(e.target, std::string("byond://localhost:13337"));
        QCOMPARE(e.fugue, (std::vector<std::string>{"--renderer=gpu"}));
    }
    void unknownDoubleDashWord() {
        // Invoked as DreamSeeker: DS drops any unknown '-' argument, so an unknown --word is dropped too and never
        // reaches QCommandLineParser (which would exit on it); Fugue's own --options still work beside it.
        DsArgv d = ds({"--bogus", "--frobnicate=3", "byond://h:1", "--audio", "none", "-pager"});
        QCOMPARE(d.target, std::string("byond://h:1"));
        QCOMPARE(d.dropped, (std::vector<std::string>{"--bogus", "--frobnicate=3"}));
        QCOMPARE(d.fugue, (std::vector<std::string>{"--audio", "none"}));
        DsArgv u = ds({"--bogus", "h:1"});   // the argument after an unknown --word is not swallowed: DS reads it as usual
        QCOMPARE(u.target, std::string("h:1"));
        // Invoked as fugue-qt: an unknown --word goes to Fugue's parser, which refuses it (a typo is reported, not ignored).
        QCOMPARE(p({"--bogus", "h:1"}).fugue, (std::vector<std::string>{"--bogus"}));
    }
    void qtArgumentsCountTowardDsArgc() {
        // `dreamseeker +connect h:1 -platform offscreen`: DS (no Qt) sees argc 5, so +connect has an argument after it and
        // joins. QApplication removes -platform offscreen before Fugue reads argv; counted as taken, the bound
        // is still DreamSeeker's, and neither of Qt's arguments is read as a flag or a target.
        std::vector<std::string> before{"+connect", "h:1", "-platform", "offscreen"};
        std::vector<bool> t = qtTakes(before, {2, 3});
        QCOMPARE(t, (std::vector<bool>{false, false, true, true}));
        DsArgv d = ds(before, t);
        QCOMPARE(d.target, std::string("byond://h:1"));
        QVERIFY(d.dropped.empty());
        QVERIFY(ds({"+connect", "h:1"}).target.empty());   // without the mask the bound would fail
        // a Qt argument is never the target either
        std::vector<std::string> st{"-style", "fusion", "h:2"};
        DsArgv e = ds(st, qtTakes(st, {0, 1}));
        QCOMPARE(e.target, std::string("h:2"));
        QVERIFY(e.dropped.empty());
        QCOMPARE(qtTakes({"a", "b"}, {}), (std::vector<bool>{false, false}));
    }
    void qtTakenIsByPositionNotText() {
        // Counter-example to a text match: Qt takes `-style +connect` from {-style, +connect, +connect, h:1}, leaving
        // {+connect, h:1}. The survivor is argv[3]'s +connect (DS index 3, argc 5: 3+2 < 5 fails), so DreamSeeker has no
        // target. A text match would keep argv[2]'s +connect, call its value taken, and make h:1 a bare target.
        std::vector<std::string> before{"-style", "+connect", "+connect", "h:1"};
        std::vector<bool> t = qtTakes(before, {0, 1});
        QCOMPARE(t, (std::vector<bool>{true, true, false, false}));
        DsArgv d = ds(before, t);
        QVERIFY2(d.target.empty(), d.target.c_str());
        QCOMPARE(d.dropped, (std::vector<std::string>{"+connect", "h:1"}));
        QVERIFY(noTargetAction(before.size() + 1, d) == NoTarget::QuietExit);
    }
    void noTarget() {
        QVERIFY(noTargetAction(1, ds({})) == NoTarget::OpenLocation);              // no argument: the Open Location box
        QVERIFY(noTargetAction(2, ds({"-trace"})) == NoTarget::QuietExit);
        QVERIFY(noTargetAction(2, ds({"-updated"})) == NoTarget::QuietExit);
        QVERIFY(noTargetAction(3, ds({"+connect", "h:1"})) == NoTarget::QuietExit);   // no window at all
        QVERIFY(noTargetAction(3, ds({"--bogus"})) == NoTarget::QuietExit);        // dropped as DS drops it
        // only Qt's arguments: DreamSeeker would have had arguments, so no box either
        QVERIFY(noTargetAction(3, ds({"-platform", "offscreen"}, {true, true})) == NoTarget::QuietExit);
        QVERIFY(noTargetAction(3, p({"--audio", "none"})) == NoTarget::Usage);     // Fugue's own options: not a DS line
    }
    void modeSwitch() {
        // Installed as DreamSeeker: the client even in a FUGUE_RELEASE build, and no subcommand is read.
        for (const char* argv0 : {"dreamseeker", "DreamSeeker.exe", "/usr/bin/dreamseeker", "C:\\BYOND\\bin\\DREAMSEEKER.EXE"}) {
            Mode m = chooseMode(argv0, {"byond://h:1", "-pager"}, true);
            QCOMPARE(m.mode, std::string("client"));
            QVERIFY(m.asDreamSeeker);
            QCOMPARE(m.rest, (std::vector<std::string>{"byond://h:1", "-pager"}));
            QCOMPARE(chooseMode(argv0, {}, true).mode, std::string("client"));   // no argument -> the client's Open Location
            Mode pg = chooseMode(argv0, {"pager"}, true);                        // `pager` is a target to DreamSeeker
            QCOMPARE(pg.mode, std::string("client"));
            QCOMPARE(pg.rest, (std::vector<std::string>{"pager"}));
            QCOMPARE(chooseMode(argv0, {"version"}, false).mode, std::string("client"));
        }
        // As fugue-qt: release = the pager, dev = the client, and a subcommand picks the mode.
        QCOMPARE(chooseMode("/opt/fugue/fugue-qt", {}, true).mode, std::string("pager"));
        QCOMPARE(chooseMode("fugue-qt", {"byond://h:1"}, true).mode, std::string("pager"));
        QCOMPARE(chooseMode("fugue-qt", {}, false).mode, std::string("client"));
        Mode c = chooseMode("fugue-qt", {"client", "--connect", "h:1"}, true);
        QCOMPARE(c.mode, std::string("client"));
        QVERIFY(!c.asDreamSeeker);
        QCOMPARE(c.rest, (std::vector<std::string>{"--connect", "h:1"}));
        QCOMPARE(chooseMode("fugue-qt", {"pager", "-list"}, false).mode, std::string("pager"));
        QCOMPARE(chooseMode("fugue-qt", {"version"}, true).mode, std::string("version"));
        QCOMPARE(chooseMode("fugue-qt", {"webcheck"}, true).mode, std::string("webcheck"));
        QCOMPARE(chooseMode("dreamseeker", {"webcheck"}, true).mode, std::string("client"));   // a target to DreamSeeker
        // --version wins in every mode; as DreamSeeker only when it is the sole argument.
        for (bool rel : {true, false}) {
            QCOMPARE(chooseMode("fugue-qt", {"--version"}, rel).mode, std::string("version"));
            QCOMPARE(chooseMode("fugue-qt", {"pager", "--version"}, rel).mode, std::string("version"));
            QCOMPARE(chooseMode("fugue-qt", {"client", "--version"}, rel).mode, std::string("version"));
            QCOMPARE(chooseMode("dreamseeker", {"--version"}, rel).mode, std::string("version"));
            QCOMPARE(chooseMode("dreamseeker", {"byond://h:1", "--version"}, rel).mode, std::string("client"));
        }
    }
    void emptyAndNothing() {
        QVERIFY(p({}).target.empty());
        QVERIFY(p({""}).target.empty());   // DS: an empty argument leaves the target empty, so the next bare one is taken
        QCOMPARE(p({"", "h:1"}).target, std::string("h:1"));
    }
    void invokedAs() {
        QVERIFY(invokedAsDreamSeeker("dreamseeker"));
        QVERIFY(invokedAsDreamSeeker("/usr/local/bin/DreamSeeker"));
        QVERIFY(invokedAsDreamSeeker("C:\\BYOND\\bin\\dreamseeker.exe"));
        QVERIFY(invokedAsDreamSeeker("./DREAMSEEKER.EXE"));
        QVERIFY(!invokedAsDreamSeeker("fugue-qt"));
        QVERIFY(!invokedAsDreamSeeker("dreamseeker2"));
        QVERIFY(!invokedAsDreamSeeker("/opt/dreamseeker/fugue-qt"));
    }
};

QTEST_GUILESS_MAIN(PagerDsArgv)
#include "pager_dsargv_test.moc"

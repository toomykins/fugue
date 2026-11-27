// Filesystem tests run under a temporary HOME, repointed before any account/ call; override env vars are cleared.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include "tests/privatefile.h"
#include <QScopeGuard>
#include "net/guest.h"
#include "net/rscstore.h"
#include "account/keyblock.h"
#include "account/store.h"

using namespace fugue;
using namespace fugue::account;

namespace {
struct Home {
    QTemporaryDir dir;
    std::string path;
    Home() {
        path = dir.path().toStdString();
        qputenv("HOME", dir.path().toUtf8());
        for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME"})
            qunsetenv(v);
    }
    std::string p(const std::string& rel) const { return path + "/" + rel; }
};
void put(const std::string& path, const std::string& body, int mode = 0600) {
    QDir().mkpath(QFileInfo(QString::fromStdString(path)).absolutePath());
    QFile f(QString::fromStdString(path));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(body.data(), qint64(body.size()));
    f.close();
    QCOMPARE(platform::chmod(path.c_str(), mode), 0);
}
std::string slurp(const std::string& path) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) return "<unreadable>";
    return f.readAll().toStdString();
}
int modeOf(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 ? int(st.st_mode & 0777) : -1;
}
}  // namespace

class AccountFormats : public QObject {
    Q_OBJECT
private slots:
#ifdef WIN32
    void windowsProfileFallback() {
        const auto oldHome = qgetenv("HOME"), oldProfile = qgetenv("USERPROFILE");
        const bool hadHome = qEnvironmentVariableIsSet("HOME"), hadProfile = qEnvironmentVariableIsSet("USERPROFILE");
        auto restore = qScopeGuard([&] {
            if (hadHome)
                qputenv("HOME", oldHome);
            else
                qunsetenv("HOME");
            if (hadProfile)
                qputenv("USERPROFILE", oldProfile);
            else
                qunsetenv("USERPROFILE");
        });
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qunsetenv("HOME");
        qputenv("USERPROFILE", QDir::toNativeSeparators(dir.path()).toUtf8());
        QCOMPARE(homeDir().value(), dir.path().toStdString());
        QCOMPARE(fugueDir(), dir.path().toStdString() + "/.fugue");
        QCOMPARE(fugue::net::defaultStateDir(), fugueDir());
        QCOMPARE(fugue::net::RscStore::defaultDir(), fugueDir() + "/rsc");
        qputenv("HOME", (dir.path() + "/override").toUtf8());
        QCOMPARE(homeDir().value(), dir.path().toStdString() + "/override");
    }
#endif
    // An explicit HOME is the home directory on every platform (USAGE.md), so tests and overrides never reach the real profile.
    void explicitHomeWins() {
        const auto oldHome = qgetenv("HOME");
        auto restore = qScopeGuard([&] { qputenv("HOME", oldHome); });
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("HOME", dir.path().toUtf8());
        QCOMPARE(homeDir().value(), dir.path().toStdString());
        QCOMPARE(fugue::net::defaultStateDir(), dir.path().toStdString() + "/.fugue");
    }
    void atomicWriteUnicodeAndReplacement() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QString::fromUtf8("/日本語 space/clé.txt");
        std::string err;
        QVERIFY2(atomicWrite(path.toStdString(), "first\n", &err), err.c_str());
        QVERIFY(isPrivateFile(path.toStdString()));
        QVERIFY2(atomicWrite(path.toStdString(), std::string("second\r\n\0tail", 13), &err), err.c_str());
        QCOMPARE(slurp(path.toStdString()), std::string("second\r\n\0tail", 13));
        QVERIFY(isPrivateFile(path.toStdString()));
    }
    void utf8OpenAndRename() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto from = (dir.path() + QString::fromUtf8("/日本語-clé.txt")).toStdString();
        const auto to = (dir.path() + QString::fromUtf8("/renamed-日本語.txt")).toStdString();
        int fd = platform::open(from.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        QVERIFY(fd >= 0);
        auto cleanup = qScopeGuard([&] {
            if (fd >= 0) platform::close(fd);
        });
        QCOMPARE(platform::write(fd, "bytes", 5u), 5);
        const int result = platform::close(fd);
        fd = -1;
        QCOMPARE(result, 0);
        QCOMPARE(platform::rename(from.c_str(), to.c_str()), 0);
        QVERIFY(!QFile::exists(QString::fromStdString(from)));
        QCOMPARE(slurp(to), std::string("bytes"));
    }
    void identityFileFormatAndModes() {
        Home h;
        Account a{{0x00, 0xab, 0x00, 0xcd, 0xef}, 0x262781, 0x00000001};
        QVERIFY(saveAccount("Fake Tester-One", a));
        std::string p = h.p(".fugue/identity-faketesterone");
        QCOMPARE(slurp(p), std::string("identity=00ab00cdef\ninstall_id=0x00262781\ninner_key=0x00000001\n"));
        QVERIFY(isPrivateFile(p));
#ifndef _WIN32
        QCOMPARE(modeOf(h.p(".fugue")), 0700);
#endif
        auto back = loadAccount("faketesterone");
        QVERIFY(back && back->identity == a.identity && back->installId == a.installId && back->innerKey == a.innerKey);
        // wide values print without padding
        QVERIFY(saveAccount("x", Account{{1}, 0xcccc37b5u, 0x14e39ac5u}));
        QCOMPARE(slurp(h.p(".fugue/identity-x")),
                 std::string("identity=01\ninstall_id=0xcccc37b5\ninner_key=0x14e39ac5\n"));
        // refusals
        std::string err;
        QVERIFY(!saveAccount("y", Account{{}, 1, 1}, &err) && err == "identity is empty");
        QVERIFY(!saveAccount("y", Account{{1}, 0, 1}, &err) && err == "hub credentials are zero");
        // a bare-hex file is rejected, never read as constants
        put(h.p(".fugue/identity-old"), "00112233\n");
        QVERIFY(!loadAccount("old", &err) && err.find("older login") != std::string::npos);
        put(h.p(".fugue/identity-half"), "identity=0011\ninstall_id=5\n");
        QVERIFY(!loadAccount("half", &err) && err.find("incomplete") != std::string::npos);
        // base-0 parse: decimal, 0x hex, and leading-zero octal
        put(h.p(".fugue/identity-bases"), "identity=0011\ninstall_id=255\ninner_key=0xff\n");
        auto b = loadAccount("bases");
        QVERIFY(b && b->installId == 255 && b->innerKey == 255);
    }

    void identityOverrideEnv() {
        Home h;
        qputenv("FUGUE_IDENTITY", QByteArray::fromStdString(h.p("elsewhere/id")));
        QVERIFY(saveAccount("anyone", Account{{9, 9}, 3, 4}));
        QVERIFY(QFile::exists(QString::fromStdString(h.p("elsewhere/id"))));
        qunsetenv("FUGUE_IDENTITY");
    }

    void sidBorrowsFromByondAndDoesNotCollide() {
        Home h;
        const std::string machine = "aaaaaaaaaaaaaaaaaaaaaaaa";
        put(h.p(".fugue/sid"), machine);
        auto s = pagerSid();
        QVERIFY(s && s->size() == 24 && *s != machine);
        QCOMPARE(slurp(h.p(".fugue/sid")), machine);  // machine identity untouched
        QVERIFY(isPrivateFile(h.p(".fugue/byond-sid")));
        QCOMPARE(*pagerSid(), *s);  // stable
        // a BYOND install's sid wins (exactly 24 chars, two fields) and is copied into byond-sid
        put(h.p(".byond/cfg/byond.txt"), "foo bar\nsid 0123456789abcdef01234567\nsid short\n");
        auto b = pagerSid();
        QVERIFY(b && *b == "0123456789abcdef01234567");
        QCOMPARE(slurp(h.p(".fugue/byond-sid")), std::string("0123456789abcdef01234567\n"));
        // wrong-length / extra-field lines are not borrowed
        put(h.p(".byond/cfg/byond.txt"), "sid 0123456789abcdef0123456\nsid 0123456789abcdef01234567 extra\n");
        QVERIFY(!byondConfigSid());
        // the machine-identity seed has its own path and its own creation
        auto m = installSid();
        QVERIFY(m && *m == machine);
        QFile::remove(QString::fromStdString(h.p(".fugue/sid")));
        auto m2 = installSid();
        QVERIFY(m2 && m2->size() == 24 && slurp(h.p(".fugue/sid")) == *m2 + "\n");
        qputenv("XDG_STATE_HOME", QByteArray::fromStdString(h.p("state")));
        QCOMPARE(installSidPath(), h.p("state/fugue/sid"));
    }

    void timeRoundTrip() {
        QCOMPARE(formatRfc3339(Timestamp()), std::string("0001-01-01T00:00:00Z"));
        QVERIFY(parseRfc3339("0001-01-01T00:00:00Z")->isZero());
        auto t = parseRfc3339("2024-06-14T12:34:56.123456789Z");
        QVERIFY(t && t->nsec == 123456789);
        QCOMPARE(formatRfc3339(*t), std::string("2024-06-14T12:34:56.123456789Z"));
        auto z = parseRfc3339("2024-06-14T14:34:56+02:00");
        QVERIFY(z && z->sec == parseRfc3339("2024-06-14T12:34:56Z")->sec);
        QCOMPARE(formatRfc3339(*parseRfc3339("2024-06-14T12:34:56.500Z")),
                 std::string("2024-06-14T12:34:56.5Z"));  // trailing zeros are trimmed
        QCOMPARE(parseRfc3339("1970-01-01T00:00:00Z")->sec, int64_t(0));
        QCOMPARE(parseRfc3339("2000-02-29T00:00:00Z")->sec, int64_t(951782400));
        QVERIFY(!parseRfc3339("yesterday") && !parseRfc3339("2024-13-01T00:00:00Z") &&
                !parseRfc3339("2024-06-14 12:34:56Z"));
    }

    void configDefaultsAndMalformed() {
        Home h;
        auto c = loadConfig();
        QVERIFY(c && c->servers.size() == 2 && c->servers[0].hub && c->servers[1].addr == "127.0.0.1:6000");
        put(h.p(".fugue/pager.json"), "{ not json");
        std::string err;
        QVERIFY(!loadConfig(&err) && !err.empty());  // never a silent reset
        put(h.p(".fugue/pager.json"), "{\"servers\":[]}");
        QCOMPARE(int(loadConfig()->servers.size()), 2);  // empty list -> seeded
        put(h.p(".fugue/pager.json"), "{\"servers\":[{\"name\":\"a\",\"addr\":\"h:1\",\"last_played\":\"nope\"}]}");
        QVERIFY(!loadConfig(&err));
    }

    void unknownKeysSurviveASaveAndWritesAreAtomic() {
        Home h;
        // A pager.json with unknown keys at both levels.
        put(h.p(".fugue/pager.json"),
            "{\n \"account\": \"Fake\",\n \"future_top\": {\"a\": [1, 2, {\"b\": null}], \"c\": true},\n \"theme\": "
            "\"dark\",\n"
            " \"servers\": [\n  {\"name\": \"my vg\", \"addr\": \"game.example:7777\", \"hub\": true, \"flags\": "
            "[\"-webpanel\", \"embed\"],\n"
            "   \"last_played\": \"2024-06-01T10:00:00Z\", \"last_prefetch\": \"0001-01-01T00:00:00Z\", \"note\": "
            "\"keep me\", \"weights\": [3, 4]},\n"
            "  {\"name\": \"b\", \"addr\": \"127.0.0.1:6000\", \"hub\": false}\n ]\n}\n");
        auto c = loadConfig();
        QVERIFY(c);
        c->remember(Server{
            "ignored", "GAME.example:7777", true, {}, {}, {}, {}});  // case-insensitive join of an existing entry
        c->markPrefetched(Server{"", "127.0.0.1:6000", false, {}, {}, {}, {}});
        c->account = "Fake2";
        QVERIFY(saveConfig(*c));
        auto d = loadConfig();
        QVERIFY(d);
        QCOMPARE(d->account, std::string("Fake2"));
        QVERIFY(d->extra.contains("future_top") && d->extra.contains("theme"));
        QCOMPARE(d->extra.value("theme").toString(), QString("dark"));
        QCOMPARE(d->extra.value("future_top").toObject().value("a").toArray().size(), 3);
        QCOMPARE(d->servers[0].extra.value("note").toString(), QString("keep me"));
        QCOMPARE(d->servers[0].extra.value("weights").toArray().size(), 2);
        QCOMPARE(d->servers[0].name, std::string("my vg"));  // joining never renames
        QCOMPARE(d->servers[0].flags.size(), size_t(2));     // flags kept when the join carries none
        QVERIFY(!d->servers[0].lastPlayed.isZero());         // stamped
        QVERIFY(!d->servers[1].lastPrefetch.isZero());
        // on-disk shape: valid JSON, 0600, 2-space indent, no temp file left behind
        std::string raw = slurp(h.p(".fugue/pager.json"));
        QVERIFY(QJsonDocument::fromJson(QByteArray::fromStdString(raw)).isObject());
        QVERIFY(raw.rfind("{\n  \"account\": \"Fake2\",\n", 0) == 0);
        QVERIFY(isPrivateFile(h.p(".fugue/pager.json")));
        for (const auto& e : QDir(QString::fromStdString(h.p(".fugue"))).entryList(QDir::Files | QDir::Hidden))
            QVERIFY2(!e.startsWith(".tmp-"), qPrintable(e));
        // a pure round trip is idempotent byte-for-byte
        QVERIFY(saveConfig(*d));
        QCOMPARE(slurp(h.p(".fugue/pager.json")), serializeConfig(*loadConfig()));
    }

    void rememberSortedRemoveSemantics() {
        Config c;
        c.servers = {Server{"my vg", "game.example:7777", true, {}, {}, {}, {}}};
        c.remember(Server{"ignored", "game.example:7777", true, {}, {}, {}, {}});
        QCOMPARE(c.servers.size(), size_t(1));
        QCOMPARE(c.servers[0].name, std::string("my vg"));
        QVERIFY(!c.servers[0].lastPlayed.isZero());
        c.remember(Server{"", "127.0.0.1:6000", false, {}, {}, {}, {}});
        QCOMPARE(c.servers.size(), size_t(2));
        QCOMPARE(c.servers[1].name, std::string("127.0.0.1:6000"));

        Config s;
        auto now = Timestamp::now();
        Timestamp old = now;
        old.sec -= 3600;
        s.servers = {Server{"never", "a:1", false, {}, {}, {}, {}}, Server{"old", "b:2", false, {}, old, {}, {}},
                     Server{"new", "c:3", false, {}, now, {}, {}}};
        auto g = s.sorted();
        QVERIFY(g[0].name == "new" && g[1].name == "old" && g[2].name == "never");

        Config r;
        r.servers = defaultServers();
        r.remove("127.0.0.1:6000");
        QVERIFY(r.servers.size() == 1 && r.find("127.0.0.1:6000") < 0);
        r.remove("nothing:1");  // must not crash

        Config p;
        p.markPrefetched(Server{"s", "host:1", false, {}, {}, {}, {}});
        QVERIFY(!p.servers[0].lastPrefetch.isZero());
        auto stamp = p.servers[0].lastPrefetch;
        p.remember(Server{"", "host:1", false, {}, {}, {}, {}});
        QVERIFY(p.servers[0].lastPrefetch == stamp);  // join keeps the warm-up stamp
        p.clearPrefetched();
        QVERIFY(p.servers[0].lastPrefetch.isZero());
    }

    void keyNamesReadOnlyMarkers() {
        const std::string body =
            "[BEGIN KEY ExamplePlayer2]\nSECRETLINE1\nmore\n[END KEY]\n[BEGIN KEY Guest]\nSECRETLINE2\n[END KEY]\n";
        auto n = keyNames(body);
        QCOMPARE(n.size(), size_t(2));
        QCOMPARE(n[0], std::string("ExamplePlayer2"));
        QCOMPARE(n[1], std::string("Guest"));
        for (auto& x : n) QVERIFY(x.find("SECRET") == std::string::npos);
        // the unbracketed on-disk form, CRLF, odd spacing, an empty name
        auto m = keyNames(
            "BEGIN KEY  Spaced Name \r\nSECRET\r\nEND KEY\r\n  [BEGIN KEY Indented]  \nBEGIN KEY \nBEGIN KEYX nope\n");
        QCOMPARE(m.size(), size_t(2));
        QCOMPARE(m[0], std::string("Spaced Name"));
        QCOMPARE(m[1], std::string("Indented"));
    }

    void resolveKeyTxtPrefersTheFileHoldingTheAccount() {
        Home h;
        std::string native = h.p(".byond/key.txt");
        std::string wine = h.p("Games/byond/drive_c/users/someone/Documents/BYOND/key.txt");
        put(native, "BEGIN KEY Guest\nxx\n`\nEND KEY\n");
        put(wine, "BEGIN KEY Guest\nxx\n`\nEND KEY\nBEGIN KEY ExamplePlayer\nyy\n`\nEND KEY\n");
        QCOMPARE(*resolveKeyTxt("", "exampleplayer"), wine);  // case-insensitive; the file holding the block wins
        QCOMPARE(*resolveKeyTxt("", ""), native);
        QCOMPARE(*resolveKeyTxt("", "nobody"), native);
        // explicit and $FUGUE_KEYTXT come first
        std::string mine = h.p("mine/key.txt");
        put(mine, "BEGIN KEY Guest\nzz\n`\nEND KEY\n");
        QCOMPARE(*resolveKeyTxt(mine, ""), mine);
        qputenv("FUGUE_KEYTXT", QByteArray::fromStdString(wine));
        QCOMPARE(*resolveKeyTxt("", ""), wine);
        auto cands = keyTxtCandidates(mine);
        QCOMPARE(cands[0], mine);
        QCOMPARE(cands[1], wine);
        QCOMPARE(cands[2], h.p(".fugue/key.txt"));      // Fugue's own write target first
        QCOMPARE(cands[3], h.p(".byond/cfg/key.txt"));  // documented order
        QCOMPARE(cands[4], h.p(".byond/key.txt"));
        QCOMPARE(cands[5], h.p("Documents/BYOND/cfg/key.txt"));
        QCOMPARE(cands[6], h.p("Documents/BYOND/key.txt"));
        qunsetenv("FUGUE_KEYTXT");
    }

    void resolveKeyTxtReportsWhereItLooked() {
        Home h;
        std::string err;
        QVERIFY(!resolveKeyTxt("", "someone", &err));
        QVERIFY(err.find(".byond") != std::string::npos && err.find("tried:") != std::string::npos);
    }

    void keyTxtWithBlockReplacesNotDuplicates() {
        std::string ex = "BEGIN KEY Guest\nold\n`\nEND KEY\nBEGIN KEY Other\nzz\n`\nEND KEY\n";
        std::string out = keyTxtWithBlock(ex, "BEGIN KEY Guest\nnew\n`\nEND KEY\n");
        QCOMPARE(out, std::string("BEGIN KEY Other\nzz\n`\nEND KEY\nBEGIN KEY Guest\nnew\n`\nEND KEY\n"));
        QCOMPARE(keyTxtWithBlock("", "BEGIN KEY A\nx\n`\nEND KEY"), std::string("BEGIN KEY A\nx\n`\nEND KEY\n"));
    }
};

FUGUE_TEST_APPLESS_MAIN(AccountFormats)
#include "account_formats_test.moc"

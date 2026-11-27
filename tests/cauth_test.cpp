// The login-certificate cache (net/cauth.h) follows stock DreamSeeker's rule: one cached key per endpoint and state dir.
// The session half runs against an in-process fake server that answers the handshake and decrypts each login record.
// Temp dirs only.
#include "platform/socket.h"
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include <thread>
#include <vector>
#include "net/cauth.h"
#include "net/cipher.h"
#include "net/handshake.h"
#include "net/session.h"

using namespace fugue;
using namespace fugue::net;

namespace {
bool readN(socket::Handle fd, uint8_t* p, size_t n) {
    while (n) {
        auto r = socket::recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r;
        n -= size_t(r);
    }
    return true;
}

// Accepts conns connections in turn on one port; answers each handshake with a zero nonce and keeps each decrypted login record.
struct FakeServer {
    socket::Handle lfd = -1;
    uint16_t port = 0;
    std::vector<Bytes> logins;
    std::vector<Bytes> handshakes;
    std::thread th;
    uint8_t flagB = 1;  // the handshake's second flag byte; stock DD always sends 1
    explicit FakeServer(int conns = 1, uint8_t fb = 1) : flagB(fb) {
        lfd = socket::create(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        socket::Length l = sizeof a;
        ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &l);
        port = ntohs(a.sin_port);
        ::listen(lfd, 4);
        th = std::thread([this, conns] {
            for (int i = 0; i < conns; i++) serve();
        });
    }
    void serve() {
        auto fd = socket::Handle(::accept(lfd, nullptr, nullptr));
        if (fd < 0) return;
        Bytes login;
        uint8_t h[6];
        if (readN(fd, h, 4)) {
            Bytes body((size_t(h[2]) << 8) | h[3]);
            if (readN(fd, body.data(), body.size()) && body.size() >= 12) {
                handshakes.push_back(body);
                Handshake hs;
                hs.proto = le32(&body[0]);
                hs.field2 = le32(&body[4]);
                hs.field3 = le32(&body[8]);
                uint32_t base = sessionKey(hs);
                Bytes plain;
                putLe32(plain, 0x0204);
                putLe32(plain, 490);
                plain.insert(plain.end(), {1, flagB, 1});
                Bytes nonce(16, 0);
                plain.insert(plain.end(), nonce.begin(), nonce.end());
                Bytes enc = encryptBody(plain, base);
                Bytes f = {0x00, 0x01, uint8_t(enc.size() >> 8), uint8_t(enc.size())};
                f.insert(f.end(), enc.begin(), enc.end());
                socket::send(fd, f.data(), f.size());
                if (readN(fd, h, 6)) {  // c2s header: [seq BE][type BE][len BE]
                    Bytes c((size_t(h[4]) << 8) | h[5]);
                    if (readN(fd, c.data(), c.size()) && ((h[2] << 8) | h[3]) == C2S::Login)
                        decryptBody(c, loginKey(base, nonce), login);
                }
            }
        }
        socket::close(fd);
        logins.push_back(login);
    }
    ~FakeServer() {
        if (th.joinable()) th.join();
        socket::close(lfd);
    }
};

void loginTo(uint16_t port, const std::string& stateDir, Bytes accountRecord = {}, const std::string& label = "") {
    SessionConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = port;
    cfg.stateDir = stateDir;
    cfg.humanPacing = false;
    cfg.accountRecord = std::move(accountRecord);
    cfg.accountLabel = label;
    {
        Session sess(cfg);
        sess.connectAndLogin();  // post-login frames may fail once the fake server has closed
    }
}

std::string slurp(const std::string& p) {
    QFile f(QString::fromStdString(p));
    return f.open(QIODevice::ReadOnly) ? f.readAll().toStdString() : std::string();
}
}  // namespace

class CauthTest : public QObject {
    Q_OBJECT
private slots:
    void entryNameIsCkeyAtHostPort() {
        QCOMPARE(cauthEntryName("Guest", "127.0.0.1", 52516),
                 std::string("guest@127.0.0.1:52516"));  // the stock DreamSeeker line name
        QCOMPARE(cauthEntryName("Some Key_9", "play.example.org", 1337), std::string("somekey9@play.example.org:1337"));
        // The endpoint name gets ':' + port only when the URL has a port; fugue-qt always has one.
        QCOMPARE(cauthEntryName("guest", "play.example.org", std::string_view()),
                 std::string("guest@play.example.org"));
        QCOMPARE(cauthEntryName("guest", "play.example.org", "7777"), std::string("guest@play.example.org:7777"));
    }

    // Reuse the entry for this endpoint; another endpoint or state dir mints its own.
    void keyIsStablePerEndpointAndPerStateDir() {
        QTemporaryDir a, b;
        const std::string n1 = cauthEntryName("guest", "127.0.0.1", 52516),
                          n2 = cauthEntryName("guest", "127.0.0.1", 52518);
        std::string err;
        uint32_t k1 = cauthKeyFor(a.path().toStdString(), n1, &err);
        QVERIFY2(err.empty(), err.c_str());
        QVERIFY(k1 != 0 && k1 < 0x80000000u);  // the key is 31-bit and never 0
        QCOMPARE(cauthKeyFor(a.path().toStdString(), n1), k1);
        QCOMPARE(cauthKeyFor(a.path().toStdString(), n1), k1);
        uint32_t k2 = cauthKeyFor(a.path().toStdString(), n2);
        QVERIFY(k2 != k1);
        QCOMPARE(cauthKeyFor(a.path().toStdString(), n1), k1);
        QVERIFY(cauthKeyFor(b.path().toStdString(), n1) != k1);  // a fresh install mints its own key

        // BYOND's cauth.txt grammar, one line per endpoint.
        const QString text = QString::fromStdString(slurp(cauthPath(a.path().toStdString())));
        const QRegularExpression line(
            "^guest@127\\.0\\.0\\.1:5251[68] key=[0-9a-f]{1,8};pwd=03[0-9a-f]{24};ctime=[0-9a-f]+$");
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        for (auto& l : lines) QVERIFY2(line.match(l).hasMatch(), qPrintable(l));
        CauthEntry e;
        QVERIFY(cauthLookup(a.path().toStdString(), n1, &e));
        QCOMPARE(e.key, k1);
    }

    // The read path ignores ctime; a mint prunes entries older than 0x278d00 seconds.
    // The entry is back-dated 40 days: stock DreamSeeker reuses it unchanged, and a mint for another endpoint prunes it.
    void staleEntryIsReusedUntilAMintPrunesIt() {
        QTemporaryDir d;
        const std::string dir = d.path().toStdString();
        const uint64_t now = 0x6ad2418b;  // the mint time that prunes the stale entry
        {
            QFile f(QString::fromStdString(cauthPath(dir)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(
                "guest@127.0.0.1:52516 key=644788f;pwd=03000000010000000200000003;ctime=6a9d856c\r\n"  // back-dated 40 days, CRLF line ending
                "guest@127.0.0.1:52518 key=57816b99;pwd=03000000040000000500000006;ctime=6ad2410c\r\n");
        }
        QCOMPARE(cauthKeyFor(dir, "guest@127.0.0.1:52516", nullptr, now), 0x0644788fu);
        QCOMPARE(cauthKeyFor(dir, "guest@127.0.0.1:52516", nullptr, now), 0x0644788fu);
        QVERIFY(cauthKeyFor(dir, "guest@127.0.0.1:52519", nullptr, now) != 0);  // a mint, which prunes the stale line
        CauthEntry e;
        QVERIFY(!cauthLookup(dir, "guest@127.0.0.1:52516", &e));
        QVERIFY(cauthLookup(dir, "guest@127.0.0.1:52518", &e));
        QCOMPARE(e.key, 0x57816b99u);
        QCOMPARE(e.ctime, uint64_t(0x6ad2410c));
        const uint32_t fresh = cauthKeyFor(dir, "guest@127.0.0.1:52516", nullptr, now);
        QVERIFY(fresh != 0x0644788fu);
        QVERIFY(cauthLookup(dir, "guest@127.0.0.1:52516", &e));
        QCOMPARE(e.ctime, now);
    }

    // The guest credential carries the endpoint's cached key (little-endian): the same on every session from one state dir,
    // and a different one on another endpoint or state dir.
    void guestWindowIsTheCachedKey() {
        QTemporaryDir d, other;
        const std::string dir = d.path().toStdString();
        FakeServer s(3), t(1);
        loginTo(s.port, dir);
        loginTo(s.port, dir);
        loginTo(s.port, other.path().toStdString());
        loginTo(t.port, dir);
        s.th.join();
        t.th.join();
        QCOMPARE(int(s.logins.size()), 3);
        for (auto& l : s.logins) QCOMPARE(l.size(), size_t(47));
        QCOMPARE(t.logins.at(0).size(), size_t(47));
        CauthEntry e;
        QVERIFY(cauthLookup(dir, cauthEntryName("guest", "127.0.0.1", s.port), &e));
        QCOMPARE(le32(&s.logins[0][16]), e.key);
        QCOMPARE(le32(&s.logins[1][16]), e.key);   // same endpoint, same state dir: same window
        QVERIFY(le32(&s.logins[2][16]) != e.key);  // another install
        QVERIFY(le32(&t.logins[0][16]) != e.key);  // another endpoint
        QVERIFY(cauthLookup(dir, cauthEntryName("guest", "127.0.0.1", t.port), &e));
        QCOMPARE(le32(&t.logins[0][16]), e.key);
    }

    // The handshake on the wire is encodeHandshake's extended form: build 1684 in extra1's high half, a non-zero seq0.
    void handshakeBodyIsTheEncoder() {
        QTemporaryDir d;
        FakeServer s(1);
        loginTo(s.port, d.path().toStdString());
        s.th.join();
        QCOMPARE(int(s.handshakes.size()), 1);
        const Bytes& b = s.handshakes[0];
        QCOMPARE(b.size(), size_t(22));
        Handshake h;
        h.proto = le32(&b[0]);
        h.field2 = le32(&b[4]);
        h.field3 = le32(&b[8]);
        h.extended = true;
        h.extra1 = le32(&b[12]);
        h.extra2 = le32(&b[16]);
        h.tail = uint16_t(b[20] | (b[21] << 8));
        QCOMPARE(encodeHandshake(h), b);
        QCOMPARE(h.proto, 516u);
        QCOMPARE(h.field2, 276u);
        QCOMPARE(h.extra1 >> 16, 0x0694u);
        QVERIFY((h.extra1 & 0xffff) != 0);
        QCOMPARE(h.extra2, 0u);
        QCOMPARE(int(h.tail), 0x4000);
    }

    // The 0x001c request: a key minted on this connection comes back with its certStr.
    // A key read from the cache keeps its value but gets a fresh certStr, written back with ctime = now.
    void certRequestKeepsTheKeyAndOwnsTheCertStr() {
        QTemporaryDir d;
        const std::string dir = d.path().toStdString(), name = "somekey@127.0.0.1:52516";
        const uint64_t t0 = 0x6ad24000, t1 = t0 + 3600;
        CauthCache first(dir, name, t0);
        const uint32_t k = first.loginKey(true);
        CauthEntry e;
        QVERIFY(cauthLookup(dir, name, &e));
        QCOMPARE(e.key, k);
        uint32_t rk = 0;
        std::string cs;
        first.certRequest(&rk, &cs);
        QCOMPARE(rk, k);
        QCOMPARE(cs, e.pwd);            // minted with the key on this connection
        QCOMPARE(first.certStr(), cs);  // what the 0x0036 ack re-keys with
        CauthEntry e2;
        QVERIFY(cauthLookup(dir, name, &e2));
        QCOMPARE(e2.pwd, e.pwd);
        QCOMPARE(e2.ctime, t0);  // not rewritten

        CauthCache second(dir, name, t1);  // the next connection: the key comes from the cache
        QCOMPARE(second.loginKey(true), k);
        QCOMPARE(second.certStr(), e.pwd);  // as read from the cache
        second.certRequest(&rk, &cs);
        QCOMPARE(rk, k);
        QVERIFY(cs != e.pwd);  // fresh, not the cached pwd
        QVERIFY(cs.size() == 26 && cs.rfind("03", 0) == 0);
        QCOMPARE(second.certStr(), cs);
        QVERIFY(cauthLookup(dir, name, &e2));
        QCOMPARE(e2.key, k);
        QCOMPARE(e2.pwd, cs);    // written back
        QCOMPARE(e2.ctime, t1);  // ctime = now: a server you keep logging into is never pruned
    }

    // A zero flag in the handshake reply gives key 0 and leaves the cache untouched.
    // A 0x001c then mints a key and certStr and persists them. Stock DD never sends 0.
    void notEngagedSendsKeyZero() {
        QTemporaryDir d;
        const std::string dir = d.path().toStdString(), name = "guest@127.0.0.1:52516";
        {
            QFile f(QString::fromStdString(cauthPath(dir)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("guest@127.0.0.1:52516 key=644788f;pwd=03000000010000000200000003;ctime=6ad24000\n");
        }
        CauthCache c(dir, name, 0x6ad24100);
        QCOMPARE(c.loginKey(false), 0u);
        QVERIFY(c.certStr().empty());
        uint32_t k = 0;
        std::string cs;
        c.certRequest(&k, &cs);
        QVERIFY(k != 0 && k != 0x0644788fu);
        CauthEntry e;
        QVERIFY(cauthLookup(dir, name, &e));
        QCOMPARE(e.key, k);
        QCOMPARE(e.pwd, cs);

        FakeServer s(1, 0);
        loginTo(s.port, dir);
        s.th.join();
        QCOMPARE(s.logins.at(0).size(), size_t(47));
        QCOMPARE(le32(&s.logins[0][16]), 0u);  // the guest window is 0 against a flag-B-0 server
    }

    // An entry WITH a key param is used as strtol reads it: "key=0" gives 0 and does not mint; one without a key param mints.
    void keyParamPresenceDecidesTheMint() {
        QTemporaryDir d;
        const std::string dir = d.path().toStdString();
        {
            QFile f(QString::fromStdString(cauthPath(dir)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(
                "guest@a:1 key=0;pwd=03000000010000000200000003;ctime=6ad24000\n"
                "guest@b:1 pwd=03000000010000000200000003;ctime=6ad24000\n");
        }
        QCOMPARE(cauthKeyFor(dir, "guest@a:1", nullptr, 0x6ad24100), 0u);
        const uint32_t kb = cauthKeyFor(dir, "guest@b:1", nullptr, 0x6ad24100);
        QVERIFY(kb != 0);
        CauthEntry e;
        QVERIFY(cauthLookup(dir, "guest@b:1", &e));
        QCOMPARE(e.key, kb);
    }

    // An account record's cauthKey is stamped from the same cache, under the account's ckey.
    void accountRecordCarriesTheCachedKey() {
        QTemporaryDir d;
        const std::string dir = d.path().toStdString();
        Bytes rec(16 + 43, 0);  // a 16-char account: len(account)+43
        rec[rec.size() - 6] = 0x16;
        FakeServer s(2);
        loginTo(s.port, dir, rec, "somekey");
        loginTo(s.port, dir, rec, "somekey");
        s.th.join();
        QCOMPARE(int(s.logins.size()), 2);
        const Bytes& got = s.logins[0];
        QCOMPARE(s.logins[1], got);  // the second session reuses the key
        QCOMPARE(got.size(), rec.size());
        const QStringList lines = QString::fromStdString(slurp(cauthPath(dir))).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines[0].startsWith("somekey@127.0.0.1:"), qPrintable(lines[0]));
        CauthEntry e;
        QVERIFY(cauthLookup(dir, lines[0].section(' ', 0, 0).toStdString(), &e));
        QCOMPARE(le32(&got[got.size() - 27]), e.key);
        for (size_t i = 0; i < got.size(); i++)
            if (i < got.size() - 27 || i >= got.size() - 23) QCOMPARE(got[i], rec[i]);  // nothing else touched
    }
};
FUGUE_TEST_MAIN(CauthTest)
#include "cauth_test.moc"

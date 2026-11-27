// pager_signin: the sign-in flow end to end against a fake hub on loopback, with a temp HOME and a synthetic account.
// The fake hub answers the download-key request with a block encoded under the password, or with a refusal.
#include <QtTest>
#include "tests/testmain.h"
#include <QTemporaryDir>
#include "platform/socket.h"
#include "tests/privatefile.h"
#include <atomic>
#include <thread>
#include "account/desc.h"
#include "account/keyblock.h"
#include "account/records.h"
#include "account/store.h"
#include "pager/model.h"
#include "pager/signin.h"

using namespace fugue;

namespace {
enum class Mode { Ok, Refuse, HubReply, CloseAfterRequest };

struct FakeHub {
    socket::Handle lfd = -1;
    int port = 0;
    std::thread th;
    Mode mode = Mode::Ok;
    std::string canonical = "Synthetic Tester";  // the hub's casing
    std::string password;
    uint32_t nonce = 0x5a17c0de;
    Bytes gotHello, gotRequest;
    std::atomic<bool> done{false};

    void start() {
        lfd = socket::create(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd, (sockaddr*)&a, sizeof a);
        ::listen(lfd, 1);
        socket::Length l = sizeof a;
        ::getsockname(lfd, (sockaddr*)&a, &l);
        port = ntohs(a.sin_port);
        th = std::thread([this] { serve(); });
    }
    std::string addr() const { return "127.0.0.1:" + std::to_string(port); }
    static bool readN(socket::Handle fd, uint8_t* b, size_t n) {
        size_t o = 0;
        while (o < n) {
            auto r = socket::recv(fd, b + o, n - o, 0);
            if (r <= 0) return false;
            o += size_t(r);
        }
        return true;
    }
    static bool readFrame(socket::Handle fd, uint16_t* t, Bytes* p) {
        uint8_t h[4];
        if (!readN(fd, h, 4)) return false;
        *t = uint16_t(h[0] << 8 | h[1]);
        p->assign(size_t(h[2] << 8 | h[3]), 0);
        return p->empty() || readN(fd, p->data(), p->size());
    }
    static void writeFrame(socket::Handle fd, uint16_t t, const Bytes& p) {
        Bytes w{uint8_t(t >> 8), uint8_t(t), uint8_t(p.size() >> 8), uint8_t(p.size())};
        w.insert(w.end(), p.begin(), p.end());
        socket::send(fd, w.data(), w.size());
    }
    void serve() {
        auto c = socket::Handle(::accept(lfd, nullptr, nullptr));
        if (c < 0) return;
        Bytes ch(20, 0);
        for (int i = 0; i < 12; i++) ch[size_t(i)] = uint8_t(0x30 + i);
        ch[12] = uint8_t(nonce);
        ch[13] = uint8_t(nonce >> 8);
        ch[14] = uint8_t(nonce >> 16);
        ch[15] = uint8_t(nonce >> 24);
        writeFrame(c, 0x0042, ch);
        uint16_t t;
        Bytes p;
        if (readFrame(c, &t, &p) && t == 0x00db) gotHello = p;
        if (readFrame(c, &t, &p) && t == 0x0088) gotRequest = p;
        if (mode == Mode::CloseAfterRequest) {
            socket::close(c);
            done = true;
            return;
        }
        if (mode == Mode::HubReply) {
            Bytes body{7};
            for (char ch2 : std::string("try later")) body.push_back(uint8_t(ch2));
            body.push_back(0);
            uint8_t sum = account::hubScramble(body, nonce);
            body.push_back(sum);
            writeFrame(c, 0x0043, body);
        } else {
            Bytes plain{uint8_t(mode == Mode::Ok ? 1 : 0)};
            if (mode == Mode::Ok) {
                ByteSpan pw(reinterpret_cast<const uint8_t*>(password.data()), password.size());
                Bytes ident(canonical.size() + 13);
                for (size_t i = 0; i < ident.size(); i++) ident[i] = uint8_t(1 + (i * 37) % 250);
                Bytes blob = account::buildKeyBlob(ident, 0x00262781, 0x1badb002, Bytes{9, 8, 7});
                std::string block = "BEGIN KEY " + canonical + "\n" +
                                    account::encodeKeyLine1(blob, account::pwordCryptLevel2(pw)) + "\n`\nEND KEY\n";
                plain.insert(plain.end(), block.begin(), block.end());
            }
            plain.push_back(0);
            uint8_t sum = account::hubScramble(plain, nonce);
            plain.push_back(sum);
            writeFrame(c, 0x0088, plain);
        }
        Bytes rest;
        readFrame(c, &t, &rest);  // wait for the client to hang up
        socket::close(c);
        done = true;
    }
    ~FakeHub() {
        if (th.joinable()) th.join();
        if (lfd >= 0) socket::close(lfd);
    }
};

const char* kSid = "0123456789abcdef01234567";
}  // namespace

class PagerSignIn : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    void writeSid() {
        QDir().mkpath(home_.path() + "/.fugue");
        QFile f(home_.path() + "/.fugue/byond-sid");
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
        QCOMPARE(f.write(QByteArray(kSid) + "\n"), qint64(QByteArray(kSid).size() + 1));
    }
private slots:
    void init() {
        QVERIFY(home_.isValid());
        qputenv("HOME", home_.path().toUtf8());
        for (auto v : {"FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY", "FUGUE_BYOND_SID", "FUGUE_KEYTXT", "XDG_STATE_HOME"})
            qunsetenv(v);
        QDir(home_.path()).removeRecursively();
        QDir().mkpath(home_.path());
        writeSid();
    }

    void signInWritesKeyTxtAndIdentity() {
        FakeHub hub;
        hub.password = "correct horse";
        hub.start();
        auto r = pager::signIn("synthetic tester", hub.password, "", hub.addr(), 5000);
        QVERIFY2(r.ok, r.detail.c_str());
        QCOMPARE(r.account, std::string("Synthetic Tester"));  // the hub's casing wins
        // The key file lives in Fugue's own directory, not ~/.byond/cfg.
        QCOMPARE(r.keyTxtPath, (home_.path() + "/.fugue/key.txt").toStdString());
        QVERIFY(!QFile::exists(home_.path() + "/.byond/cfg/key.txt"));
        // The 0x0088 request carries the username as typed and the password hashes.
        QCOMPARE(hub.gotHello, pager::hubHelloPayload());
        ByteSpan pw(reinterpret_cast<const uint8_t*>(hub.password.data()), hub.password.size());
        QCOMPARE(hub.gotRequest,
                 account::buildDownloadKeyRequest("synthetic tester", "dantom", account::pwordCryptLevel2(pw),
                                                  account::pwordCrypt(pw), kSid, hub.nonce));
        // the identity the client reads
        auto a = account::loadAccount("Synthetic Tester");
        QVERIFY(a.has_value());
        QCOMPARE(a->installId, 0x00262781u);
        QCOMPARE(a->innerKey, 0x1badb002u);
        QCOMPARE(a->identity.size(), std::string("Synthetic Tester").size() + 13);
        QVERIFY(isPrivateFile(account::identityPath("Synthetic Tester")));
        QVERIFY(isPrivateFile(r.keyTxtPath));
        // the pager now considers the account ready: a hub server is joinable
        account::Config cfg;
        cfg.account = r.account;
        cfg.servers = account::defaultServers();
        qputenv("FUGUE_CLIENT", QCoreApplication::applicationFilePath().toUtf8());
        auto status = pager::check(cfg);
        QVERIFY(status.hasIdentity);
        QVERIFY(status.hasKey());
        QVERIFY(!pager::blocker(status, cfg.servers[0]));
        qunsetenv("FUGUE_CLIENT");
        // the password is in no file under HOME
        QDirIterator it(home_.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.errorString()));
            QVERIFY2(!f.readAll().contains("correct horse"), qPrintable(f.fileName()));
        }
    }

    void signInReplacesTheBlockInAnExistingKeyTxt() {
        QDir().mkpath(home_.path() + "/.byond");
        QFile f(home_.path() + "/.byond/key.txt");  // an existing ~/.byond/key.txt is the write target
        QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(f.errorString()));
        f.write("BEGIN KEY Other\nabc\n`\nEND KEY\nBEGIN KEY Synthetic Tester\nstale\n`\nEND KEY\n");
        f.close();
        FakeHub hub;
        hub.password = "pw2";
        hub.start();
        auto r = pager::signIn("Synthetic Tester", hub.password, "", hub.addr(), 5000);
        QVERIFY2(r.ok, r.detail.c_str());
        QCOMPARE(r.keyTxtPath, (home_.path() + "/.byond/key.txt").toStdString());
        QFile g(QString::fromStdString(r.keyTxtPath));
        QVERIFY2(g.open(QIODevice::ReadOnly), qPrintable(g.errorString()));
        QByteArray body = g.readAll();
        QVERIFY(body.startsWith("BEGIN KEY Other\nabc\n`\nEND KEY\nBEGIN KEY Synthetic Tester\n"));
        QVERIFY(!body.contains("stale"));
        QCOMPARE(body.count("BEGIN KEY Synthetic Tester"), 1);
    }

    void wrongPasswordIsPlain() {
        FakeHub hub;
        hub.mode = Mode::Refuse;
        hub.start();
        auto r = pager::signIn("someone", "nope", "", hub.addr(), 5000);
        QVERIFY(!r.ok);
        QCOMPARE(int(r.failure), int(pager::SignInFailure::WrongPassword));
        QCOMPARE(pager::plainSignInMessage(r.failure), std::string("Wrong username or password."));
        QVERIFY(r.detail.find("nope") == std::string::npos);
        QVERIFY(!account::loadAccount("someone"));
    }

    void hubReplyRefusalAndSilentCloseArePlain() {
        {
            FakeHub hub;
            hub.mode = Mode::HubReply;
            hub.start();
            auto r = pager::signIn("someone", "pw", "", hub.addr(), 5000);
            QCOMPARE(int(r.failure), int(pager::SignInFailure::Refused));
            QVERIFY2(r.detail.find("code=7") != std::string::npos && r.detail.find("try later") != std::string::npos,
                     r.detail.c_str());
        }
        {
            FakeHub hub;
            hub.mode = Mode::CloseAfterRequest;
            hub.start();
            auto r = pager::signIn("someone", "pw", "", hub.addr(), 5000);
            QCOMPARE(int(r.failure), int(pager::SignInFailure::NoAnswer));
        }
    }

    void unreachableIsPlain() {
        // a port nobody listens on
        auto s = socket::create(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(s, (sockaddr*)&a, sizeof a);
        socket::Length l = sizeof a;
        ::getsockname(s, (sockaddr*)&a, &l);
        int port = ntohs(a.sin_port);
        socket::close(s);
        auto r = pager::signIn("someone", "pw", "", "127.0.0.1:" + std::to_string(port), 3000);
        QCOMPARE(int(r.failure), int(pager::SignInFailure::Unreachable));
        QCOMPARE(pager::plainSignInMessage(r.failure),
                 std::string("Could not reach BYOND. Check your internet connection and try again."));
        r = pager::signIn("", "pw", "", "127.0.0.1:1", 1000);
        QCOMPARE(int(r.failure), int(pager::SignInFailure::MissingInput));
    }
};
FUGUE_TEST_GUILESS_MAIN(PagerSignIn)
#include "pager_signin_test.moc"

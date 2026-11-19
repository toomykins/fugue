// The map's frame scheduling (app/mapcadence.h) and the session wake it relies on (net::Session::wake):
//  * a still map idles at MapCadence::kIdle, but a frame ingested while idling makes the next assembly due one motion
//    frame after the last, so a glide that starts from rest is not drawn up to kIdle late;
//  * a frame that wakes the session ends the run() in progress, so the session thread reaches that assembly instead
//    of sleeping out the rest of its socket budget. Checked against an in-process fake server.
// The call site that joins them (the session observer in app/sessionworker.cpp) is not covered here.
#include "platform/socket.h"
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>
#include "app/mapcadence.h"
#include "net/cipher.h"
#include "net/handshake.h"
#include "net/session.h"

using namespace fugue;
using namespace fugue::net;
using namespace std::chrono_literals;

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

// Answers the handshake, reads the login, then sends one 0x7777 frame after `delay` and holds the connection open
// until `done`.
struct OneFrameServer {
    socket::Handle lfd = -1, fd = -1;
    uint16_t port = 0;
    std::thread th;
    std::chrono::milliseconds delay;
    std::atomic<bool> done{false};

    explicit OneFrameServer(std::chrono::milliseconds d) : delay(d) {
        lfd = socket::create(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(lfd, reinterpret_cast<sockaddr*>(&a), sizeof a);
        socket::Length l = sizeof a;
        ::getsockname(lfd, reinterpret_cast<sockaddr*>(&a), &l);
        port = ntohs(a.sin_port);
        ::listen(lfd, 1);
        th = std::thread([this] { serve(); });
    }
    void sendS2c(uint16_t type, const Bytes& plain, uint32_t key) {
        Bytes enc = encryptBody(plain, key);
        Bytes f = {uint8_t(type >> 8), uint8_t(type), uint8_t(enc.size() >> 8), uint8_t(enc.size())};
        f.insert(f.end(), enc.begin(), enc.end());
        socket::send(fd, f.data(), f.size());
    }
    void serve() {
        fd = socket::Handle(::accept(lfd, nullptr, nullptr));
        if (fd < 0) return;
        uint8_t h[6];
        Bytes body;
        if (readN(fd, h, 4)) {
            body.resize((size_t(h[2]) << 8) | h[3]);
            if (!readN(fd, body.data(), body.size()) || body.size() < 22) body.clear();
        }
        if (!body.empty()) {
            Handshake hs;
            hs.proto = le32(&body[0]);
            hs.field2 = le32(&body[4]);
            hs.field3 = le32(&body[8]);
            uint32_t base = sessionKey(hs);
            Bytes plain;
            putLe32(plain, 0x0204);
            putLe32(plain, 490);
            plain.insert(plain.end(), {1, 1, 1});
            Bytes nonce(16, 0);
            plain.insert(plain.end(), nonce.begin(), nonce.end());
            sendS2c(S2C::Handshake, plain, base);
            uint32_t k = loginKey(base, nonce);
            Bytes login;
            if (readN(fd, h, 6)) {
                login.resize((size_t(h[4]) << 8) | h[5]);
                if (readN(fd, login.data(), login.size())) {
                    std::this_thread::sleep_for(delay);
                    sendS2c(0x7777, Bytes{1, 2, 3}, k);
                }
            }
        }
        while (!done) std::this_thread::sleep_for(10ms);
        socket::close(fd);
    }
    ~OneFrameServer() {
        done = true;
        if (th.joinable()) th.join();
        socket::close(lfd);
    }
};
}  // namespace

class MapCadenceTest : public QObject {
    Q_OBJECT
private slots:
    void aFrameIngestedWhileIdleIsDrawnOneMotionFrameAfterTheLast() {
        MapCadence c;
        const auto t0 = MapCadence::Clock::now();
        c.assembled(t0, false);
        QCOMPARE(c.next, t0 + MapCadence::kIdle);
        QVERIFY(c.ingested());
        QCOMPARE(c.next, t0 + MapCadence::kMotion);
        QVERIFY(!c.ingested());  // once per idle period: the rest of the burst wakes nothing
        QCOMPARE(c.next, t0 + MapCadence::kMotion);

        // A frame arriving long after the last assembly is due at once (next is already past).
        c.assembled(t0, false);
        QVERIFY(c.ingested());
        QVERIFY(c.next <= t0 + 200ms);
    }

    void aMovingMapKeepsItsCadence() {
        MapCadence c;
        const auto t0 = MapCadence::Clock::now();
        c.assembled(t0, true);
        QCOMPARE(c.next, t0 + MapCadence::kMotion);
        QVERIFY(!c.ingested());
        QCOMPARE(c.next, t0 + MapCadence::kMotion);
        c.assembled(t0 + MapCadence::kMotion, false);  // the motion ended: idle again
        QCOMPARE(c.next, t0 + MapCadence::kMotion + MapCadence::kIdle);
    }

    void aWakingFrameEndsTheRunInProgress() {
        QTemporaryDir td;
        OneFrameServer srv(300ms);
        SessionConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = srv.port;
        cfg.stateDir = td.path().toStdString();
        cfg.humanPacing = false;
        Session s(cfg);
        QCOMPARE(QString::fromStdString(s.connectAndLogin()), QString());
        bool seen = false;
        s.setObserver([&](uint16_t type, ByteSpan) {
            if (type != 0x7777) return;
            seen = true;
            s.wake();
        });
        auto t = std::chrono::steady_clock::now();
        s.run(5000ms);
        auto took = std::chrono::steady_clock::now() - t;
        QVERIFY(seen);
        QVERIFY2(took < 2000ms,
                 qPrintable(QString("run() took %1 ms after the waking frame")
                                .arg(std::chrono::duration_cast<std::chrono::milliseconds>(took).count())));

        // Without a wake, run() keeps its budget.
        t = std::chrono::steady_clock::now();
        s.run(200ms);
        QVERIFY(std::chrono::steady_clock::now() - t >= 200ms);
        s.close();
    }
};

QTEST_GUILESS_MAIN(MapCadenceTest)
#include "mapcadence_test.moc"

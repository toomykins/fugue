// Session liveness and reconnect (net/session.h, net/reconnect.h), against an in-process fake server:
//  * DreamDaemon probes a connection that has sent nothing for about 10 minutes with s2c 0x008f and ends it ("Connection
//    timed out.") unless the client answers 0x0090 with the same body, as stock DreamSeeker 516 does.
//  * After a world reboot (s2c 0x001e) the stock client handshakes again on the same connection: a sequenced 0x0001 under
//    the current key with a 20-byte body (no seq0), then the login under the key the new reply derives.
//  * reconnectStep follows stock DreamSeeker's rules for a lost, kicked, timed-out and rebooted session.
#include "platform/socket.h"
#include <QTemporaryDir>
#include <QtTest>
#include "tests/testmain.h"
#include <thread>
#include "net/cipher.h"
#include "net/handshake.h"
#include "net/reconnect.h"
#include "net/seq.h"
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

struct C2s {
    uint16_t seq = 0, type = 0;
    Bytes body;
};

struct ScriptedServer {
    socket::Handle lfd = -1, fd = -1;
    uint16_t port = 0;
    std::thread th;
    uint16_t seq0 = 0;
    C2s login1, probeAnswer, rehandshake, login2;
    bool login1Ok = false, probeOk = false, rehandshakeOk = false, login2Ok = false;
    Bytes rehandshakePlain;

    ScriptedServer() {
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
    bool readC2s(C2s& c) {
        uint8_t h[6];
        if (!readN(fd, h, 6)) return false;
        c.seq = uint16_t((h[0] << 8) | h[1]);
        c.type = uint16_t((h[2] << 8) | h[3]);
        c.body.resize((size_t(h[4]) << 8) | h[5]);
        return readN(fd, c.body.data(), c.body.size());
    }
    void sendS2c(uint16_t type, const Bytes& plain, uint32_t key) {
        Bytes enc = encryptBody(plain, key);
        Bytes f = {uint8_t(type >> 8), uint8_t(type), uint8_t(enc.size() >> 8), uint8_t(enc.size())};
        f.insert(f.end(), enc.begin(), enc.end());
        socket::send(fd, f.data(), f.size());
    }
    uint32_t reply(const Handshake& hs) {  // the handshake reply under the handshake's base key; returns K_login
        uint32_t base = sessionKey(hs);
        Bytes plain;
        putLe32(plain, 0x0204);
        putLe32(plain, 490);
        plain.insert(plain.end(), {1, 1, 1});
        Bytes nonce(16, 0);
        plain.insert(plain.end(), nonce.begin(), nonce.end());
        sendS2c(S2C::Handshake, plain, base);
        return loginKey(base, nonce);
    }
    void serve() {
        fd = socket::Handle(::accept(lfd, nullptr, nullptr));
        if (fd < 0) return;
        uint8_t h[4];
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
            seq0 = uint16_t(body[12] | (body[13] << 8));
            uint32_t k = reply(hs);
            Bytes p;
            login1Ok = readC2s(login1) && decryptBody(login1.body, k, p);
            sendS2c(S2C::Probe, Bytes{7, 8, 9}, k);
            probeOk = readC2s(probeAnswer) && decryptBody(probeAnswer.body, k, p) && p == Bytes{7, 8, 9};
            sendS2c(S2C::Reboot, Bytes{}, k);
            rehandshakeOk = readC2s(rehandshake) && decryptBody(rehandshake.body, k, rehandshakePlain) &&
                            rehandshakePlain.size() == 20;
            if (rehandshakeOk) {
                Handshake h2;
                h2.proto = le32(&rehandshakePlain[0]);
                h2.field2 = le32(&rehandshakePlain[4]);
                h2.field3 = le32(&rehandshakePlain[8]);
                uint32_t k2 = reply(h2);
                login2Ok = readC2s(login2) && decryptBody(login2.body, k2, p);
            }
        }
        socket::close(fd);
    }
    ~ScriptedServer() {
        if (th.joinable()) th.join();
        socket::close(lfd);
    }
};
}  // namespace

class ReconnectTest : public QObject {
    Q_OBJECT
private slots:
    void probeIsAnsweredAndRebootLogsInAgainOnTheSameConnection() {
        QTemporaryDir td;
        ScriptedServer srv;
        SessionConfig cfg;
        cfg.host = "127.0.0.1";
        cfg.port = srv.port;
        cfg.stateDir = td.path().toStdString();
        cfg.humanPacing = false;
        Session s(cfg);
        QCOMPARE(QString::fromStdString(s.connectAndLogin()), QString());
        for (int i = 0; i < 50 && !s.stats().rebootRequested; i++) s.run(std::chrono::milliseconds(100));
        QVERIFY(s.stats().rebootRequested);
        QVERIFY(s.stats().c2s.count(C2S::ProbeEcho));
        QCOMPARE(QString::fromStdString(s.relogin()), QString());
        QVERIFY(!s.stats().rebootRequested);
        QVERIFY(s.stats().connected);
        s.close();
        srv.th.join();

        QVERIFY(srv.login1Ok);
        QCOMPARE(srv.login1.type, uint16_t(C2S::Login));
        QCOMPARE(srv.login1.seq, srv.seq0);
        QVERIFY2(srv.probeOk, "0x008f not answered with 0x0090 carrying its body");
        QCOMPARE(srv.probeAnswer.type, uint16_t(C2S::ProbeEcho));
        QCOMPARE(srv.probeAnswer.seq, nextSeq(srv.seq0));
        QVERIFY2(srv.rehandshakeOk, "no sequenced 20-byte 0x0001 under the current key after 0x001e");
        QCOMPARE(srv.rehandshake.type, uint16_t(C2S::Handshake));
        QCOMPARE(srv.rehandshake.seq, nextSeq(nextSeq(srv.seq0)));
        const Bytes& b = srv.rehandshakePlain;
        QCOMPARE(le32(&b[0]), 516u);
        QCOMPARE(le32(&b[4]), 276u);
        QCOMPARE(uint16_t(b[12] | (b[13] << 8)), uint16_t(0x0694));
        QCOMPARE(le32(&b[14]), 0u);
        QCOMPARE(uint16_t(b[18] | (b[19] << 8)), uint16_t(0x4000));
        QVERIFY2(srv.login2Ok, "the login after the reboot did not decrypt under the new key");
        QCOMPARE(srv.login2.type, uint16_t(C2S::Login));
        QCOMPARE(srv.login2.seq, nextSeq(nextSeq(nextSeq(srv.seq0))));
    }

    void reconnectRules() {
        SessionEnding inWorld;
        inWorld.connected = inWorld.loggedIn = inWorld.accepted = true;

        SessionEnding reboot = inWorld;
        reboot.reboot = true;
        ReconnectStep r = reconnectStep(reboot, false, false);
        QCOMPARE(r.delayMs, 0);
        QVERIFY(r.sameConnection);
        QVERIFY(r.message.empty());
        QVERIFY(!reconnectStep(reboot, false, true).sameConnection);  // .reconnect always dials again

        r = reconnectStep(inWorld, true, false);  // lost without a reason: one retry after 10 s
        QCOMPARE(r.delayMs, 10000);
        QVERIFY(!r.sameConnection);
        QCOMPARE(
            QString::fromStdString(r.message),
            QString("Connection failed after handshake. (code 0: no/unknown reason)\nReconnecting in 10 seconds..."));

        SessionEnding failedRelogin;
        failedRelogin.connected = failedRelogin.afterReboot = true;
        QCOMPARE(reconnectStep(failedRelogin, false, false).delayMs, 10000);

        SessionEnding kicked = inWorld;  // del(client): 0x0000 reason 1; an idle timeout: reason 8
        kicked.refused = true;
        r = reconnectStep(kicked, false, false);
        QCOMPARE(r.delayMs, -1);
        QVERIFY(r.message.empty());

        SessionEnding neverIn;  // a ban: closed before 0x0071
        neverIn.connected = neverIn.loggedIn = true;
        QCOMPARE(reconnectStep(neverIn, true, false).delayMs, -1);
        QVERIFY(reconnectStep(neverIn, true, false).message.empty());
        r = reconnectStep(neverIn, false, false);  // the retry failed: no second one
        QCOMPARE(r.delayMs, -1);
        QCOMPARE(QString::fromStdString(r.message),
                 QString("Connection failed during handshake. (code 0: no/unknown reason)"));
        QCOMPARE(QString::fromStdString(reconnectStep(SessionEnding{}, false, false).message),
                 QString("Connection failed."));

        QCOMPARE(reconnectStep(kicked, false, true).delayMs, 0);  // .reconnect works after any ending

        // A link() joins its target; when that join's handshake fails (vg's world is rebooting), it is
        // retried once 10 s later. With no server there at all, it is not.
        SessionEnding linkJoin;
        linkJoin.connected = linkJoin.afterLink = true;
        r = reconnectStep(linkJoin, false, false);
        QCOMPARE(r.delayMs, 10000);
        QVERIFY(!r.sameConnection);
        QCOMPARE(
            QString::fromStdString(r.message),
            QString("Connection failed during handshake. (code 0: no/unknown reason)\nReconnecting in 10 seconds..."));
        SessionEnding linkNoServer;
        linkNoServer.afterLink = true;
        QCOMPARE(reconnectStep(linkNoServer, false, false).delayMs, -1);
        SessionEnding linkRefused = linkJoin;
        linkRefused.refused = true;
        QCOMPARE(reconnectStep(linkRefused, false, false).delayMs, -1);
    }
};

FUGUE_TEST_MAIN(ReconnectTest)
#include "reconnect_test.moc"

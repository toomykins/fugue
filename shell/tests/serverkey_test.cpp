// shell/serverkey.h: a link() to the same server keeps the per-server macro key; another server changes it.
#include <QSignalSpy>
#include <QtTest>
#include "shell/serverkey.h"

using fugue::shell::ServerKey;

class ServerKeyTest : public QObject {
    Q_OBJECT
private slots:
    void addressLists() {
        using A = QHostAddress;
        QVERIFY(fugue::shell::sameServerAddresses({A("10.1.2.3"), A("10.9.9.9")}, {A("10.9.9.9")}));
        QVERIFY(fugue::shell::sameServerAddresses({A("::ffff:10.1.2.3")}, {A("10.1.2.3")}));
        QVERIFY(fugue::shell::sameServerAddresses({A("127.0.1.1")}, {A("::1")}));  // loopback both sides
        QVERIFY(!fugue::shell::sameServerAddresses({A("10.1.2.3")}, {A("10.1.2.4")}));
        QVERIFY(!fugue::shell::sameServerAddresses({}, {A("10.1.2.3")}));  // a name that did not resolve
    }
    void sameNameOrLoopbackKeepsTheKeyAtOnce() {
        ServerKey k("LocalHost", 47600);
        QSignalSpy changed(&k, &ServerKey::changed), kept(&k, &ServerKey::kept);
        k.moved("localhost", 47600);
        k.moved("127.0.0.1", 47600);
        QCOMPARE(kept.count(), 2);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(k.host(), QString("LocalHost"));
    }
    void anotherPortIsAnotherServer() {
        ServerKey k("play.example.org", 7777);
        QSignalSpy changed(&k, &ServerKey::changed);
        k.moved("play.example.org", 7778);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(k.port(), quint16(7778));
    }
    // The resolved path: two spellings of one address keep the key, two addresses change it.
    void resolvedAddressesDecide() {
        ServerKey k("10.1.2.3", 7777);
        QSignalSpy changed(&k, &ServerKey::changed), kept(&k, &ServerKey::kept);
        k.moved("::ffff:10.1.2.3", 7777);
        QVERIFY(kept.wait(20000));
        QCOMPARE(changed.count(), 0);
        QCOMPARE(k.host(), QString("10.1.2.3"));
        k.moved("10.1.2.4", 7777);
        QVERIFY(changed.wait(20000));
        QCOMPARE(k.host(), QString("10.1.2.4"));
    }
    void aLaterMoveOvertakesALookup() {
        ServerKey k("10.1.2.3", 7777);
        QSignalSpy changed(&k, &ServerKey::changed), kept(&k, &ServerKey::kept);
        k.moved("10.1.2.4", 7777);  // would change the key once resolved
        k.moved("10.1.2.3", 7777);  // back at the keyed server before that
        QCOMPARE(kept.count(), 1);
        QTest::qWait(1500);
        QCOMPARE(changed.count(), 0);
    }
};

QTEST_GUILESS_MAIN(ServerKeyTest)
#include "serverkey_test.moc"

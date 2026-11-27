// pager_target: parseTarget table: the shapes DreamSeeker's parser accepts, plus the refusals.
#include <QtTest>
#include "tests/testmain.h"
#include "pager/target.h"

using namespace fugue::pager;

class PagerTarget : public QObject {
    Q_OBJECT
private slots:
    void accepts_data() {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("want");
        QTest::newRow("byond") << "byond://game.example:7777" << "game.example:7777";
        QTest::newRow("connect") << "+connect byond://host:6000" << "host:6000";
        QTest::newRow("connect bare") << "+CONNECT   host:6000" << "host:6000";
        QTest::newRow("bare") << "game.example:7777" << "game.example:7777";
        QTest::newRow("trailing slash") << "byond://127.0.0.1:6000/" << "127.0.0.1:6000";
        QTest::newRow("topic") << "byond://h:1#topic" << "h:1";
        QTest::newRow("ipv6") << "[::1]:6000" << "[::1]:6000";
        QTest::newRow("spaces") << "  BYOND://h:65535 " << "h:65535";
    }
    void accepts() {
        QFETCH(QString, in);
        QFETCH(QString, want);
        std::string err;
        auto r = parseTarget(in.toStdString(), &err);
        QVERIFY2(r, err.c_str());
        QCOMPARE(QString::fromStdString(*r), want);
    }
    void refuses_data() {
        QTest::addColumn<QString>("in");
        QTest::addColumn<QString>("needle");
        QTest::newRow("empty") << "" << "no server address given";
        QTest::newRow("world ref") << "byond://Example.World" << "hub WORLD reference";
        QTest::newRow("world ref advice")
            << "Example.World" << "Fugue does not look hub worlds up by name. Use the server's host:port";
        QTest::newRow("no port") << "127.0.0.1" << "has no port";
        QTest::newRow("no host") << ":6000" << "has no host";
        QTest::newRow("bad port") << "h:0" << "bad port";
        QTest::newRow("big port") << "h:65536" << "bad port";
        QTest::newRow("word port") << "h:abc" << "bad port";
    }
    void refuses() {
        QFETCH(QString, in);
        QFETCH(QString, needle);
        std::string err;
        QVERIFY(!parseTarget(in.toStdString(), &err));
        QVERIFY2(QString::fromStdString(err).contains(needle), err.c_str());
    }
};
FUGUE_TEST_APPLESS_MAIN(PagerTarget)
#include "pager_target_test.moc"

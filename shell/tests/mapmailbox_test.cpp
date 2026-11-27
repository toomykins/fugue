#include <QtTest>
#include "tests/testmain.h"
#include <atomic>
#include <thread>
#include "shell/mapmailbox.h"
using namespace fugue::shell;
class MapMailboxTest : public QObject {
    Q_OBJECT
private slots:
    void namedRoutes() {
        MapRoutes routes;
        auto a = routes.subscribe("preview");
        auto b = routes.subscribe("other");
        auto snapshot = routes.snapshot();
        QCOMPARE(snapshot.size(), size_t(2));
        const auto oldId = snapshot[0].id;
        QVERIFY(a->publish({}, QRect(1,0,32,32), 1, 2));
        QVERIFY(!b->forRender(0));
        QVERIFY(a->forRender(0));
        snapshot.clear(); a.reset();
        QCOMPARE(routes.snapshot().size(), size_t(1));
        a = routes.subscribe("preview");
        snapshot = routes.snapshot();
        QCOMPARE(snapshot.size(), size_t(2));
        QVERIFY(snapshot.back().id != oldId);
        QVERIFY(!a->forRender(0)); // no retained frame from the previous control
        snapshot.clear();
        std::vector<std::shared_ptr<MapMailbox>> held;
        for (int i=0;i<14;++i) { held.push_back(routes.subscribe(QString::number(i))); QVERIFY(held.back()); }
        QVERIFY(!routes.subscribe("over-budget"));
        held.clear();
        QVERIFY(routes.subscribe("available-again"));
    }
    void coalescesAndPullsNewest() {
        auto metrics = std::make_shared<MapMetrics>(1);
        MapMailbox mailbox(metrics);
        QImage terrain(64, 32, QImage::Format_RGBA8888);
        terrain.fill(Qt::green);
        QVERIFY(mailbox.publish(terrain, QRect(0, 0, 32, 32), 10, 20));
        for (int x = 1; x <= 10; ++x)
            QVERIFY(!mailbox.publish(terrain, QRect(x, 0, 32, 32), 10 + x, 20 + x));
        mailbox.acknowledge(40);
        // A newer frame arrives after GUI receipt but before the renderer starts.
        QVERIFY(mailbox.publish(terrain, QRect(12, 0, 32, 32), 50, 60));
        auto frame = mailbox.forRender(0, 70);
        QVERIFY(frame);
        QCOMPARE(frame->source.x(), 12);
        QCOMPARE(frame->timing.started, int64_t(50));
        QCOMPARE(frame->timing.prepared, int64_t(60));
        QCOMPARE(frame->timing.received, int64_t(70));
        QCOMPARE(frame->terrain.cacheKey(), terrain.cacheKey());
        QVERIFY(!mailbox.forRender(frame->sequence, 80));
        mailbox.acknowledge(90); // delayed notification for the already consumed frame
        QVERIFY(!mailbox.newerThan(frame->sequence));
        auto s = metrics->snapshot(1'000'000'001);
        QCOMPARE(s.pending, 0u);
        QCOMPARE(s.overwrittenPerSecond, 11.0);
        auto rebuilt = mailbox.forRender(0, 100);
        QVERIFY(rebuilt); // replay retained pixels in a new MAP, without a bogus old latency
        QCOMPARE(rebuilt->terrain.cacheKey(), terrain.cacheKey());
        QCOMPARE(rebuilt->timing.started, int64_t(0));
    }
    void queueAndClear() {
        auto metrics = std::make_shared<MapMetrics>(1);
        MapMailbox mailbox(metrics);
        QVERIFY(mailbox.publish(QImage(), QRect(), 100, 120));
        mailbox.acknowledge(150);
        auto frame = mailbox.forRender(0, 200);
        QVERIFY(frame);
        QCOMPARE(frame->timing.received, int64_t(150));
        QVERIFY(mailbox.publish(QImage(), QRect(), 0, 0)); // reconnect clear is also the latest state
        auto cleared = mailbox.forRender(frame->sequence, 250);
        QVERIFY(cleared);
        QVERIFY(cleared->terrain.isNull());
        QVERIFY(cleared->sequence > frame->sequence);
        QCOMPARE(cleared->timing.started, int64_t(0));
    }
    void concurrentProducer() {
        auto metrics = std::make_shared<MapMetrics>();
        MapMailbox mailbox(metrics);
        std::atomic<bool> done = false;
        std::atomic<int> notifications = 0;
        std::thread producer([&] {
            for (int i = 1; i <= 2000; ++i)
                if (mailbox.publish(QImage(), QRect(i, 0, 1, 1), i, i)) ++notifications;
            done = true;
        });
        uint64_t last = 0;
        bool ordered = true;
        do {
            mailbox.acknowledge();
            if (auto frame = mailbox.forRender(last)) {
                ordered = ordered && frame->sequence > last && frame->source.x() == int(frame->sequence);
                last = frame->sequence;
            }
        } while (!done);
        producer.join();
        if (auto frame = mailbox.forRender(last)) last = frame->sequence;
        QVERIFY(ordered);
        QCOMPARE(last, uint64_t(2000));
        QVERIFY(notifications > 0 && notifications <= 2000);
        // Producer cannot leave a stale wake flag after the last GUI acknowledgement.
        mailbox.acknowledge();
        QVERIFY(mailbox.publish(QImage(), QRect(), 0, 0));
    }
};
FUGUE_TEST_GUILESS_MAIN(MapMailboxTest)
#include "mapmailbox_test.moc"

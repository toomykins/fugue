#include <QtTest>
#include "tests/testmain.h"
#include "app/mapframeclock.h"
using namespace std::chrono_literals;
using fugue::MapFrameClock;

class MapFrameClockTest : public QObject {
    Q_OBJECT
private slots:
    void defaultAndFallback() {
        MapFrameClock::Time start{};
        MapFrameClock clock(start);
        QVERIFY(clock.due(start));
        clock.completed(start);
        QVERIFY(!clock.due(start + 99ms));
        QVERIFY(clock.due(start + 100ms));
        clock.setTiming(50'000'000, 0, start);
        clock.completed(start);
        QVERIFY(clock.due(start + 50ms));
    }
    void independentOfUiTick() {
        MapFrameClock::Time start{};
        MapFrameClock clock(start);
        clock.setTiming(100'000'000, 16'666'667, start);
        clock.completed(start);
        QCOMPARE(clock.wait(start, start + 90ms).count(), 17LL);
        QVERIFY(clock.due(start + 17ms));
        clock.completed(start + 17ms);
        QCOMPARE(clock.wait(start + 17ms, start + 90ms).count(), 17LL);
        QVERIFY(!clock.due(start + 33ms));
        QVERIFY(clock.due(start + 34ms));
        // The phase did not drift by rounding every frame to 17 ms.
        clock.completed(start + 34ms);
        QVERIFY(clock.due(start + 50ms + 1ns));
    }
    void timingChangesAndOverrun() {
        MapFrameClock::Time start{};
        MapFrameClock clock(start);
        clock.completed(start);
        clock.setTiming(100'000'000, 10'000'000, start + 5ms);
        QVERIFY(clock.due(start + 5ms)); // faster client.fps applies immediately
        QCOMPARE(clock.completed(start + 48ms), uint64_t(4)); // four missed slots, no catch-up burst
        QVERIFY(!clock.due(start + 48ms));
        QVERIFY(!clock.due(start + 54ms));
        QVERIFY(clock.due(start + 55ms));
        clock.setTiming(100'000'000, 10'000'000, start + 49ms);
        QVERIFY(!clock.due(start + 49ms)); // unchanged timing does not re-arm
        clock.setTiming(100'000'000, 1'000'000'000, start + 49ms);
        clock.completed(start + 49ms);
        QCOMPARE(clock.wait(start + 49ms, start + 90ms).count(), 30LL); // input/audio cap
        QCOMPARE(clock.wait(start + 89ms + 1ns, start + 90ms).count(), 1LL);
        QCOMPARE(clock.wait(start + 90ms, start + 90ms).count(), 0LL);
    }
};
FUGUE_TEST_GUILESS_MAIN(MapFrameClockTest)
#include "mapframeclock_test.moc"

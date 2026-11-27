#include <QtTest>
#include "tests/testmain.h"
#include "shell/mapmetrics.h"
using fugue::shell::MapMetrics;
class MapMetricsTest : public QObject {
    Q_OBJECT
private slots:
    void ratesAndTimings() {
        constexpr int64_t start = 1'000'000'000;
        MapMetrics metrics(start);
        metrics.setPeriod(20'000'000);
        for (int i = 1; i <= 20; ++i) metrics.composed(i, i == 20 ? 2 : 0);
        metrics.uploaded(4'000'000);
        metrics.submitted({start, start + 2'000'000, start + 3'000'000, start + 6'000'000, start + 6'500'000}, start + 8'000'000);
        metrics.submitted({}); // redraw without a newly published frame has no update latency
        auto s = metrics.snapshot(start + 1'000'000'000);
        QCOMPARE(s.targetFps, 50.0);
        QCOMPARE(s.mapsPerSecond, 20.0);
        QCOMPARE(s.submittedPerSecond, 2.0);
        QCOMPARE(s.missedPerSecond, 2.0);
        QCOMPARE(s.uploadMBPerSecond, 4.0);
        QVERIFY(s.compose.available);
        QCOMPARE(s.compose.average, 10.5);
        QCOMPARE(s.compose.p95, 19.0);
        QCOMPARE(s.latency.average, 8.0);
        QCOMPARE(s.latency.p95, 8.0);
        QCOMPARE(s.queue.average, 1.0);
        QCOMPARE(s.renderWait.average, 3.0);
        QCOMPARE(s.submit.average, 2.0);
        QCOMPARE(s.recording.average, 0.5);
        QCOMPARE(s.recording.p95, 0.5);
        QCOMPARE(s.qtSubmitWait.average, 1.5);
        QCOMPARE(s.qtSubmitWait.p95, 1.5);
        QCOMPARE(s.recording.average + s.qtSubmitWait.average, s.submit.average);
        QCOMPARE(metrics.snapshot(start + 1'000'000'001).mapsPerSecond, 20.0); // second overlay shares cached window
        s = metrics.snapshot(start + 2'000'000'000);
        QCOMPARE(s.submittedPerSecond, 0.0);
        QVERIFY(!s.latency.available);
        QVERIFY(!s.compose.available);
        QVERIFY(!s.queue.available);
        QVERIFY(!s.renderWait.available);
        QVERIFY(!s.submit.available);
        QVERIFY(!s.recording.available);
        QVERIFY(!s.qtSubmitWait.available);
    }
    void missingOrInvalidRecordingTimestamp() {
        MapMetrics metrics(1);
        metrics.submitted({10, 20, 30, 40, 0}, 50);
        metrics.submitted({10, 20, 30, 40, 60}, 50);
        auto s = metrics.snapshot(1'000'000'001);
        QVERIFY(s.submit.available);
        QVERIFY(!s.recording.available);
        QVERIFY(!s.qtSubmitWait.available);
    }
    void pendingAndOverwritten() {
        MapMetrics metrics(1);
        metrics.published(false);
        metrics.published(true);
        metrics.published(true);
        auto s = metrics.snapshot(1'000'000'001);
        QCOMPARE(s.pending, 1u);
        QCOMPARE(s.overwrittenPerSecond, 2.0);
        metrics.consumed();
        QCOMPARE(metrics.snapshot(1'000'000'002).pending, 0u);
        metrics.reset(2'000'000'001);
        s = metrics.snapshot(3'000'000'001);
        QCOMPARE(s.pending, 0u);
        QCOMPARE(s.overwrittenPerSecond, 0.0);
    }
    void elapsedTimeAndReset() {
        MapMetrics metrics(1);
        metrics.composed(3, 0);
        metrics.composed(5, 1);
        auto s = metrics.snapshot(2'000'000'001);
        QCOMPARE(s.mapsPerSecond, 1.0);
        QCOMPARE(s.missedPerSecond, 0.5);
        metrics.reset(3'000'000'001);
        s = metrics.snapshot(4'000'000'001);
        QCOMPARE(s.targetFps, 10.0);
        QCOMPARE(s.mapsPerSecond, 0.0);
        QVERIFY(!s.compose.available);
    }
};
FUGUE_TEST_GUILESS_MAIN(MapMetricsTest)
#include "mapmetrics_test.moc"

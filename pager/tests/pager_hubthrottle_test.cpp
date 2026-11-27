// pager/hublink HubThrottle: hub dials are spaced out, and spaced further after each consecutive failure.
#include <QtTest>
#include "tests/testmain.h"
#include "pager/hublink.h"

using namespace fugue::pager;
using ms = std::chrono::milliseconds;

class PagerHubThrottleTest : public QObject {
    Q_OBJECT
private slots:
    void firstDialIsAllowedAndAnImmediateSecondIsNot() {
        HubThrottle t(ms(5000), ms(300000));
        const auto t0 = HubThrottle::Clock::time_point{} + std::chrono::hours(1);
        QVERIFY(t.allow(t0));
        t.record(true);
        int wait = 0;
        QVERIFY(!t.allow(t0 + ms(1000), &wait));
        QVERIFY(wait >= 4000 && wait <= 4002);
        QVERIFY(t.allow(t0 + ms(5000)));
    }
    void gapDoublesPerFailureUpToTheCapAndResetsOnSuccess() {
        HubThrottle t(ms(5000), ms(30000));
        auto now = HubThrottle::Clock::time_point{} + std::chrono::hours(1);
        for (int gap : {10000, 20000, 30000, 30000}) {
            QVERIFY(t.allow(now));
            t.record(false);
            QVERIFY(!t.allow(now + ms(gap - 1)));
            now += ms(gap);
        }
        QVERIFY(t.allow(now));
        t.record(true);
        QVERIFY(t.allow(now + ms(5000)));
    }
};

FUGUE_TEST_APPLESS_MAIN(PagerHubThrottleTest)
#include "pager_hubthrottle_test.moc"

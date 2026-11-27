// animate() chains on objs, mobs and a client.image from retained DreamDaemon 516.1684 wire (tests/data/movanim-world).
// Frames arrive at their preceding "@N" tick; checkpoints below are seconds after the suite starts at tick 11.
// Numeric, colour and width expectations are historical source assertions, not independently retained stock output.
// See the fixture README for their provenance and the six historical rounding corrections.
#include <QtTest>
#include "tests/testmain.h"
#include "model/world.h"
#include "shell/tests/replayfixture.h"
#include "shell/tests/animationfixture.h"

using namespace fugue;

namespace {
// Slot i's colour (mkdmi.py), slot 6 is "white", and the image is "img".
uint32_t slotColour(int i) {
    if (i == 6) return 0xf0f0f0;
    if (i < 0) return 0xfac800;
    return uint32_t(40 + 13 * i) << 16 | uint32_t(250 - 11 * i) << 8 | uint32_t((60 * i) % 200 + 30);
}
// Slot i's tile, as image pixels of its top-left corner (the view is 13 x 13 about (11, 11)).
QPoint slotTile(int i) {
    const int xs[] = {5, 8, 13, 16}, ys[] = {16, 13, 8, 5};
    if (i < 0) i = 12;
    return {(xs[i % 4] - 5) * 32, (17 - ys[i / 4]) * 32};
}
}  // namespace

class MovAnim : public QObject {
    Q_OBJECT
    struct Frame {
        uint16_t typ;
        Bytes body;
        int tick;
    };
    std::vector<Frame> frames_;
    std::unique_ptr<shell::test::ReplayFixture> replay_;
    size_t nextFrame_ = 0;
    double sampledTick_ = -1;
    int64_t originNs_ = 0, tickNs_ = 0;
    int startTick_ = 0;
    QSize viewport_;

    int64_t at(double tick) const { return originNs_ + int64_t(tick * double(tickNs_)); }
    double tickAt(double seconds) const { return startTick_ + seconds * (1'000'000'000.0 / double(tickNs_)); }
    void reset() {
        replay_ = std::make_unique<shell::test::ReplayFixture>();
        nextFrame_ = 0;
        sampledTick_ = -1;
    }
    QImage image(double tick) {
        if (!replay_ || tick < sampledTick_) reset();
        while (nextFrame_ < frames_.size() && frames_[nextFrame_].tick <= int(tick)) {
            const auto& frame = frames_[nextFrame_++];
            replay_->ingest(frame.typ, frame.body, at(frame.tick));
        }
        // Resource arrival times were not retained; supply the static bytes after wire ingest.
        if (!replay_->loadResources("movanim-world")) {
            QTest::qFail(qPrintable(replay_->error), __FILE__, __LINE__);
            return {};
        }
        replay_->refresh(at(tick));
        sampledTick_ = tick;
        const QImage rendered = AnimationFixture::raster(replay_->frame, replay_->sprites);
        if (rendered.size() != viewport_) {
            const auto error = QString("tick %1: expected retained viewport %2x%3, got %4x%5")
                .arg(tick).arg(viewport_.width()).arg(viewport_.height()).arg(rendered.width()).arg(rendered.height());
            QTest::qFail(qPrintable(error), __FILE__, __LINE__);
            return {};
        }
        return rendered;
    }
    static uint32_t rgb(const QImage& img, int x, int y) { return img.pixel(x, y) & 0xffffff; }

    // Slot `i`'s drawn offset from its tile (+x east, +y north) `t` seconds into the suite, from the top-left corner of
    // its colour; with `width`, its drawn width instead of y.
    QPoint offset(int i, double t, bool width = false) {
        QImage img = image(tickAt(t));
        int x0 = INT_MAX, y0 = INT_MAX, x1 = -1;
        for (int y = 0; y < img.height(); y++)
            for (int x = 0; x < img.width(); x++)
                if (rgb(img, x, y) == slotColour(i)) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                }
        if (x1 < 0) return {9999, 9999};
        QPoint tile = slotTile(i);
        return width ? QPoint(x0 - tile.x(), x1 - x0 + 1) : QPoint(x0 - tile.x(), tile.y() - y0);
    }
    // The colour drawn at the centre of slot `i`'s tile.
    uint32_t centre(int i, double t) {
        QImage img = image(tickAt(t));
        QPoint tile = slotTile(i);
        return img.width() ? rgb(img, tile.x() + 16, tile.y() + 16) : 0;
    }

private slots:
    void initTestCase() {
        QFile file(QString(FUGUE_TEST_DATA_DIR) + "/movanim-world/manifest.json");
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        QJsonParseError parseError;
        const auto manifest = QJsonDocument::fromJson(file.readAll(), &parseError).object();
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        originNs_ = manifest["clock_unix_ns"].toInteger();
        tickNs_ = manifest["tick_ns"].toInteger();
        startTick_ = manifest["start_tick"].toInt();
        QVERIFY(originNs_ > 0 && tickNs_ > 0 && startTick_ > 0);
        const auto viewport = manifest["viewport"].toObject();
        viewport_ = QSize(viewport["w"].toInt(), viewport["h"].toInt());
        QVERIFY(viewport_.width() > 0 && viewport_.height() > 0);
        const int expectedFrames = manifest["frames"].toInt();
        const int expectedResources = manifest["resources"].toInt();
        const auto resourceNames = manifest["resource_names"].toObject();
        QVERIFY(expectedFrames > 0 && expectedResources > 0);
        QCOMPARE(resourceNames.size(), expectedResources);

        shell::test::ReplayFixture recording;
        int tick = 0;
        bool ordered = true;
        QVERIFY2(recording.readFrames("frames.txt", "movanim-world", [&](uint16_t type, const Bytes& body) {
            if (type == S2C::Text && body.size() > 1 && body[0] == '@') {
                bool valid = false;
                const int next = QByteArray(reinterpret_cast<const char*>(body.data() + 1), qsizetype(body.size() - 1))
                    .split('\0').front().toInt(&valid);
                ordered = ordered && valid && next >= tick;
                tick = next;
            }
            frames_.push_back({type, body, tick});
        }), qPrintable(recording.error));
        QVERIFY2(ordered, "Recorded tick markers must be valid and ordered");
        QCOMPARE(int(frames_.size()), expectedFrames);
        QVERIFY2(recording.loadResources("movanim-world"), qPrintable(recording.error));
        QCOMPARE(recording.resources.size(), size_t(expectedResources));
        int64_t resourceBytes = 0;
        for (auto entry = resourceNames.begin(); entry != resourceNames.end(); ++entry) {
            bool valid = false;
            const uint32_t id = entry.key().toUInt(&valid, 16);
            QVERIFY(valid && !entry.value().toString().isEmpty());
            QVERIFY2(recording.resources.count(id), qPrintable("Missing resource: " + entry.key()));
            const auto& bytes = recording.resources.at(id);
            QVERIFY(!bytes.isEmpty());
            resourceBytes += bytes.size();
        }
        QCOMPARE(resourceBytes, manifest["resource_bytes"].toInteger());
        // Known-good unanimated control validates the adapter before chains.
        QCOMPARE(offset(0, -.1), QPoint(0, 0));
    }

    void init() { reset(); }

    // animate(pixel_x = 32, 10); animate(pixel_y = 32, 10): one sequence, its steps in turn.
    void sequentialSteps() {
        QCOMPARE(offset(0, 0.5), QPoint(16, 0));
        QCOMPARE(offset(0, 1.0), QPoint(32, 0));
        QCOMPARE(offset(0, 1.5), QPoint(32, 16));
        QCOMPARE(offset(0, 2.0), QPoint(32, 32));
    }

    // A PARALLEL animate 5 ticks into a 20-tick one runs alongside it (flags 0x4, StepDelay 0): y over 0.5 .. 2.5 s while
    // x runs 0 .. 2 s. The same on a client.image (Kind 13, sent as Chain = 0 appending the parallel step).
    void parallelRunsAlongside() {
        QCOMPARE(offset(1, 0.5), QPoint(8, 0));
        QCOMPARE(offset(1, 1.0), QPoint(16, 8));
        QCOMPARE(offset(1, 2.0), QPoint(32, 24));
        QCOMPARE(offset(1, 2.5), QPoint(32, 32));
        QCOMPARE(offset(-1, 0.5), QPoint(16, 0));
        QCOMPARE(offset(-1, 1.0), QPoint(32, 16));
        QCOMPARE(offset(-1, 1.5), QPoint(32, 32));
    }

    // A later animate() (a head, flags 0x40000080) takes over where the chain is: x = 8 at 0.5 s eases back to 0 by 1.5 s
    // instead of the first tween running on to 32. Two in the same tick: the head starts from the first one's From, so
    // the obj moves diagonally from its tile.
    void headTakesOver() {
        QCOMPARE(offset(2, 0.5), QPoint(8, 0));
        QCOMPARE(offset(2, 1.0), QPoint(4, 0));
        QCOMPARE(offset(2, 1.5), QPoint(0, 0));
        QCOMPARE(offset(2, 2.0), QPoint(0, 0));
        QCOMPARE(offset(13, 0.5), QPoint(16, 16));
        QCOMPARE(offset(13, 1.0), QPoint(32, 32));
    }

    // delay = 5: the From holds until 0.5 s; the second step's delay is not sent and it follows on.
    void delayHoldsTheFrom() {
        QCOMPARE(offset(3, 0.4), QPoint(0, 0));
        QCOMPARE(offset(3, 1.0), QPoint(16, 0));
        QCOMPARE(offset(3, 1.5), QPoint(32, 0));
        QCOMPARE(offset(3, 2.0), QPoint(32, 16));
    }

    // loop = 3 on a 2-step ping-pong plays it three times; ANIMATION_END_NOW is sent as a fresh chain from the end state.
    void loopAndEndNow() {
        QCOMPARE(offset(4, 0.5), QPoint(16, 0));
        QCOMPARE(offset(4, 1.0), QPoint(0, 0));
        QCOMPARE(offset(4, 2.5), QPoint(16, 0));
        QCOMPARE(offset(4, 3.0), QPoint(0, 0));
        QCOMPARE(offset(4, 3.5), QPoint(0, 0));
        QCOMPARE(offset(5, 0.5), QPoint(32, 0));
        QCOMPARE(offset(5, 1.0), QPoint(32, 16));
        QCOMPARE(offset(5, 1.5), QPoint(32, 32));
    }

    // loop = -1 runs until animate(obj) at 2.5 s, whose Count = 0 frame stops it on the obj's own pixel_y (0). A head
    // 7 ticks into another looping obj holds the loop there and eases from it to its own To.
    void foreverLoopStops() {
        QCOMPARE(offset(11, 0.5), QPoint(0, 16));
        QCOMPARE(offset(11, 1.5), QPoint(0, 16));
        QCOMPARE(offset(11, 2.4), QPoint(0, 12));
        QCOMPARE(offset(11, 2.5), QPoint(0, 0));
        QCOMPARE(offset(14, 0.7), QPoint(10, 0));
        QCOMPARE(offset(14, 1.0), QPoint(4, 9));
        QCOMPARE(offset(14, 1.2), QPoint(0, 16));
        QCOMPARE(offset(14, 2.0), QPoint(0, 16));
    }

    // Mobs (Kind 3): vg's attack shape (8 out and back, two 2-tick steps), and animate() re-called every 2 ticks, each
    // call a head easing from where the one before had got to.
    void mobChains() {
        QCOMPARE(offset(9, 0.2), QPoint(8, 0));
        QCOMPARE(offset(9, 0.3), QPoint(4, 0));
        QCOMPARE(offset(9, 0.4), QPoint(0, 0));
        QCOMPARE(offset(8, 0.2), QPoint(0, 5));
        QCOMPARE(offset(8, 0.4), QPoint(0, 2));
        QCOMPARE(offset(8, 0.5), QPoint(0, 4));
        QCOMPARE(offset(8, 0.6), QPoint(0, 6));
        QCOMPARE(offset(8, 0.8), QPoint(0, 2));
        QCOMPARE(offset(8, 0.9), QPoint(0, 0));
    }

    // vg's directional_recoil on an obj: a 1-tick PARALLEL | RELATIVE kick of 8 every 2 ticks, each followed after 2 ticks
    // by a 3-tick return. The kicks stack on the returns until the last return ends at 1.1 s.
    void stackedKicks() {
        QCOMPARE(offset(10, 0.1), QPoint(8, 0));
        QCOMPARE(offset(10, 0.3), QPoint(14, 0));
        QCOMPARE(offset(10, 0.38), QPoint(12, 0));
        QCOMPARE(offset(10, 0.7), QPoint(14, 0));
        QCOMPARE(offset(10, 0.9), QPoint(6, 0));
        QCOMPARE(offset(10, 1.1), QPoint(0, 0));
    }

    // Colour and alpha: color to blue over 10 ticks, alpha to 0 over 10 ticks PARALLEL from 0.5 s: half-blue, half-gone at
    // 1 s, only the floor at 1.5 s. Transform: scale 2 over 10 ticks, pixel_x 32 PARALLEL from 0.5 s.
    void colourAlphaTransform() {
        uint32_t c = centre(6, 1.0);
        QVERIFY2(std::abs(int(c >> 16) - 48) <= 3 && std::abs(int(c & 0xff) - 168) <= 6,
                 qPrintable(QString::number(c, 16)));
        QCOMPARE(centre(6, 1.5), 0x606060u);
        QPoint w = offset(7, 0.5, true);
        QVERIFY2(std::abs(w.x() + w.y() / 2 - 16) <= 1 && std::abs(w.y() - 48) <= 3,
                 qPrintable(QString("%1,w%2").arg(w.x()).arg(w.y())));
        w = offset(7, 1.0, true);
        QVERIFY2(std::abs(w.x() + w.y() / 2 - 32) <= 1 && std::abs(w.y() - 64) <= 3,
                 qPrintable(QString("%1,w%2").arg(w.x()).arg(w.y())));
    }
};

FUGUE_TEST_MAIN(MovAnim)
#include "movanim_test.moc"

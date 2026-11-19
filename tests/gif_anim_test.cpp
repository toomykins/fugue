// Animated GIF icons (render/gif) animate exactly as stock BYOND 516 does. tests/data/gifanim/<n>.dd.dmi is the stock
// conversion of <n>.gif (conv.dme.src), and each decode must match it cell for cell: size, frames, delays, loop, composited pixels.
// The fixtures cover per-frame delays, loop counts, sub-rectangles, transparent indices and disposal methods.
#include <QFile>
#include <QtTest>

#include "render/dmi.h"

using namespace fugue::render;

namespace {

std::vector<uint8_t> load(const QString& name) {
    QFile f(QString(FUGUE_TEST_DATA_DIR) + "/gifanim/" + name);
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(f.fileName()));
    QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

ParseResult parse(const QString& name) {
    std::vector<uint8_t> d = load(name);
    return parseIcon(d.data(), d.size());
}

}  // namespace

class GifAnim : public QObject {
    Q_OBJECT
private slots:
    void equalsDreamDaemonsConversion_data() {
        QTest::addColumn<QString>("name");
        for (const char* n : {"a", "c", "e", "l2", "d", "d2", "d3", "d4"}) QTest::newRow(n) << QString(n);
    }
    void equalsDreamDaemonsConversion() {
        QFETCH(QString, name);
        ParseResult g = parse(name + ".gif"), dd = parse(name + ".dd.dmi");
        QVERIFY2(g.ok(), qPrintable(QString::fromStdString(g.errClass + ": " + g.errDetail)));
        QVERIFY2(dd.ok(), qPrintable(QString::fromStdString(dd.errClass + ": " + dd.errDetail)));
        QCOMPARE(g.icon->width, dd.icon->width);
        QCOMPARE(g.icon->height, dd.icon->height);
        QCOMPARE(int(g.icon->states.size()), 1);
        QCOMPARE(int(dd.icon->states.size()), 1);
        const IconState &gs = g.icon->states[0], &ds = dd.icon->states[0];
        QCOMPARE(QString::fromStdString(gs.name), QString::fromStdString(ds.name));
        QCOMPARE(gs.dirs, ds.dirs);
        QCOMPARE(gs.frames, ds.frames);
        QCOMPARE(int(gs.delays.size()), int(ds.delays.size()));
        for (size_t i = 0; i < gs.delays.size(); i++) QCOMPARE(gs.delays[i], ds.delays[i]);
        QCOMPARE(gs.loop, ds.loop);
        for (int64_t f = 0; f < ds.frames; f++) {
            std::vector<uint8_t> a, b;
            QVERIFY(g.icon->frame(gs, 0, f, a));
            QVERIFY(dd.icon->frame(ds, 0, f, b));
            QVERIFY2(a == b, qPrintable(QString("%1 frame %2 differs from DreamDaemon's").arg(name).arg(f)));
        }
    }

    // The frame shown at time t (deciseconds on the icon's clock).
    void frameAtTimeIsWhatDreamSeekerShowed() {
        ParseResult a = parse("a.gif");
        QVERIFY(a.ok());
        const IconState& st = a.icon->states[0];
        // Stock playback: R ~0.5 s, G ~1.0 s, B ~0.1 s (a 0-delay frame is one tick), Y ~1.5 s, then R again (cycle 3.1 s)
        const std::pair<double, int> rows[] = {{0, 0},
                                               {4.9, 0},
                                               {5, 1},
                                               {14.9, 1},
                                               {15, 2},
                                               {15.9, 2},
                                               {16, 3},
                                               {30.9, 3},
                                               {31, 0},
                                               {36, 1},
                                               {31 * 1000 + 15.5, 2}};
        for (auto [t, f] : rows)
            QVERIFY2(frameAtTime(st, t) == f,
                     qPrintable(QString("a.gif at %1 ds: frame %2, want %3").arg(t).arg(frameAtTime(st, t)).arg(f)));
        // pixels of each frame: solid R, G, B, Y
        const uint8_t want[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
        for (int f = 0; f < 4; f++) {
            std::vector<uint8_t> px;
            QVERIFY(a.icon->frame(st, 0, f, px));
            QVERIFY(std::memcmp(&px[(16 * 32 + 16) * 4], want[f], 4) == 0);
        }
        // c.gif has no loop extension: it plays once and holds its last frame (stock DreamSeeker shows Y from then on)
        ParseResult c = parse("c.gif");
        QVERIFY(c.ok());
        QCOMPARE(c.icon->states[0].loop, int64_t(1));
        QCOMPARE(frameAtTime(c.icon->states[0], 15), 1);
        QCOMPARE(frameAtTime(c.icon->states[0], 40), 3);
        QCOMPARE(frameAtTime(c.icon->states[0], 1e9), 3);
    }

    // A single-frame GIF is one cell with no delays.
    void singleFrameIsStill() {
        // 1x1 GIF89a, global table of 2, one frame
        const uint8_t one[] = {'G', 'I',  'F', '8', '9', 'a', 1, 0, 1, 0, 0x80, 0, 0, 255,  0,    0, 0,   0,
                               0,   0x2c, 0,   0,   0,   0,   1, 0, 1, 0, 0,    2, 2, 0x44, 0x01, 0, 0x3b};
        ParseResult r = parseIcon(one, sizeof one);
        QVERIFY2(r.ok(), qPrintable(QString::fromStdString(r.errClass + ": " + r.errDetail)));
        QCOMPARE(r.icon->states[0].frames, int64_t(1));
        QVERIFY(!r.icon->states[0].hasDelays);
    }
};

QTEST_APPLESS_MAIN(GifAnim)
#include "gif_anim_test.moc"

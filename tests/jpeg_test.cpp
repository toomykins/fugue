// JPEG icons (render/jpeg.h) are one-frame opaque icons. The expected pixels are stock DreamDaemon 516's decode
// (tests/data/jpeg/conv.dme.src). The Blend forces the decode: a bare fcopy copies the JPEG bytes unchanged.
// Baseline and progressive JPEGs, including one under a .gif name, match libjpeg exactly (max channel difference 0).
#include <QFile>
#include <QtTest>

#include "render/dmi.h"

using namespace fugue::render;

namespace {

std::vector<uint8_t> load(const QString& name) {
    QFile f(QString(FUGUE_TEST_DATA_DIR) + "/jpeg/" + name);
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(f.fileName()));
    QByteArray b = f.readAll();
    return std::vector<uint8_t>(b.begin(), b.end());
}

ParseResult parse(const QString& name) {
    std::vector<uint8_t> d = load(name);
    return parseIcon(d.data(), d.size());
}

}  // namespace

class Jpeg : public QObject {
    Q_OBJECT
private slots:
    void equalsDreamDaemonsDecode_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("baseline") << QString("base");
        QTest::newRow("progressive") << QString("prog");
    }
    void equalsDreamDaemonsDecode() {
        QFETCH(QString, name);
        ParseResult j = parse(name + ".jpg"), dd = parse(name + ".dd.dmi");
        QVERIFY2(j.ok(), qPrintable(QString::fromStdString(j.errClass + ": " + j.errDetail)));
        QVERIFY2(dd.ok(), qPrintable(QString::fromStdString(dd.errClass + ": " + dd.errDetail)));
        QCOMPARE(j.icon->width, dd.icon->width);
        QCOMPARE(j.icon->height, dd.icon->height);
        QCOMPARE(int(j.icon->states.size()), 1);
        QCOMPARE(j.icon->states[0].frames, int64_t(1));
        std::vector<uint8_t> a, b;
        QVERIFY(j.icon->frame(j.icon->states[0], 0, 0, a));
        QVERIFY(dd.icon->frame(dd.icon->states[0], 0, 0, b));
        QCOMPARE(a.size(), b.size());
        int maxDiff = 0;
        for (size_t i = 0; i < a.size(); i++) maxDiff = std::max(maxDiff, std::abs(int(a[i]) - int(b[i])));
        QCOMPARE(maxDiff, 0);  // exact: DreamDaemon decodes as libjpeg's default does
        for (size_t i = 3; i < a.size(); i += 4) QCOMPARE(int(a[i]), 255);  // opaque
    }
    // Grayscale expands to RGB. Stock DreamDaemon's icon() conversion is 1-bit (0 -> black, anything else -> white); the client path is not pinned here.
    void grayscaleExpandsToRgb() {
        ParseResult j = parse("gray.jpg");
        QVERIFY2(j.ok(), qPrintable(QString::fromStdString(j.errClass + ": " + j.errDetail)));
        QCOMPARE(j.icon->width, 16);
        QCOMPARE(j.icon->height, 16);
        std::vector<uint8_t> a;
        QVERIFY(j.icon->frame(j.icon->states[0], 0, 0, a));
        for (size_t i = 0; i < a.size(); i += 4) {
            QCOMPARE(a[i], a[i + 1]);
            QCOMPARE(a[i], a[i + 2]);
        }
        QVERIFY(a[4] > 0 && a[4] < 255);  // a real gray level, not thresholded
    }
    void truncatedFileIsRefusedNotCrashed() {
        std::vector<uint8_t> d = load("base.jpg");
        d.resize(d.size() / 3);
        ParseResult r = parseIcon(d.data(), d.size());
        // libjpeg pads a truncated stream with gray (a warning, not an error), so a decoded icon or a clean refusal both pass.
        // A crash or a hang does not.
        QVERIFY(r.ok() || r.errClass == "jpeg_decode");
        std::vector<uint8_t> junk = {0xff, 0xd8, 0xff, 0x00, 0x01, 0x02};
        ParseResult r2 = parseIcon(junk.data(), junk.size());
        QVERIFY(!r2.ok());
        QCOMPARE(QString::fromStdString(r2.errClass), QString("jpeg_decode"));
    }
};

QTEST_APPLESS_MAIN(Jpeg)
#include "jpeg_test.moc"

#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QBuffer>
#include <QPainter>
#include "shell/turfmap.h"
using namespace fugue;

static QByteArray png(const QString& metadata = {}) {
    QImage sheet(64, 64, QImage::Format_RGBA8888);
    sheet.fill(Qt::red);
    QPainter p(&sheet);
    p.fillRect(32, 0, 32, 32, Qt::green);
    p.fillRect(0, 32, 32, 32, Qt::blue);
    p.fillRect(32, 32, 32, 32, Qt::yellow);
    p.end();
    if (!metadata.isEmpty()) sheet.setText("Description", metadata);
    QByteArray bytes;
    QBuffer out(&bytes);
    out.open(QIODevice::WriteOnly);
    sheet.save(&out, "PNG");
    return bytes;
}
static QString metadata() {
    return "# BEGIN DMI\nversion = 4.0\nwidth = 32\nheight = 32\nstate = \"floor\"\n dirs = 4\n frames = 1\n# END DMI\n";
}
class TurfMapTest : public QObject {
    Q_OBJECT
private slots:
    void directions() {
        auto icon = shell::DmiIcon::decode(png(metadata()));
        QCOMPARE(icon.frame("floor", 2).pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(icon.frame("floor", 1).pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(icon.frame("floor", 4).pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(icon.frame("floor", 8).pixelColor(0, 0), QColor(Qt::yellow));
        QVERIFY(icon.frame("missing", 2).isNull());
        QVERIFY(shell::DmiIcon::decode("not a png").sheet.isNull());
    }
    void missingStateFallsBackToDefault() {
        auto defaultOnly = metadata(); defaultOnly.replace("\"floor\"", "\"\"");
        const auto icon = shell::DmiIcon::decode(png(defaultOnly));
        for (int dir : {2,1,4,8}) {
            QVERIFY(!icon.frame("old-state-name",dir).isNull());
            QCOMPARE(icon.frame("old-state-name",dir),icon.frame("",dir));
        }
        // Named states still take precedence; the default need not be first.
        const auto mixed = shell::DmiIcon::decode(png("width = 32\nheight = 32\nstate = \"named\"\ndirs = 1\nframes = 1\nstate = \"\"\ndirs = 1\nframes = 1\n"));
        QCOMPARE(mixed.frame("named",2).pixelColor(0,0),QColor(Qt::red));
        QCOMPARE(mixed.frame("unknown",2).pixelColor(0,0),QColor(Qt::green));
        QVERIFY(shell::DmiIcon::decode(png(metadata())).frame("unknown",2).isNull());
        const auto plain = shell::DmiIcon::decode(png());
        QCOMPARE(plain.frame("unknown",2),plain.frame("",2));
    }
    void stateOffsets() {
        auto icon = shell::DmiIcon::decode(png("width = 32\nheight = 32\nstate = \"a\"\ndirs = 1\nframes = 2\nstate = \"b\"\ndirs = 1\nframes = 2\n"));
        QCOMPARE(icon.frame("b", 2).pixelColor(0, 0), QColor(Qt::blue));
    }
    void gliding_data() {
        QTest::addColumn<int>("dx");
        QTest::addColumn<int>("dy");
        QTest::newRow("east") << 1 << 0;
        QTest::newRow("west") << -1 << 0;
        QTest::newRow("north") << 0 << 1;
        QTest::newRow("south") << 0 << -1;
        QTest::newRow("northeast") << 1 << 1;
        QTest::newRow("northwest") << -1 << 1;
        QTest::newRow("southeast") << 1 << -1;
        QTest::newRow("southwest") << -1 << -1;
        QTest::newRow("carried-east") << 2 << 0;
        QTest::newRow("carried-north") << 0 << 2;
    }
    void gliding() {
        QFETCH(int, dx);
        QFETCH(int, dy);
        model::World w;
        w.bounds = {true, 10, 10, 0, 4, 4, 2, 2};
        w.resync.haveOrigin = true;
        w.haveEye = true;
        w.eyeX = w.eyeY = 6; // destination view's zero-based lower-left tile is (4,4)
        w.resources.push_back({123, 0});
        QImage whole(320, 320, QImage::Format_RGBA8888);
        QPainter painter(&whole);
        for (int y = 0; y < 10; ++y) for (int x = 0; x < 10; ++x) {
            QColor color(20 + x * 20, 20 + y * 20, 200);
            painter.fillRect(x * 32, (9 - y) * 32, 32, 32, color);
            uint32_t id = 1 + x + y * 10;
            auto& appearance = w.appearances[id];
            appearance.fields.iconResID = 0;
            setFields(appearance, QString("{\"hasColor\":true,\"colorR\":%1,\"colorG\":%2,\"colorB\":200,\"colorA\":255}")
                .arg(color.red()).arg(color.green()).toStdString());
            w.tiles[{uint16_t(x), uint16_t(y), 0}] = id;
        }
        painter.end();
        QImage white(32, 32, QImage::Format_RGBA8888);
        white.fill(Qt::white);
        QByteArray bytes;
        QBuffer out(&bytes);
        out.open(QIODevice::WriteOnly);
        QVERIFY(white.save(&out, "PNG"));
        shell::TurfMap map;
        auto read = [&](uint32_t, QByteArray& result) { result = bytes; return true; };
        constexpr int64_t start = 1'000'000'000;
        w.eyeGliding = true;
        w.eyeGlide = model::glideMotion(-dx, -dy, 32, 32, 100'000'000, true, start, false);
        const int64_t duration = w.eyeGlide.durNs;
        auto first = map.prepare(w, read, start);
        auto middle = map.prepare(w, read, start + duration / 2);
        QCOMPARE(first.image.cacheKey(), middle.image.cacheKey()); // glide reuses the terrain texture
        QVERIFY(first.source != middle.source);
        // Compare entire frames, including edge tiles outside the destination view.
        QCOMPARE(map.compose(w, read, start), whole.copy(128 - dx * 32, 128 + dy * 32, 64, 64));
        QCOMPARE(map.compose(w, read, start + duration / 2), whole.copy(128 - dx * 16, 128 + dy * 16, 64, 64));
        QCOMPARE(map.compose(w, read, start + duration), whole.copy(128, 128, 64, 64));
        QCOMPARE(map.compose(w, read, start + duration * 2), whole.copy(128, 128, 64, 64));
        QCOMPARE(map.compose(w, read, 0), whole.copy(128, 128, 64, 64));
        w.eyeGliding = false; // teleports/snaps never retain a camera translation
        QCOMPARE(map.compose(w, read, start), whole.copy(128, 128, 64, 64));
    }
    void cacheInvalidation() {
        model::World w;
        w.bounds = {true, 40, 40, 0, 2, 2, 2, 2};
        w.resync.haveOrigin = true;
        w.haveEye = true;
        w.eyeX = w.eyeY = 12;
        w.resources.push_back({123, 0});
        model::Appearance ap;
        ap.fields.iconResID = 0; ap.fields.iconState = "floor"; setFields(ap, "{\"dir\":2}");
        w.appearances[1] = ap;
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) w.tiles[{uint16_t(x), uint16_t(y), 0}] = 1;
        shell::TurfMap map;
        auto read = [&](uint32_t, QByteArray& bytes) { bytes = png(metadata()); return true; };
        auto first = map.prepare(w, read);
        auto idle = map.prepare(w, read);
        QCOMPARE(first.image.cacheKey(), idle.image.cacheKey());
        QCOMPARE(first.source, idle.source);
        w.eyeX++; // movement inside padding is uniform-only too
        auto step = map.prepare(w, read);
        QCOMPARE(first.image.cacheKey(), step.image.cacheKey());
        QVERIFY(first.source != step.source);
        w.atoms[42].appid = 99;
        setFields(w.appearances[99], "{}");
        QCOMPARE(map.prepare(w, read).image.cacheKey(), first.image.cacheKey());
        setFields(w.appearances[1], "{\"dir\":1}"); // same id, new appearance
        auto changed = map.prepare(w, read);
        QVERIFY(changed.image.cacheKey() != first.image.cacheKey());
        QCOMPARE(changed.image.pixelColor(changed.source.topLeft()), QColor(Qt::green));
        w.eyeX += 12; // outside cached terrain: recenter
        auto far = map.prepare(w, read);
        QVERIFY(far.image.rect().contains(far.source));
        QCOMPARE(far.image.pixelColor(far.source.topLeft()), QColor(Qt::green));
        w.tiles[{uint16_t(w.eyeX - 2), uint16_t(w.eyeY - 1), 0}] = 2; // missing appearance
        auto missing = map.prepare(w, read);
        QCOMPARE(missing.image.pixelColor(missing.source.topLeft()), QColor(Qt::black));
        w.appearances[2] = ap;
        auto arrived = map.prepare(w, read);
        QCOMPARE(arrived.image.pixelColor(arrived.source.topLeft()), QColor(Qt::red));
        w.viewZ = 1;
        auto otherLevel = map.prepare(w, read);
        QCOMPARE(otherLevel.image.pixelColor(otherLevel.source.topLeft()), QColor(Qt::black));
        w.bounds.viewW = 3;
        QCOMPARE(map.prepare(w, read).source.width(), 96);
        w.bounds.viewW = 0;
        QVERIFY(map.prepare(w, read).image.isNull());
    }
    void turfsOnlyAndResourceRetry() {
        model::World w;
        w.bounds = {true, 20, 20, 0, 2, 2, 2, 2};
        w.resync.haveOrigin = true;
        w.resync.originX = 4;
        w.resync.originY = 4;
        w.viewZ = 3;
        w.resources.push_back({123, 0});
        model::Appearance south;
        south.fields.iconResID = 0;
        south.fields.iconState = "floor";
        setFields(south, "{\"dir\":2}");
        w.appearances[1] = south;
        auto north = south;
        setFields(north, "{\"dir\":1}");
        w.appearances[2] = north;
        w.tiles[{4, 4, 3}] = 1;
        w.tiles[{4, 5, 3}] = 2;
        w.tiles[{5, 5, 2}] = 1; // another level must not leak in
        w.atoms[42].appid = 2;  // an entity must not be drawn
        shell::TurfMap map;
        int requests = 0;
        uint32_t requestedId = 0;
        auto missing = map.compose(w, [&](uint32_t id, QByteArray&) { requestedId = id; ++requests; return false; });
        QCOMPARE(requestedId, uint32_t(123));
        QCOMPARE(requests, 1);
        QCOMPARE(missing.pixelColor(0, 63), QColor(Qt::black));
        auto result = map.compose(w, [&](uint32_t, QByteArray& bytes) { bytes = png(metadata()); return true; });
        QCOMPARE(result.size(), QSize(64, 64));
        QCOMPARE(result.pixelColor(0, 0), QColor(Qt::green)); // north is at the top
        QCOMPARE(result.pixelColor(0, 63), QColor(Qt::red));
        QCOMPARE(result.pixelColor(63, 0), QColor(Qt::black));
        w.haveEye = true;
        w.eyeX = 6;
        w.eyeY = 6;
        QCOMPARE(map.compose(w, {}).pixelColor(0, 0), QColor(Qt::green));
        // A turf's own overlay inherits its icon, but is not an entity on that tile.
        setFields(w.appearances[2], "{\"dir\":1,\"overlays\":[3]}");
        model::Appearance overlay;
        overlay.fields.iconState = "floor";
        setFields(overlay, "{\"dir\":8,\"appearanceFlags\":131588,\"overlays\":[2]}"); // fixed WEST, cycle terminates
        w.appearances[3] = overlay;
        QCOMPARE(map.compose(w, {}).pixelColor(0, 0), QColor(Qt::yellow));
        w.appearances[3].fields.iconResID = 0xffff; // captured no-icon wire sentinel also inherits
        QCOMPARE(map.compose(w, {}).pixelColor(0, 0), QColor(Qt::yellow));
        setFields(w.appearances[3], R"({"dir":2,"overlays":[2]})"); // SOUTH without fixedDir inherits NORTH
        QCOMPARE(map.compose(w, {}).pixelColor(0, 0), QColor(Qt::green));
        w.viewZ = 2;
        QCOMPARE(map.compose(w, {}).pixelColor(0, 0), QColor(Qt::black));
        w.bounds.viewW = w.bounds.maxX = 65535; // the view is no wider than the world
        QVERIFY(map.compose(w, {}).isNull());
    }
};
FUGUE_TEST_GUILESS_MAIN(TurfMapTest)
#include "turfmap_test.moc"

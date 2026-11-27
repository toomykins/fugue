// The map's right-click menu against what stock DreamSeeker 516.1689 showed (tests/data/menu-world: stock.tsv and the
// pictures in icons/): the session's recorded wire is replayed, each case right-clicked where the probe clicked stock,
// and the entries, their verbs' labels and the picture beside each atom's name compared with stock's.
#include <QtTest>
#include "tests/testmain.h"

#include <QFile>
#include <QPainter>
#include <map>

#include "model/verbmenu.h"
#include "shell/mapmenu.h"
#include "shell/tests/replayfixture.h"
using namespace fugue;
using namespace fugue::shell;

namespace {
const QString kWorld = "menu-world";
// Where menuprobe.py right-clicked each case: tile (1-based, y up) and the pixel inside it from its top left.
struct Click { int tx, ty, dx = 16, dy = 16; };
const std::map<QString, Click> kCases = {
    {"plain", {1, 11}}, {"dir-east", {3, 11}}, {"anim", {5, 11}}, {"overlay", {7, 11}}, {"underlay", {9, 11}},
    {"colored", {11, 11}}, {"alpha", {13, 11}}, {"scaled", {15, 11}}, {"turned", {17, 11}}, {"big", {1, 9}},
    {"big-top", {2, 10}}, {"small", {4, 9, 8, 24}}, {"iconless", {6, 9}}, {"nostate", {8, 9}}, {"offset", {10, 9}},
    {"offset-up", {11, 10, 4, 28}}, {"outlined", {12, 9}}, {"holder", {14, 9}}, {"imaged", {16, 9}},
    {"changer", {18, 9}}, {"improper", {1, 7}}, {"proper", {3, 7}}, {"amp", {5, 7}}, {"html", {7, 7}},
    {"unicode", {9, 7}}, {"long", {11, 7}}, {"twins", {13, 7}}, {"suffix", {15, 7}}, {"empty", {17, 7}},
    {"verbs", {2, 4}}, {"stack", {5, 4}}, {"mob", {8, 4}}, {"argonly", {11, 4}}, {"hud", {18, 1}},
    {"anim-b", {5, 11}}, {"anim-c", {5, 11}},
};
// Pictures that depend on when the menu opened: the frame of an animation, a state the world flips every second.
// They are checked by the rule instead (iconFollowsTheScene).
const QSet<QString> kTimed = {"anim", "anim-b", "anim-c", "changer"};
struct Row { QString atom; QStringList verbs; QString icon; };
std::map<QString, std::vector<Row>> stockRows() {
    QFile f(QString(FUGUE_TEST_DATA_DIR) + '/' + kWorld + "/stock.tsv");
    if (!f.open(QIODevice::ReadOnly)) qFatal("cannot open %s", qPrintable(f.fileName()));
    std::map<QString, std::vector<Row>> out;
    for (const QByteArray& raw : f.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).remove('\r');
        if (line.trimmed().isEmpty() || line.startsWith('#')) continue;
        const QStringList c = line.split('\t');
        if (c.size() != 5) qFatal("stock.tsv: %s", qPrintable(line));
        auto& rows = out[c[0]];
        if (c[1] != "-") rows.push_back({c[2], c[3].split('|'), c[4]});
    }
    return out;
}
// Fugue's picture as the screen shows it: over the menu's background.
QImage onMenu(const QImage& icon) {
    QImage out(16, 16, QImage::Format_RGB32);
    out.fill(QColor(240, 240, 240));
    QPainter painter(&out);
    painter.drawImage(0, 0, icon);
    return out;
}
// The largest difference of a channel between two pictures of the same size.
int difference(const QImage& a, const QImage& b) {
    int worst = 0;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                              std::abs(qBlue(p) - qBlue(q))});
        }
    return worst;
}
struct Fixture : test::ReplayFixture {
    std::optional<MapPointer> at(const Click& c) const {
        const QPointF pixel((c.tx - 1) * 32 + c.dx + .5, (12 - c.ty) * 32 + c.dy + .5);
        auto p = mapPointerAt(pixel, frame.source.size(), frame.source.size() / frame.geometry.tile, frame.source,
                              frame.geometry, "map", true, sprites);
        if (p) p->kind = MapPointer::Down;
        return p;
    }
    std::vector<MapMenuEntry> listed(const Click& c) const {
        std::vector<MapMenuEntry> out;
        const auto p = at(c);
        if (!p) return out;
        for (auto& e : mapMenuStack(*p, session.world(), frame.geometry.session))
            if (!e.name.empty() && !e.verbs.empty()) out.push_back(std::move(e));
        return out;
    }
};
}  // namespace

class MapMenuTest : public QObject {
    Q_OBJECT
    Fixture f;
private slots:
    void initTestCase() {
        QVERIFY2(f.load("frames.txt", kWorld), qPrintable(f.error));
        QCOMPARE(f.frame.source.size(), QSize(576, 384));
    }
    void stockMenus_data() {
        QTest::addColumn<QString>("name");
        for (const auto& [name, rows] : stockRows()) QTest::newRow(qPrintable(name)) << name;
    }
    // Entry by entry: the atom's name, the labels of its verbs in order, and its picture.
    void stockMenus() {
        QFETCH(QString, name);
        const auto rows = stockRows().at(name);
        const auto listed = f.listed(kCases.at(name));
        QStringList want, got;
        for (const auto& row : rows) want << row.atom;
        for (const auto& e : listed) got << QString::fromStdString(e.name);
        QCOMPARE(got, want);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& entry = listed[i];
            QStringList labels;
            for (const auto& verb : entry.verbs) labels << QString::fromStdString(model::menuVerbLabel(verb.verb));
            QCOMPARE(labels, rows[i].verbs);
            if (kTimed.contains(name)) continue;
            const QImage icon = mapMenuIcon(entry.rendered);
            if (rows[i].icon == "-") {
                QVERIFY2(icon.isNull(), qPrintable(name));
                continue;
            }
            QVERIFY2(!icon.isNull(), qPrintable(name));
            QCOMPARE(icon.size(), QSize(16, 16));
            const QImage stock(QString(FUGUE_TEST_DATA_DIR) + '/' + kWorld + '/' + rows[i].icon);
            QCOMPARE(stock.size(), QSize(16, 16));
            QVERIFY2(difference(onMenu(icon), stock.convertToFormat(QImage::Format_RGB32)) <= 1,
                     qPrintable(name + " entry " + QString::number(i)));
        }
    }
    // An animated icon's picture is the frame the map drew when the menu opened (stock: anim, anim-b and anim-c showed
    // the frame of that moment, each a different one, and kept it while the menu stayed open).
    void iconFollowsTheScene() {
        Fixture g;  // every frame arrives at 0 ns: the animation starts there
        QVERIFY2(g.readFrames("frames.txt", kWorld, [&](uint16_t type, const Bytes& body) { g.ingest(type, body, 0); }),
                 qPrintable(g.error));
        QVERIFY2(g.loadResources(kWorld), qPrintable(g.error));
        QSet<QRgb> seen;
        for (int64_t ns : {int64_t(0), int64_t(1'000'000'000), int64_t(2'000'000'000)}) {
            g.refresh(ns);
            const auto listed = g.listed(kCases.at("anim"));
            QCOMPARE(int(listed.size()), 1);
            const QImage icon = mapMenuIcon(listed[0].rendered);
            QVERIFY(!icon.isNull());
            const auto drawn = std::find_if(g.sprites.begin(), g.sprites.end(), [&](const MapSprite& s) {
                return s.kind == MapSprite::Entity && mapSpriteRef(s).ref == listed[0].rendered.click.over.ref;
            });
            QVERIFY(drawn != g.sprites.end());
            QCOMPARE(icon.pixel(8, 8), drawn->image.convertToFormat(QImage::Format_ARGB32_Premultiplied).pixel(16, 16));
            // The menu keeps its picture: a later scene does not change the one taken from this click's.
            g.refresh(ns + 1'000'000'000);
            QCOMPARE(mapMenuIcon(listed[0].rendered), icon);
            seen.insert(icon.pixel(8, 8));
        }
        QCOMPARE(seen.size(), 3);
    }
    // The scaling: each pixel the average of what it covers, the picture centred and its aspect kept.
    void fitAveragesAndCentres() {
        QImage tall(32, 64, QImage::Format_ARGB32_Premultiplied);
        tall.fill(Qt::transparent);
        for (int y = 0; y < 64; ++y) tall.setPixel(y % 2, y, qRgba(255, 255, 255, 255));
        const QImage fit = mapMenuFit(tall, 16);
        QCOMPARE(fit.size(), QSize(16, 16));
        QCOMPARE(qAlpha(fit.pixel(3, 5)), 0);       // left of the 8 pixels wide picture
        QCOMPARE(qAlpha(fit.pixel(4, 5)), 64);      // 4 of the 16 pixels it covers
        QCOMPARE(qAlpha(fit.pixel(5, 5)), 0);
        QCOMPARE(qAlpha(fit.pixel(12, 5)), 0);
        QVERIFY(mapMenuFit({}, 16).isNull());
    }
    // A picture keeps its own pixels on a scaled display: the icon is the style's 16 logical pixels with it unscaled in
    // the middle.
    void iconKeepsDevicePixels() {
        QImage red(16, 16, QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        const QIcon icon = mapMenuEntryIcon(red, 1.5);
        const QPixmap pixmap = icon.pixmap(QSize(16, 16), 1.5);
        QCOMPARE(pixmap.size(), QSize(24, 24));
        const QImage image = pixmap.toImage();
        QCOMPARE(qAlpha(image.pixel(3, 12)), 0);
        QCOMPARE(image.pixel(4, 12), qRgb(255, 0, 0));
        QCOMPARE(image.pixel(19, 12), qRgb(255, 0, 0));
        QCOMPARE(qAlpha(image.pixel(20, 12)), 0);
        QVERIFY(mapMenuEntryIcon({}, 1.5).isNull());
    }
};

FUGUE_TEST_MAIN(MapMenuTest)
#include "mapmenu_test.moc"

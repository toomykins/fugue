// Independent stock parity and exact renderer-equivalence checks use GPU readback.
#include <QtTest>
#include "tests/testmain.h"
#include "shell/compositionplan.h"
#include "shell/mappresentation.h"
#include "shell/tests/compositionfixture.h"
#include "shell/tests/gpucomparison.h"
#include "shell/tests/replayfixture.h"
#include <QDir>
#include <QFile>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>

using namespace fugue::shell;
using test::GpuComparison;
using test::contributorScene;

// The master's matrix for an fx-grid case of tests/data/master-transform-world, as world.dm's shape() builds it.
static std::optional<std::array<double,6>> gridMatrix(const QString& shape) {
    using M = std::array<double,6>;
    const auto turned = [](M m, double degrees) {   // BYOND's M.Turn(): m first, then clockwise by degrees
        const double c = std::cos(degrees*std::numbers::pi/180), s = std::sin(degrees*std::numbers::pi/180);
        return M{c*m[0]+s*m[3], c*m[1]+s*m[4], c*m[2]+s*m[5], -s*m[0]+c*m[3], -s*m[1]+c*m[4], -s*m[2]+c*m[5]};
    };
    static const std::map<QString,M> shapes = {
        {"plain",{1,0,0,0,1,0}}, {"s75",{.75,0,0,0,.75,0}}, {"s50",{.5,0,0,0,.5,0}}, {"s25",{.25,0,0,0,.25,0}},
        {"s60",{.6,0,0,0,.6,0}}, {"s40",{.4,0,0,0,.4,0}}, {"sx50",{.5,0,0,0,1,0}}, {"sy50",{1,0,0,0,.5,0}},
        {"sx50y75",{.5,0,0,0,.75,0}}, {"s50m",{.5,0,16,0,.5,8}}, {"s50m3",{.5,0,3,0,.5,-5}},
        {"s150",{1.5,0,0,0,1.5,0}}, {"s200",{2,0,0,0,2,0}}, {"s300",{3,0,0,0,3,0}}, {"sx200y300",{2,0,0,0,3,0}},
        {"s200m3",{2,0,3,0,2,-5}}, {"s200t90",turned({2,0,0,0,2,0},90)}, {"s200t30",turned({2,0,0,0,2,0},30)},
        {"s50t90",turned({.5,0,0,0,.5,0},90)}, {"s50t30",turned({.5,0,0,0,.5,0},30)},
        {"s200e",{2,0,-112,0,2,112}}, {"s300t1",turned({3,0,0,0,3,0},1)}, {"s400t4",turned({4,0,0,0,4,0},4)}};
    const auto found = shapes.find(shape);
    if (found == shapes.end()) return std::nullopt;
    return found->second;
}

class CompositionGpuTest : public QObject {
    Q_OBJECT
    QStringList stockSets_;
private slots:
    void contributorMaterialGpu() {
        GpuComparison gpu;const QRect source(11,-7,193,137);
        QVERIFY2(gpu.initialize(source.size()), "Required GPU backend could not be initialized");
        int comparisons=0;
        for (int variant=0;variant<9;++variant) {
            auto scene=contributorScene(source,73,variant);
            QImage original;
            for (int phase=0;phase<4;++phase) {
                // Replay, shrink/move to force pool reuse, then restore old content.
                auto current=scene;
                if (phase==2) {current.remove(9,47);for (auto& s:current) if(s.kind!=MapSprite::Master) s.position+=QPoint(5,-3);}
                const auto a=gpu.render(gpu.reference,current,source);QVERIFY(!a.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                const auto b=gpu.render(gpu.bounded,current,source);QVERIFY(!b.isNull());
                if (phase>0) QCOMPARE(gpu.uploaded,uint64_t(0));
                QVERIFY(!gpu.reference.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->backend.boundedMaterials);
                QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
                if (phase==0) original=a;else if(phase!=2) QCOMPARE(a,original);
                QVERIFY2(a==b,qPrintable(QString("exact RGBA comparison variant=%1 phase=%2").arg(variant).arg(phase)));
                ++comparisons;
            }
        }
        for (int blend=0;blend<5;++blend) {
            const auto grouped=contributorScene(source,70+blend,7);
            const auto a=gpu.render(gpu.reference,grouped,source);
            const auto b=gpu.render(gpu.bounded,grouped,source);
            QVERIFY(!a.isNull() && !b.isNull());
            QVERIFY(gpu.bounded.preparedPlan()->storage.workPixels < gpu.reference.preparedPlan()->storage.workPixels);
            QCOMPARE(a,b);++comparisons;
        }
        GpuComparison large;const QRect largeSource(-23,17,832,704);
        QVERIFY2(large.initialize(largeSource.size()), "Required GPU backend could not be initialized");
        const auto dense=contributorScene(largeSource,1024);
        const auto a=large.render(large.reference,dense,largeSource);
        const auto b=large.render(large.bounded,dense,largeSource);
        QVERIFY(!a.isNull() && !b.isNull());
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        QCOMPARE(a,b);++comparisons;
        // Device resource recreation must not reset the candidate selection.
        QVERIFY(large.bounded.initialize(large.rhi.get(),large.descriptor.get(),1));
        QCOMPARE(large.render(large.bounded,dense,largeSource),a);++comparisons;
        QVERIFY(large.bounded.preparedPlan()->backend.boundedMaterials);
        QVERIFY(large.bounded.preparedPlan()->storage.workPixels < large.reference.preparedPlan()->storage.workPixels);
        qInfo()<<comparisons<<"exact full RGBA GPU comparisons; not presentation/timing evidence";
    }
    // Sets of tests/data/stock-parity-world, replayed without a server: replay/<set>.txt is what DreamDaemon 516.1689
    // sent this client for the set (the app's recorder, FUGUE_RECORD_DIR), replay/res its resources, and
    // replay/<set>.png the map stock DreamSeeker 516.1689 drew for the same world, grabbed six seconds after login
    // (tools/stockprobe/stockprobe.py record writes all three). No channel of any pixel may be more than 8 levels from
    // stock's.
    // Drawn on the entry's backend (Mesa's llvmpipe passes on OpenGL: on Linux each entry runs on its own Xvfb
    // display with QT_QPA_PLATFORM=xcb, tools/xvfb-test.sh; the offscreen platform has no OpenGL). A missing device
    // fails this GPU check.
private:
    std::unique_ptr<GpuComparison> replayGpu_;   // one device for every set
    std::unique_ptr<GpuComparison> transformGpu_;   // and one for every master transform grab
private slots:
    void cleanupTestCase() { replayGpu_.reset(); transformGpu_.reset(); }
    void initTestCase() {
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/stock-parity-world/replay");
        QFile recorded(directory.filePath("recorded.tsv"));
        QVERIFY2(recorded.open(QIODevice::ReadOnly),qPrintable(recorded.fileName()));
        QStringList sets;
        for (const auto& raw:recorded.readAll().split('\n')) {
            const auto line=QString::fromUtf8(raw).trimmed();
            if (line.isEmpty() || line.startsWith('#')) continue;
            const auto set=line.section('\t',0,0);
            QVERIFY2(!set.isEmpty() && !sets.contains(set),qPrintable("Invalid or duplicate set: "+set));
            sets.push_back(set);
        }
        QVERIFY2(!sets.empty(),"recorded.tsv contains no stock replay sets");
        sets.sort();
        // The recording inventory, wire and images must agree, so a removed PNG
        // cannot silently remove its regression from this data-driven test.
        for (const auto& suffix:{QString(".png"),QString(".txt")}) {
            QStringList expected;
            for (const auto& set:sets) expected.push_back(set+suffix);
            expected.sort();
            QCOMPARE(directory.entryList({"*"+suffix},QDir::Files),expected);
        }
        QVERIFY2(!QDir(directory.filePath("res")).entryList(QDir::Files).empty(),"Missing stock replay resources");
        stockSets_=sets;
    }
    void stockParityReplay_data() {
        QTest::addColumn<QString>("set");
        for (const auto& set:stockSets_) QTest::newRow(qPrintable(set)) << set;
    }
    void stockParityReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/stock-parity-world/replay/";
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        test::ReplayFixture fixture;
        fixture.session.setIngestClockNs(login);
        QVERIFY2(fixture.load(set + ".txt", "stock-parity-world/replay", {}, grabbed), qPrintable(fixture.error));
        const auto& frame = fixture.frame;
        const auto& sprites = fixture.sprites;
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(frame.source.size(), stock.size());
        auto& gpu = replayGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
            if (!gpu->initialize(stock.size())) {
                gpu.reset();
                QFAIL("Required GPU backend could not be initialized");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, sprites, frame.source, Qt::black);
        QCOMPARE(drawn.size(), stock.size());
        int off = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            if (std::abs(qRed(a) - qRed(b)) > 8 || std::abs(qGreen(a) - qGreen(b)) > 8 || std::abs(qBlue(a) - qBlue(b)) > 8)
                if (!off++) first = {x, y};
        }
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-parity-" + set + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock, the first at %2,%3; drawn: %4")
                             .arg(off).arg(first.x()).arg(first.y()).arg(saved)));
    }
    // tests/data/master-transform-world/replay: a plane master whose filter, matrix or both are animating, replayed
    // through the real scene assembly from what DreamDaemon 516.1689 sent (stockprobe.py record, as for
    // stock-parity-world) and drawn six seconds after login, inside the ten-minute hold that follows each half-second
    // first step (world.dm, animate_master()). Stock draws the plane with the filters at their animated values and
    // turned by the animated matrix. The filter-list cases (README) change the list between steps or after animate(),
    // on the master and on an atom; the group cases put KEEP_TOGETHER on the master, settled and animating, with and
    // without an overlay. No channel of any pixel may be more than 8 levels from stock's grab.
    void animatedMasterReplay_data() {
        QTest::addColumn<QString>("set");
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/master-transform-world/replay");
        QFile recorded(directory.filePath("recorded.tsv"));
        QVERIFY2(recorded.open(QIODevice::ReadOnly),qPrintable(recorded.fileName()));
        QStringList sets;
        for (const auto& raw:recorded.readAll().split('\n')) {
            const auto line=QString::fromUtf8(raw).trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) sets.push_back(line.section('\t',0,0));
        }
        sets.sort();
        QCOMPARE(sets.size(),20);
        for (const auto& suffix:{QString(".png"),QString(".txt")}) {
            QStringList expected;
            for (const auto& set:sets) expected.push_back(set+suffix);
            QCOMPARE(directory.entryList({"*"+suffix},QDir::Files,QDir::Name),expected);
        }
        for (const auto& set:sets) QTest::newRow(qPrintable(set)) << set;
    }
    void animatedMasterReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/master-transform-world/replay/";
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        test::ReplayFixture fixture;
        fixture.session.setIngestClockNs(login);
        QVERIFY2(fixture.load(set + ".txt", "master-transform-world/replay", {}, grabbed), qPrintable(fixture.error));
        const auto master = std::find_if(fixture.sprites.begin(), fixture.sprites.end(),
                                         [](const auto& s) { return s.kind == MapSprite::Master; });
        // The cases that clear the list, or whose master has neither a filter nor (tree-group) anything stock draws,
        // are checked by their pixels alone.
        if (!set.contains("clear") && !set.contains("group") && !set.contains("pline"))
            QVERIFY2(master != fixture.sprites.end() && master->planeEffects && !master->planeEffects->filters.empty(),
                     "the animating master and its filter reach the scene");
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(fixture.frame.source.size(), stock.size());
        auto& gpu = transformGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
            if (!gpu->initialize(stock.size())) {
                gpu.reset();
                QFAIL("Required GPU backend could not be initialized");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, fixture.sprites, fixture.frame.source, Qt::black);
        QCOMPARE(drawn.size(), stock.size());
        int off = 0, worst = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            worst = std::max(worst, d);
            if (d > 8 && !off++) first = {x, y};
        }
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-animated-master-" + set + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock (worst %2), the first at %3,%4; drawn: %5")
                             .arg(off).arg(worst).arg(first.x()).arg(first.y()).arg(saved)));
    }
    // tests/data/plane-relay-world/replay: a plane drawn through a relay (render_source of the master's render target)
    // that stands elsewhere (an edge, a percent, a range), carries pixel offsets, a matrix or filters of its own (a
    // colour matrix included), relays a master with a matrix, has an overlay of its own, is an overlay of a screen
    // object, or is relayed again by a second relay ("chain"); the "grid" sets relay a view-sized icon whose pixels
    // name themselves through a matrix, with
    // PIXEL_SCALE on the relay for "rnear" (which texel each pixel takes), replayed through the real
    // scene assembly from what DreamDaemon 516.1689 sent (stockprobe.py record) and compared with stock's grab of the
    // same case: no channel of any pixel more than 8 levels from stock's.
    void planeRelayReplay_data() {
        QTest::addColumn<QString>("set");
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/plane-relay-world/replay");
        QFile recorded(directory.filePath("recorded.tsv"));
        QVERIFY2(recorded.open(QIODevice::ReadOnly),qPrintable(recorded.fileName()));
        QStringList sets;
        for (const auto& raw:recorded.readAll().split('\n')) {
            const auto line=QString::fromUtf8(raw).trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) sets.push_back(line.section('\t',0,0));
        }
        sets.sort();
        QCOMPARE(sets.size(),59);
        for (const auto& suffix:{QString(".png"),QString(".txt")}) {
            QStringList expected;
            for (const auto& set:sets) expected.push_back(set+suffix);
            expected.sort();
            auto found=directory.entryList({"*"+suffix},QDir::Files);
            found.sort();
            QCOMPARE(found,expected);
        }
        for (const auto& set:sets) QTest::newRow(qPrintable(set)) << set;
    }
    void planeRelayReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/plane-relay-world/replay/";
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        test::ReplayFixture fixture;
        fixture.session.setIngestClockNs(login);
        QVERIFY2(fixture.load(set + ".txt", "plane-relay-world/replay", {}, grabbed), qPrintable(fixture.error));
        if (set != "norelay")
            QVERIFY2(std::any_of(fixture.sprites.begin(), fixture.sprites.end(),
                                 [](const auto& s) { return s.kind == MapSprite::Screen && !s.renderSource.isEmpty(); }),
                     "the relay reaches the scene");
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(fixture.frame.source.size(), stock.size());
        auto& gpu = transformGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
            if (!gpu->initialize(stock.size())) {
                gpu.reset();
                QFAIL("Required GPU backend could not be initialized");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, fixture.sprites, fixture.frame.source, Qt::black);
        QCOMPARE(drawn.size(), stock.size());
        int off = 0, worst = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            worst = std::max(worst, d);
            if (d > 8 && !off++) first = {x, y};
        }
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-plane-relay-" + set + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock (worst %2), the first at %3,%4; drawn: %5")
                             .arg(off).arg(worst).arg(first.x()).arg(first.y()).arg(saved)));
    }
    // tests/data/master-screen-loc-world/replay: a screen object outside a 7x7 view (8,1, 0,4, 9,1 or 1,9) enlarges the
    // map on a level whose turfs and objects lie past the view, in a control of 224 pixels or the size the set's name
    // ends with. Replayed through the real scene assembly (with the margins the scene asks for) from what DreamDaemon
    // 516.1689 sent, drawn with mapWorldClip and mapWorldView and placed in the control as the presentation places it,
    // then compared with stock's grab of the whole control: no channel of any pixel more than 8 levels from stock's.
    // The atoms coloured by a colour matrix are drawn stretched from the view onto the clip, as stock draws them; the
    // "wide-fx" sets add a channel swap, other kinds of drawing that stay on their tiles, and sprites turned by 45
    // degrees that the clip cuts (README).
    void masterLocWideReplay_data() {
        QTest::addColumn<QString>("set");
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/master-screen-loc-world/replay");
        QFile recorded(directory.filePath("recorded.tsv"));
        QVERIFY2(recorded.open(QIODevice::ReadOnly),qPrintable(recorded.fileName()));
        QStringList sets;
        for (const auto& raw:recorded.readAll().split('\n')) {
            const auto line=QString::fromUtf8(raw).trimmed();
            if (!line.isEmpty() && !line.startsWith('#')) sets.push_back(line.section('\t',0,0));
        }
        QCOMPARE(sets.size(),9);
        for (const auto& set:sets) QTest::newRow(qPrintable(set)) << set;
    }
    void masterLocWideReplay() {
        QFETCH(QString, set);
        const QString dir = QString(FUGUE_TEST_DATA_DIR) + "/master-screen-loc-world/replay/";
        const int64_t login = 1'000'000'000'000'000'000, grabbed = login + 6'000'000'000;
        test::ReplayFixture fixture;
        fixture.session.setIngestClockNs(login);
        QVERIFY2(fixture.load(set + ".txt", "master-screen-loc-world/replay", {}, grabbed), qPrintable(fixture.error));
        // The frame after the one that learns of the screen object holds the larger map.
        const auto reader = [&](uint32_t id, QByteArray& out) { return fixture.read(id, out); };
        const QMargins hud = fixture.entities.hudMargins();
        QVERIFY2(!hud.isNull(), "the screen object enlarges the map");
        fixture.frame = fixture.terrain.prepare(fixture.session.world(), reader, grabbed, false, hud);
        fixture.sprites = fixture.entities.prepare(fixture.session.world(), fixture.frame, reader, grabbed, nullptr, true);
        const QRect source = fixture.frame.source;
        const int tile = fixture.frame.geometry.tile;
        QCOMPARE(source.size(), QSize(224, 224) + QSize(tile*(hud.left()+hud.right()), tile*(hud.top()+hud.bottom())));
        const auto size = set.section('_', 3, 3);
        const int side = size.isEmpty() ? 224 : size.section('x', 0, 0).toInt();
        const QSize control(side, side);
        MapPresentationSettings normal; normal.iconSize = tile;   // the skin's icon-size = 32
        const auto placement = mapPresentation(control, source.size(), tile, normal);
        QCOMPARE(placement.scale, 1.0);
        const auto clip = mapWorldClip(placement, control, source.size(), hud, tile);
        QVERIFY(clip);
        GpuComparison gpu;
        QVERIFY2(gpu.initialize(source.size()), "Required GPU backend could not be initialized");
        const QImage scene = gpu.render(gpu.bounded, fixture.sprites, source, Qt::black, clip,
                                        mapWorldView(source.size(), hud, tile));
        QVERIFY(!scene.isNull());
        QImage drawn(control, QImage::Format_RGBA8888); drawn.fill(Qt::black);
        const QPoint at = placement.destination.topLeft().toPoint();
        for (int y = 0; y < scene.height(); ++y) for (int x = 0; x < scene.width(); ++x)
            if (QRect({}, control).contains(at + QPoint(x, y))) drawn.setPixel(at + QPoint(x, y), scene.pixel(x, y));
        QImage stock(dir + set + ".png"); stock = stock.convertToFormat(QImage::Format_RGBA8888);
        QVERIFY(stock.width() >= side && stock.height() >= side);
        stock = stock.copy(QRect({}, control));
        // The atoms coloured by a colour matrix, which stock draws stretched from the view onto the clip, are compared
        // too: two in every set, and in the "wide-fx" sets a channel swap among the other kinds of drawing.
        QSet<QPoint> coloured;
        for (const auto& s : fixture.sprites)
            if (s.color != identityColor && s.kind != MapSprite::Screen && s.kind != MapSprite::Master) coloured.insert(s.cell);
        QCOMPARE(coloured.size(), set.contains("wide-fx") ? 3 : 2);
        int off = 0, worst = 0, compared = 0; QPoint first;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            ++compared;
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            worst = std::max(worst, d);
            if (d > 8 && !off++) first = {x, y};
        }
        QCOMPARE(compared, side * side);
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-wide-map-" + QString(set).replace(',', '-') + ".png");
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock (worst %2), the first at %3,%4; drawn: %5")
                             .arg(off).arg(worst).arg(first.x()).arg(first.y()).arg(saved)));
    }
    // tests/data/master-transform-world/filters: the map stock DreamSeeker 516.1689 drew for a plane master with a
    // matrix, a filter of each kind or both over a red 16 px box at (5,4) and, for "edge", a blue tile at (7,4);
    // "pxs" adds PIXEL_SCALE to the master. The "grid" cases put a 224 px icon whose pixels name themselves under a
    // master that shrinks, turns, moves or enlarges its plane: which plane pixel each map pixel shows; with a render
    // target ("rtv", "rth"), a tint ("dim", "tint"), a filter, or the matrix on a KEEP_TOGETHER group ("kt") instead.
    // Rebuilt here from sprites; no channel of any pixel may be more than 8 levels from stock's.
    void masterTransformFilters_data() {
        QTest::addColumn<QString>("name");
        const QDir directory(QString(FUGUE_TEST_DATA_DIR)+"/master-transform-world/filters");
        const auto files=directory.entryList({"fx-*.png"},QDir::Files,QDir::Name);
        QVERIFY2(files.size()==78,qPrintable(directory.path()+": expected 78 stock grabs"));
        for (const auto& file:files) QTest::newRow(qPrintable(file.chopped(4))) << file.chopped(4);
    }
    void masterTransformFilters() {
        QFETCH(QString, name);
        QImage stock(QString(FUGUE_TEST_DATA_DIR)+"/master-transform-world/filters/"+name+".png");
        stock = stock.convertToFormat(QImage::Format_RGBA8888);
        const QRect source(0,0,224,224);
        QCOMPARE(stock.size(),source.size());
        MapSprites scene;
        auto effects=std::make_shared<PlaneEffects>();
        if (name.contains("grid")) {
            // The fx-grid cases: one 224 px icon over the whole view, red its column, green its row and blue a
            // one-pixel checker; the case's last part names the master's matrix (world.dm shape()).
            auto grid=test::ink(1); grid.image=QImage(224,224,QImage::Format_RGBA8888);
            for (int y=0;y<224;++y) for (int x=0;x<224;++x) grid.image.setPixelColor(x,y,QColor(x,y,(x+y)%2 ? 0 : 255));
            grid.iconBottom={0,224}; grid.position={0,0};
            const auto matrix=gridMatrix(name.section('-',-1));
            QVERIFY2(matrix,qPrintable("unknown shape in "+name));
            if (name.contains("-kt-")) {
                // A KEEP_TOGETHER group (the grid with its own icon as an overlay) takes the matrix, about the icon's
                // centre, and the master none; "pxs" is PIXEL_SCALE on the group.
                const auto& m=*matrix;
                auto group=std::make_shared<AppearanceGroup>();
                group->transform=QTransform(m[0],-m[3],-m[1],m[4],m[2],-m[5]);
                group->matrix=m; group->nearest=name.contains("pxs");
                grid.appearanceGroup=group; grid.groupOccurrence=1; grid.groupOrigin={112,112};
                scene.push_back(grid);
                grid.groupLayer=1;
            } else effects->matrix=*matrix;
            scene.push_back(grid);
        } else {
            auto box=test::ink(1); box.image.fill(Qt::transparent);
            for (int y=8;y<24;++y) for (int x=8;x<24;++x) box.image.setPixelColor(x,y,QColor(200,60,60));
            box.position={128,96}; scene.push_back(box);
        }
        if (name.contains("edge")) {
            auto edge=test::ink(1); edge.image.fill(QColor(40,80,230)); edge.atom=44; edge.position={192,96};
            scene.push_back(edge);
        }
        if (name.contains("scalex2")) effects->matrix=std::array<double,6>{2,0,0,0,1,0};
        if (name.contains("turn90")) effects->matrix=std::array<double,6>{0,1,0,-1,0,0};
        if (name.contains("half")) effects->matrix=std::array<double,6>{.5,0,0,0,.5,0};
        PlaneFilter f;
        if (name.contains("outline")) { f.kind=PlaneFilter::Outline; f.size=name.contains("outline4") ? 4 : 1; f.color={0,1,0,1}; }
        if (name.contains("blur2")) f.size=2;
        if (name.contains("shadow6")) { f.kind=PlaneFilter::Shadow; f.x=6; f.color={1,1,1,1}; }
        // As the decoder reads the records of world.dm's filters (y is screen-down).
        if (name.contains("wave")) { f.kind=PlaneFilter::Wave; f.x=16; f.size=3; f.flags=1; } // WAVE_SIDEWAYS
        if (name.contains("ripple")) { f.kind=PlaneFilter::Ripple; f.size=6; f.repeat=16; f.radius=24; f.falloff=1; }
        if (name.contains("bloom")) { f.kind=PlaneFilter::Bloom; f.size=3; f.offset=2; f.color={100.f/255,0,0,1}; }
        if (name.contains("swap")) { f.kind=PlaneFilter::Color; f.material={0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0}; }
        if (name.contains("mask")) {
            QImage mask(32,32,QImage::Format_RGBA8888); mask.fill(Qt::transparent);
            for (int y=0;y<32;++y) for (int x=0;x<16;++x) mask.setPixelColor(x,y,Qt::white);
            f.kind=PlaneFilter::AlphaMask; f.icon=1; f.image=std::make_shared<const QImage>(mask); f.x=32;
        }
        if (name.contains("cover")) {   // an alpha mask that hides nothing: a 224 px opaque icon
            QImage cover(224,224,QImage::Format_RGBA8888); cover.fill(Qt::white);
            f.kind=PlaneFilter::AlphaMask; f.icon=2; f.image=std::make_shared<const QImage>(cover);
        }
        const bool gridFilter=name.contains("outline") || name.contains("blur2") || name.contains("swap") || name.contains("cover");
        if ((!name.contains("grid") || gridFilter) && !name.endsWith("plain") && !name.endsWith("scalex2") && !name.endsWith("turn90") &&
            !name.endsWith("half-edge"))
            effects->filters={f};
        QVERIFY(effects->valid(source.size()));
        // "rtv": the master has the visible render target p1; "rth": the hidden *p1, drawn by a relay at 1,1.
        auto master=test::master(1,name.contains("-rtv-") ? "p1" : name.contains("-rth-") ? "*p1" : QString());
        master.planeEffects=effects;
        master.nearest=name.contains("pxs") && !name.contains("-kt-"); // PIXEL_SCALE on the master
        if (name.contains("dim")) master.tint[3]=192.f/255;   // the master's alpha 192
        if (name.contains("tint")) master.tint={1,1,128.f/255,1};   // its colour rgb(255,255,128)
        scene.push_back(master);
        if (name.contains("-rth-")) { auto relay=test::relay(5,"*p1"); relay.iconBottom={0,224}; scene.push_back(relay); }
        auto& gpu = transformGpu_;
        if (!gpu) {
            gpu = std::make_unique<GpuComparison>();
            if (!gpu->initialize(source.size())) {
                gpu.reset();
                QFAIL("Required GPU backend could not be initialized");
            }
        }
        const QImage drawn = gpu->render(gpu->bounded, scene, source, QColor(16,16,16));
        QCOMPARE(drawn.size(), stock.size());
        // Where a PIXEL_SCALE plane turned a quarter turn puts a pixel's centre exactly on a texel boundary, stock takes
        // the texel before it six times in seven and the one after it otherwise, by its own interpolation's rounding
        // (fx-grid-pxs-s50t90, 1792 of its pixels): there, either texel of the two passes.
        std::optional<QTransform> ties;
        // A KEEP_TOGETHER group's surface halved does the same in a triangle of every seventh column
        // (fx-grid-kt-pxs-s50, 904 pixels).
        if (name.contains("grid") && name.contains("pxs") && (name.endsWith("t90") || name.contains("-kt-"))) {
            const auto m=*gridMatrix(name.section('-',-1));   // BYOND's y is up: screen = pivot + A (texel - pivot) + t
            ties=(QTransform::fromTranslate(-112,-112)*QTransform(m[0],-m[3],-m[1],m[4],m[2],-m[5])*
                  QTransform::fromTranslate(112,112)).inverted();
        }
        const auto tied=[&](int x, int y, QRgb a, QRgb b) {
            if (!ties || qAlpha(a)!=255 || qAlpha(b)!=255 || qRgb(qRed(a),qGreen(a),qBlue(a))==qRgb(16,16,16) ||
                qRgb(qRed(b),qGreen(b),qBlue(b))==qRgb(16,16,16)) return false;
            const QPointF at=ties->map(QPointF(x+.5,y+.5));
            const auto axis=[](double u, int p, int q) {
                const double k=std::round(u);
                return p==q || (std::abs(u-k)<1e-6 && std::min(p,q)==int(k)-1 && std::max(p,q)==int(k));
            };
            return axis(at.x(),qRed(a),qRed(b)) && axis(at.y(),qGreen(a),qGreen(b)) &&
                   qBlue(a)==((qRed(a)+qGreen(a))%2 ? 0 : 255);
        };
        int off = 0, worst = 0, either = 0; QPoint first;
        for (int y = 0; y < stock.height(); ++y) for (int x = 0; x < stock.width(); ++x) {
            const QRgb a = drawn.pixel(x, y), b = stock.pixel(x, y);
            const int d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            if (d > 8 && tied(x, y, a, b)) { ++either; continue; }
            worst = std::max(worst, d);
            if (d > 8 && !off++) first = {x, y};
        }
        if (either) qInfo() << either << "pixels on a texel boundary took the other texel of the two";
        // An enlarged bilinear plane is read as though it ended at the part the view shows, rounded out to whole plane
        // pixels, so the view's outermost row and column are not mixed with what lies past it (fx-grid-s200: 892
        // pixels otherwise; -s200t90, -sx200y300), but not with a filter (fx-grid-outline1-s200).
        if (!off) return;
        const QString saved = QDir::temp().filePath("fugue-transform-" + name + ".png");   // to look at beside stock's
        drawn.save(saved);
        QFAIL(qPrintable(QString("%1 pixels differ from stock (worst %2), the first at %3,%4; drawn: %5")
                             .arg(off).arg(worst).arg(first.x()).arg(first.y()).arg(saved)));
    }
};
FUGUE_TEST_MAIN(CompositionGpuTest)
#include "compositiongpu_test.moc"

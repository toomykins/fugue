#include <QtTest>
#include <QSignalSpy>
#include <QPointer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QLabel>
#include <QScreen>
#include "app/gpumapwindow.h"
#include "shell/browserpane.h"
#include "shell/skinwindow.h"
#include "shell/displaysettings.h"

using namespace fugue::render;
using namespace fugue::shell;
namespace {
DrawList picture() {
    auto s = std::make_shared<Sprite>();
    s->w = 4;
    s->h = 2;
    s->px = {255, 0,   0,  255, 0,  255, 0,   255, 0,   0,   255, 255, 190, 70,  40,  0,
             80,  150, 30, 255, 90, 20,  240, 255, 255, 255, 0,   255, 0,   220, 190, 255};
    DrawLayer l;
    l.sprite = s;
    DrawList dl;
    dl.outW = 4;
    dl.outH = 2;
    dl.planes.push_back({0, {l}, {}});
    dl.hit = std::make_shared<HitScene>();
    return dl;
}
QImage expected(const DrawList& dl) {
    auto image = execute(dl);
    return QImage(image.px.data(), image.w, image.h, image.w * 4, QImage::Format_RGBA8888).copy();
}
struct Stop {
    RenderThread& thread;
    ~Stop() {
        thread.requestStop();
        thread.wait();
    }
};
struct Keys : QObject {
    int presses = 0, releases = 0;
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) ++presses;
        if (event->type() == QEvent::KeyRelease) ++releases;
        return false;
    }
};
struct RestoreEnv {
    const char* name;
    QByteArray value;
    bool set;
    explicit RestoreEnv(const char* n) : name(n), value(qgetenv(n)), set(qEnvironmentVariableIsSet(n)) {}
    ~RestoreEnv() {
        if (set)
            qputenv(name, value);
        else
            qunsetenv(name);
    }
};
const char* skin = R"(window "main"
    elem "main"
        type = MAIN
        size = 240x180
        is-default = true
    elem "map"
        type = MAP
        pos = 0,0
        size = 240x180
        is-default = true
)";
}  // namespace
class GpuPresentTest : public QObject {
    Q_OBJECT
    void samePixels(QImage a, QImage b, int tolerance = 0) {
        QCOMPARE(a.size(), b.size());
        QVERIFY(!a.isNull());
        a = a.convertToFormat(QImage::Format_RGBA8888);
        b = b.convertToFormat(QImage::Format_RGBA8888);
        static int comparison = 0;
        ++comparison;
        int worst = 0;
        QPoint at;
        for (int y = 0; y < a.height(); ++y)
            for (int i = 0; i < a.width() * 4; ++i) {
                int error = std::abs(int(a.constScanLine(y)[i]) - int(b.constScanLine(y)[i]));
                if (error > worst) {
                    worst = error;
                    at = QPoint(i / 4, y);
                }
            }
        QVERIFY2(worst <= tolerance, qPrintable(QString("comparison %1: max channel error %2 at (%3,%4)")
                                                    .arg(comparison)
                                                    .arg(worst)
                                                    .arg(at.x())
                                                    .arg(at.y())));
    }
private slots:
    void initTestCase() {
        qRegisterMetaType<GpuDrawList>();
        qRegisterMetaType<HitPtr>();
        qRegisterMetaType<MapPointer>();
    }
    void nativePresentationSnapshotsInputAndResize() {
        RestoreEnv trace("FUGUE_GPU_TRACE");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/frames.csv";
        qputenv("FUGUE_GPU_TRACE", path.toUtf8());
        MapWidget map;
        map.resize(96, 72);
        map.setMapScale(0, 0, true, "distort");
        auto* gpu = new GpuMapWindow(&map);
        auto* container = QWidget::createWindowContainer(gpu, &map);
        map.setExternalView(container);
        connect(gpu, &GpuMapWindow::submitted, &map, [&](GpuDrawList dl) {
            QPointer<GpuMapWindow> view = gpu;
            map.setExternalFrame(QSize(dl->outW, dl->outH), dl->hit,
                                 [view] { return view ? view->snapshot() : QImage(); });
        });
        QSignalSpy submitted(gpu, &GpuMapWindow::submitted), rejected(gpu, &GpuMapWindow::rejected);
        auto dl = std::make_shared<const DrawList>(picture());
        QVERIFY(gpu->setList(dl));
        container->show();
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(map.windowHandle()));
        map.activateWindow();  // focus needs an active window, which X without a window manager never activates itself
        QVERIFY(QTest::qWaitForWindowActive(map.windowHandle()));
        QTRY_COMPARE_WITH_TIMEOUT(submitted.size(), 1, 15000);
        QCOMPARE(rejected.size(), 0);
        QFile csv(path + ".present.csv");
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const auto rows = csv.readAll().trimmed().split('\n');
        QCOMPARE(rows.size(), 2);
        const auto names = rows[0].split(','), values = rows[1].split(',');
        QCOMPARE(values.size(), names.size());
        QCOMPARE(values[names.indexOf("width")].toInt(), 4);
        QCOMPARE(values[names.indexOf("height")].toInt(), 2);
        QVERIFY(values[names.indexOf("total_cpu_ms")].toDouble() > 0);
        QVERIFY(values[names.indexOf("encode_ms")].toDouble() > 0);
        QVERIFY(names.contains("uniform_bytes"));
        QVERIFY(values[names.indexOf("uniform_bytes")].toULongLong() > 0);
        QCOMPARE(map.hitScene(), dl->hit);
        samePixels(map.frame(), expected(*dl));
        samePixels(gpu->snapshotView(),
                   map.viewSnapshot());  // actual swapchain pixels: orientation, letterbox, nearest sampling
        map.setPerformanceText("GPU test\nno readback in ordinary frames");
        samePixels(gpu->snapshotView(), map.viewSnapshot(), 2);
        samePixels(map.frame(), expected(*dl));  // metrics excluded from native-size map screenshots
        QCOMPARE(submitted.size(), 1);           // captures/repaints are not counted as new scene submissions

        QSignalSpy pointers(&map, &MapWidget::pointer);
        QPoint click = map.imageRect().center();
        QTest::mouseClick(gpu, Qt::LeftButton, Qt::NoModifier, click);
        QTRY_COMPARE(pointers.size(), 2);
        QCOMPARE(qvariant_cast<MapPointer>(pointers[0][0]).scene, dl->hit);
        int x, y;
        QVERIFY(map.toImage(click, x, y, false));
        QCOMPARE(qvariant_cast<MapPointer>(pointers[0][0]).ev.imgX, x);
        QCOMPARE(qvariant_cast<MapPointer>(pointers[0][0]).ev.imgY, y);
        QWheelEvent wheel(QPointF(click), gpu->mapToGlobal(QPointF(click)), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(gpu, &wheel);
        QCOMPARE(pointers.size(), 3);
        QCOMPARE(int(qvariant_cast<MapPointer>(pointers[2][0]).kind), int(MapPointer::Wheel));
        QCOMPARE(qvariant_cast<MapPointer>(pointers[2][0]).ev.imgX, x);
        QCOMPARE(QApplication::focusWidget(), &map);
        Keys keys;
        map.installEventFilter(&keys);
        QTest::keyClick(gpu, Qt::Key_A);
        QCOMPARE(keys.presses, 1);
        QCOMPARE(keys.releases, 1);

        map.setPerformanceText({});
        map.resize(81, 91);
        // Cocoa may animate native layer bounds after QWidget already reports its new logical size.
        QTRY_COMPARE(gpu->snapshotView().size(), map.size() * map.devicePixelRatioF());
        samePixels(gpu->snapshotView(), map.viewSnapshot());
        map.setMapScale(0, 0, true, "normal");  // GPU linear vs QPainter's fixed-point smooth magnification
        samePixels(gpu->snapshotView(), map.viewSnapshot(), 3);
        map.setMapScale(0, 0, false, "distort");  // centre-cropped cover
        samePixels(gpu->snapshotView(), map.viewSnapshot());
        map.hide();
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(map.windowHandle()));
        QTRY_COMPARE(gpu->snapshotView().size(), map.size() * map.devicePixelRatioF());
        samePixels(gpu->snapshotView(), map.viewSnapshot());
    }
    // Read from the screen, not the widget: a control stacked above the map shows over the native presenter.
    void controlsAboveTheMapStayVisible() {
        if (QGuiApplication::platformName() == "offscreen") QSKIP("needs a real screen to read pixels from");
        QWidget top;
        top.resize(96, 72);
        auto* map = new MapWidget(&top);
        map->setGeometry(0, 0, 96, 72);
        map->setMapScale(0, 0, true, "distort");
        auto* gpu = new GpuMapWindow(map);
        map->setExternalView(QWidget::createWindowContainer(gpu, map), gpu);
        connect(gpu, &GpuMapWindow::submitted, map,
                [map](GpuDrawList dl) { map->setExternalFrame(QSize(dl->outW, dl->outH), dl->hit, {}); });
        auto* label = new QLabel(&top);
        label->setGeometry(8, 8, 24, 16);
        label->setAutoFillBackground(true);
        label->setStyleSheet("background-color:#ff00ff");
        map->clipExternalView();
        QSignalSpy submitted(gpu, &GpuMapWindow::submitted);
        QVERIFY(gpu->setList(std::make_shared<const DrawList>(picture())));
        top.show();
        QVERIFY(QTest::qWaitForWindowExposed(&top));
        QTRY_COMPARE_WITH_TIMEOUT(submitted.size(), 1, 15000);
        auto onScreen = [&](QPoint p) {
            const QPoint g = top.mapToGlobal(p);
            return top.screen()->grabWindow(0, g.x(), g.y(), 1, 1).toImage().pixelColor(0, 0);
        };
        const QPoint under(20, 16), clear(70, 50);
        const QColor magenta(255, 0, 255);
        QTRY_COMPARE(onScreen(under), magenta);
        QTRY_COMPARE(onScreen(clear).rgb(), gpu->snapshotView().pixelColor(clear * top.devicePixelRatioF()).rgb());
        label->hide();
        map->clipExternalView();
        QTRY_COMPARE(onScreen(under).rgb(), gpu->snapshotView().pixelColor(under * top.devicePixelRatioF()).rgb());
    }
    void instancedAtlasFramesInFlight() {
        RestoreEnv trace("FUGUE_GPU_TRACE"), batching("FUGUE_GPU_BATCH_SPRITES");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + "/instances.csv";
        qputenv("FUGUE_GPU_TRACE", path.toUtf8());
        qputenv("FUGUE_GPU_BATCH_SPRITES", "1");
        MapWidget map;
        map.resize(128, 96);
        map.setMapScale(0, 0, false, "distort");
        auto* gpu = new GpuMapWindow(&map);
        auto* container = QWidget::createWindowContainer(gpu, &map);
        map.setExternalView(container);
        connect(gpu, &GpuMapWindow::submitted, &map, [&](GpuDrawList dl) {
            QPointer<GpuMapWindow> view = gpu;
            map.setExternalFrame(QSize(dl->outW, dl->outH), dl->hit,
                                 [view] { return view ? view->snapshot() : QImage(); });
        });
        QSignalSpy submitted(gpu, &GpuMapWindow::submitted), rejected(gpu, &GpuMapWindow::rejected);
        container->show();
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(map.windowHandle()));
        for (int frame = 0; frame < 24; ++frame) {
            DrawList dl;
            dl.outW = 32;
            dl.outH = 24;
            dl.planes.push_back(PlanePass{});
            for (int i = 0; i < 200; ++i) {
                auto s = std::make_shared<Sprite>();
                s->w = 4;
                s->h = 3;
                for (int p = 0; p < 12; ++p)
                    s->px.insert(s->px.end(),
                                 {uint8_t(i + frame), uint8_t(i / 4), uint8_t(p * 20), uint8_t((p + i) % 3 ? 255 : 0)});
                DrawLayer l;
                l.sprite = s;
                l.x = (i * 3) % 34 - 2;
                l.y = (i * 7) % 26 - 2;
                dl.planes[0].layers.push_back(l);
            }
            auto fixed = std::make_shared<Sprite>();
            fixed->w = fixed->h = 1;
            fixed->px = {190, 70, 40, 255};
            DrawLayer fixedLayer;
            fixedLayer.sprite = fixed;
            fixedLayer.x = 5;
            fixedLayer.y = 7;
            auto master = std::make_shared<PlaneMasterSpec>();
            master->hasColor = true;
            master->r = uint8_t(190 - (frame / 8) * 50);
            FilterSpec shadow;
            shadow.kind = FilterSpec::DropShadow;
            shadow.offset = 1;
            shadow.r = 40;
            shadow.g = 90;
            shadow.b = 130;
            master->filters = {shadow};
            dl.planes.push_back({1, {fixedLayer}, master});  // cached while the atlas-backed plane keeps animating
            auto list = std::make_shared<const DrawList>(std::move(dl));
            QVERIFY(gpu->setList(list));
            QTRY_COMPARE_WITH_TIMEOUT(submitted.size(), frame + 1, 15000);
            // Several swapchain submissions reuse plane outputs/update the same atlas without readback/finish.
            if (frame % 8 == 7) {
                samePixels(gpu->snapshotView(), map.viewSnapshot());
                samePixels(map.frame(), expected(*list));
            }
        }
        QCOMPARE(rejected.size(), 0);
        delete container;  // close the trace before reading all rows
        QFile csv(path + ".present.csv");
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const auto rows = csv.readAll().trimmed().split('\n');
        QCOMPARE(rows.size(), 25);
        const auto names = rows[0].split(',');
        QVERIFY(names.contains("instanced_draws"));
        QVERIFY(names.contains("instanced_sprites"));
        for (int i = 1; i < rows.size(); ++i) {
            const auto values = rows[i].split(',');
            QCOMPARE(values.size(), names.size());
            QCOMPARE(values[names.indexOf("instanced_draws")].toInt(), 1);
            QCOMPARE(values[names.indexOf("instanced_sprites")].toInt(), 200);
            QCOMPARE(values[names.indexOf("plane_cache_hits")].toInt(), (i - 1) % 8 ? 1 : 0);
            QCOMPARE(values[names.indexOf("filtered_plane_cache_hits")].toInt(), (i - 1) % 8 ? 1 : 0);
        }
    }

    void detailedMetricsToggleWithoutRecreatingTheView() {
        RestoreEnv trace("FUGUE_GPU_TRACE"), profile("FUGUE_GPU_PROFILE");
        qunsetenv("FUGUE_GPU_TRACE");
        qunsetenv("FUGUE_GPU_PROFILE");
        RenderThread thread(true);
        Stop stop{thread};
        SkinWindow window;
        auto* settings = window.displaySettings();
        settings->setGpuAvailable(true);
        settings->setUseGpu(true);
        GpuMapController presenter(thread, window);
        QVERIFY(window.loadSkin(skin).isEmpty());
        window.resize(240, 180);
        window.show();
        thread.start();
        auto dl = picture();
        thread.submit(dl, 2.5);
        QTRY_VERIFY_WITH_TIMEOUT(window.map()->hasExternalFrame(), 15000);
        auto m = thread.metrics().maps.at("");
        QCOMPARE(m.preparation.average, 2.5);
        QCOMPARE(m.cpuSubmit.samples, size_t(1));
        QCOMPARE(m.recording[0].samples, size_t(0));  // detailed clocks off by default
        double sum = 0;
        for (const auto& phase : m.phases) {
            QCOMPARE(phase.samples, size_t(1));
            sum += phase.last;
        }
        QVERIFY(std::abs(sum - m.cpuSubmit.last) < 0.00001);
        settings->setPerformanceOverlay(true);
        settings->setPerformanceDetails(true);
        dl.outW = 5;
        thread.submit(dl, 3.5);
        QTRY_COMPARE(thread.metrics().maps.at("").cpuSubmit.samples, size_t(2));
        m = thread.metrics().maps.at("");
        double recording = 0;
        for (const auto& phase : m.recording) {
            QCOMPARE(phase.samples, size_t(1));
            recording += phase.last;
        }
        QVERIFY(recording > 0);
        QVERIFY(recording <= m.phases[1].last);
        QCOMPARE(m.preparation.average, 3.0);
        samePixels(window.map()->frame(), expected(dl));
        // Screenshots and overlay-only repaints must not become scene timing samples.
        window.map()->setPerformanceText("metrics repaint");
        QTest::qWait(60);
        QCOMPARE(thread.metrics().maps.at("").cpuSubmit.samples, size_t(2));
        settings->setPerformanceOverlay(false);  // details preference remains checked, but clocks stop
        dl.outW = 6;
        thread.submit(dl, 4.5);
        QTRY_COMPARE(thread.metrics().maps.at("").cpuSubmit.samples, size_t(3));
        QCOMPARE(thread.metrics().maps.at("").recording[0].samples, size_t(1));
        QCOMPARE(thread.metrics().maps.at("").phases[0].samples, size_t(3));
        samePixels(window.map()->frame(), expected(dl));
    }
    void controllerFallbackSwitchAndRemount() {
        RenderThread thread(true);
        Stop stop{thread};
        SkinWindow window;
        window.displaySettings()->setGpuAvailable(true);
        window.displaySettings()->setUseGpu(true);
        GpuMapController presenter(thread, window);
        connect(window.displaySettings(), &DisplaySettings::rendererChanged, &thread, &RenderThread::setUseGpu);
        QVERIFY(window.loadSkin(skin).isEmpty());
        window.resize(240, 180);
        window.show();
        thread.start();
        auto dl = picture();
        thread.submit(dl);
        QTRY_VERIFY_WITH_TIMEOUT(window.map()->hasExternalFrame(), 15000);
        QVERIFY(thread.metrics().lastAsync);
        QVERIFY(thread.metrics().lastGpu);
        samePixels(window.map()->frame(), expected(dl));
        auto first = std::make_shared<const DrawList>(dl);
        dl.hit = std::make_shared<HitScene>();
        thread.submit(dl);
        QTRY_COMPARE(window.map()->hitScene(), dl.hit);
        QPointer<MapWidget> old = window.map();
        QVERIFY(window.loadSkin(skin).isEmpty());
        QVERIFY(old.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(window.map()->hasExternalFrame(), 15000);
        QCOMPARE(window.map()->hitScene(), dl.hit);
        samePixels(window.map()->frame(), expected(dl));
        QVERIFY(!window.grabWithMaps().isNull());

        window.displaySettings()->setUseGpu(false);
        QTRY_VERIFY(!thread.metrics().lastGpu);
        QTRY_VERIFY(!window.map()->hasExternalFrame());
        samePixels(window.map()->frame(), expected(dl));
        window.displaySettings()->setUseGpu(true);
        QTRY_VERIFY(window.map()->hasExternalFrame());
        QVERIFY(thread.metrics().lastAsync);
        auto good = dl;
        HudPass oversized;
        oversized.tileW = 4097;
        oversized.tileH = 1;
        dl.hud.push_back(oversized);
        thread.submit(dl);
        QTRY_VERIFY(thread.cpuFallbacks() > 0);
        QTRY_VERIFY(!window.map()->hasExternalFrame());
        samePixels(window.map()->frame(), expected(dl));
        dl.hit = std::make_shared<HitScene>();
        thread.submit(dl);
        QTRY_COMPARE(window.map()->hitScene(), dl.hit);
        thread.submit(good);
        QTRY_VERIFY(window.map()->hasExternalFrame());
        samePixels(window.map()->frame(), expected(good));
        QImage stale(4, 2, QImage::Format_RGBA8888);
        stale.fill(Qt::magenta);
        thread.fallbackFrame({}, first, stale);  // a late fallback must not replace a newer submitted scene
        samePixels(window.map()->frame(), expected(good));
    }
    void auxiliaryViewsPresentFallbackSwitchAndRemount() {
        RestoreEnv trace("FUGUE_GPU_TRACE");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("FUGUE_GPU_TRACE", dir.filePath("frames").toUtf8());
        RenderThread thread(true);
        Stop stop{thread};
        SkinWindow window;
        window.displaySettings()->setGpuAvailable(true);
        window.displaySettings()->setUseGpu(true);
        GpuMapController presenter(thread, window);
        connect(window.displaySettings(), &DisplaySettings::rendererChanged, &thread, &RenderThread::setUseGpu);
        connect(&window, &SkinWindow::auxMapBuilt, &thread,
                [&](const QString& id) { thread.forgetAux(id.toStdString()); });
        connect(&window, &SkinWindow::auxMapRemoved, &thread,
                [&](const QString& id) { thread.removeAux(id.toStdString()); });
        QVERIFY(window.loadSkin(skin).isEmpty());
        window.resize(240, 180);
        window.show();
        QSignalSpy cpuAux(&thread, &RenderThread::auxFrame);
        auto main = picture(), camera = picture(), radar = picture();
        camera.outW = 5;
        radar.outH = 3;
        auto master = std::make_shared<PlaneMasterSpec>();
        FilterSpec outline;
        outline.kind = FilterSpec::Outline;
        outline.size = 1;
        outline.flags = 3;
        outline.r = 0;
        outline.g = 255;
        outline.b = 0;
        master->filters = {outline};
        master->hasXform = true;
        master->xf = {-1, 0, 0, 0, 1, 0};
        radar.planes[0].master = master;
        HudPass hud;
        hud.tileW = 1;
        hud.tileH = 1;
        hud.layers = {radar.planes[0].layers[0]};
        hud.dests = {{0, 1}};
        radar.hud = {hud};
        thread.start();
        thread.submit(main);
        thread.submitAux("Camera", camera);
        thread.submitAux("radar", radar);
        QTRY_VERIFY_WITH_TIMEOUT(window.map()->hasExternalFrame(), 15000);
        // Auxiliary lists may arrive before the popup/native views exist.
        window.live()->setPopups({"cameras"});
        window.showBrowse("cameras", "window=cameras;size=400x240", "<div>camera views</div>");
        auto mount = [&](const char* id, const char* pos) {
            window.live()->winset(id, std::string("parent=cameras;type=map;size=180x180;pos=") + pos);
            window.syncFromLive();
        };
        mount("camera", "0,0");
        mount("radar", "200,0");
        QVERIFY(window.auxMap("camera"));
        QVERIFY(window.auxMap("radar"));
        QTRY_VERIFY_WITH_TIMEOUT(window.auxMap("camera")->hasExternalFrame(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(window.auxMap("radar")->hasExternalFrame(), 15000);
        samePixels(window.auxMap("camera")->frame(), expected(camera));
        samePixels(window.auxMap("radar")->frame(), expected(radar));
        QCOMPARE(cpuAux.size(), 0);  // neither secondary map uses worker GPU readback
        QCOMPARE(thread.metrics().mainFrames, uint64_t(1));
        QCOMPARE(thread.metrics().auxFrames, uint64_t(2));
        QCOMPARE(QDir(dir.path()).entryList({"*.present.csv"}, QDir::Files).size(), 3);

        auto unsupported = camera;
        HudPass oversized;
        oversized.tileW = 4097;
        oversized.tileH = 1;
        unsupported.hud.push_back(oversized);
        thread.submitAux("camera", unsupported);
        QTRY_COMPARE(thread.cpuFallbacks(), uint64_t(1));
        QTRY_VERIFY(!window.auxMap("camera")->hasExternalFrame());
        samePixels(window.auxMap("camera")->frame(), expected(unsupported));
        QVERIFY(window.auxMap("radar")->hasExternalFrame());
        QVERIFY(window.map()->hasExternalFrame());
        thread.submitAux("camera", camera);
        QTRY_VERIFY(window.auxMap("camera")->hasExternalFrame());
        QImage stale(5, 2, QImage::Format_RGBA8888);
        stale.fill(Qt::magenta);
        thread.fallbackFrame("camera", std::make_shared<const DrawList>(unsupported), stale);
        samePixels(window.auxMap("camera")->frame(), expected(camera));

        window.displaySettings()->setUseGpu(false);
        QTRY_VERIFY(!window.map()->hasExternalFrame());
        QTRY_VERIFY(!window.auxMap("camera")->hasExternalFrame());
        QTRY_VERIFY(!window.auxMap("radar")->hasExternalFrame());
        samePixels(window.auxMap("radar")->frame(), expected(radar));
        window.displaySettings()->setUseGpu(true);
        QTRY_VERIFY(window.map()->hasExternalFrame());
        QTRY_VERIFY(window.auxMap("camera")->hasExternalFrame());
        QTRY_VERIFY(window.auxMap("radar")->hasExternalFrame());

        QPointer<MapWidget> old = window.auxMap("camera");
        window.live()->winset("camera", "parent=");
        window.syncFromLive();
        QVERIFY(old.isNull());
        QVERIFY(!window.auxMap("camera"));
        mount("camera", "0,0");
        thread.submitAux("camera", camera);  // identical pixels must reach the remounted view
        QTRY_VERIFY_WITH_TIMEOUT(window.auxMap("camera")->hasExternalFrame(), 15000);
        samePixels(window.auxMap("camera")->frame(), expected(camera));
        QVERIFY(window.auxMap("radar")->hasExternalFrame());
        QVERIFY(!window.grabWithMaps().isNull());
    }
    void boundedDirectMailbox() {
        RenderThread thread(true);
        std::atomic<int> notifications{0};
        Stop stop{thread};  // stop before the callback's captured counter is destroyed
        connect(&thread, &RenderThread::gpuListReady, &thread, [&] { ++notifications; }, Qt::DirectConnection);
        thread.start();
        auto dl = picture();
        thread.submit(dl);
        QTRY_COMPARE(notifications.load(), 1);
        for (int i = 1; i < 40; ++i) {
            dl.outW = 4 + i;
            dl.hit = std::make_shared<HitScene>();
            thread.submit(dl);
            QThread::msleep(2);  // no GUI event processing; only one wake-up may be queued
        }
        QCOMPARE(notifications.load(), 1);
        RenderThread::DirectFrame frame;
        auto drain = [&] {
            auto next = thread.takeDirectFrames();
            if (next.count("")) frame = std::move(next.at(""));
            return frame.list && frame.list->outW == 43;
        };
        QTRY_VERIFY(drain());
        auto newestHit = std::make_shared<HitScene>();
        dl.hit = newestHit;
        thread.submit(dl);  // same pixels, new hits: no new GPU pixel list
        RenderThread::DirectFrame same;
        auto drainHit = [&] {
            auto next = thread.takeDirectFrames();
            if (next.count("")) same = std::move(next.at(""));
            return same.hit == newestHit;
        };
        QTRY_VERIFY(drainHit());
        QCOMPARE(same.list, frame.list);
        QVERIFY(thread.unchanged() > 0);
        QVERIFY(thread.takeDirectFrames().empty());
        QVERIFY(thread.dropped() > 0);
    }
    void failedDeviceUsesWorkerCpu() {
        RestoreEnv backend("FUGUE_GPU_BACKEND");
        qputenv("FUGUE_GPU_BACKEND", "invalid-test-backend");
        RenderThread thread(true);
        Stop stop{thread};
        SkinWindow window;
        window.displaySettings()->setGpuAvailable(true);
        window.displaySettings()->setUseGpu(true);
        GpuMapController presenter(thread, window);
        QVERIFY(window.loadSkin(skin).isEmpty());
        window.resize(240, 180);
        window.show();
        thread.start();
        auto dl = picture();
        thread.submit(dl);
        QTRY_COMPARE_WITH_TIMEOUT(thread.cpuFallbacks(), uint64_t(1), 15000);
        QTRY_VERIFY(window.map()->hasFrame());
        QVERIFY(!window.map()->hasExternalFrame());
        samePixels(window.map()->frame(), expected(dl));
        QCOMPARE(thread.gpuExecuted(), uint64_t(0));
        QVERIFY(thread.metrics().fallbackReason.find("cannot initialize") != std::string::npos);
    }
};
int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    GpuPresentTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "gpu_present_test.moc"

#include <QtTest>
#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <QTimer>
#include "app/renderthread.h"
#include "app/rendermonitor.h"
#include "shell/browserpane.h"
#include "shell/displaysettings.h"
#include "shell/macrodialog.h"
#include "shell/skinwindow.h"

using namespace fugue::shell;
using namespace fugue::render;

namespace {
const char* skin = R"(menu "menu"
    elem
        name = "&File"
        command = ""
    elem
        name = "Say hello"
        command = "hello"
        category = "&File"
window "main"
    elem "main"
        type = MAIN
        size = 420x260
        is-default = true
        menu = "menu"
    elem "map"
        type = MAP
        pos = 0,0
        size = 420x240
        is-default = true
)";
struct Stop {
    RenderThread& thread;
    ~Stop() {
        thread.requestStop();
        thread.wait();
    }
};
DrawList picture() {
    auto s = std::make_shared<Sprite>();
    s->w = 1;
    s->h = 1;
    s->px = {170, 40, 20, 255};
    DrawLayer layer;
    layer.sprite = s;
    DrawList dl;
    dl.outW = 3;
    dl.outH = 2;
    dl.planes.push_back(PlanePass{0, {layer}, {}});
    dl.hit = std::make_shared<HitScene>();
    return dl;
}
}  // namespace

class RenderControlsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<HitPtr>();
        qRegisterMetaType<MapPointer>();
    }

    void boundedTimingWindowsAndGuiLag() {
        using namespace fugue::app;
        TimingWindow timing;
        QCOMPARE(timing.summary().samples, size_t(0));
        const auto stamp = MetricsClock::now();
        timing.add(-1, stamp);
        timing.add(std::nan(""), stamp);
        timing.add(INFINITY, stamp);
        QCOMPARE(timing.summary().samples, size_t(0));
        timing.add(0, stamp);
        QCOMPARE(timing.summary(stamp).samples, size_t(1));
        QCOMPARE(timing.summary(stamp).p95, 0.0);
        for (int i = 1; i <= 150; ++i) timing.add(i, stamp);
        const auto stats = timing.summary(stamp + std::chrono::milliseconds(250));
        QCOMPARE(stats.samples, TimingWindow::capacity);
        QCOMPARE(stats.average, 90.5);  // retained values 31..150
        QCOMPARE(stats.p95, 144.0);
        QCOMPARE(stats.maximum, 150.0);
        QCOMPARE(stats.last, 150.0);
        QCOMPARE(stats.ageMs, 250.0);
        EventLoopHealth health;
        health.sampleGap(40);
        health.sampleGap(50);
        health.sampleGap(70);
        health.sampleGap(150);
        health.sampleGap(-1);
        health.sampleGap(INFINITY);
        QCOMPARE(health.stalls, uint64_t(1));
        QCOMPARE(health.lag.summary().samples, size_t(4));
        QCOMPARE(health.lag.summary().average, 30.0);
        QCOMPARE(health.lag.summary().maximum, 100.0);
    }

    void timingsRemainSeparateByMapAndExecutionKind() {
        RenderThread thread;
        Stop stop{thread};
        auto dl = picture();
        for (int i = 1; i <= 150; ++i) thread.submit(dl, i);
        thread.submitAux("Camera", dl, 5);
        thread.submitAux("camera", dl, 7);
        fugue::app::RenderPhases phases;
        phases.wallMs = {1, 2, 3, 4};
        phases.recordMs = {0.1, 0.2, 0.3, 0.4, 0.5};
        thread.reportSubmission("", 10, 6, "main backend", phases);
        thread.reportSubmission("CAMERA", 99, -1, "aux backend");
        auto maps = thread.metrics().maps;
        QCOMPARE(maps.size(), size_t(2));
        QCOMPARE(maps.at("").prepared, uint64_t(150));
        QCOMPARE(maps.at("").dropped, uint64_t(149));
        QCOMPARE(maps.at("").preparation.average, 90.5);
        QCOMPARE(maps.at("").preparation.p95, 144.0);
        QCOMPARE(maps.at("").cpuSubmit.average, 10.0);
        QCOMPARE(maps.at("").gpu.average, 6.0);
        QCOMPARE(maps.at("").phases[1].average, 2.0);
        QCOMPARE(maps.at("").recording[2].average, 0.3);
        QCOMPARE(maps.at("camera").preparation.average, 6.0);
        QCOMPARE(maps.at("camera").dropped, uint64_t(1));
        QCOMPARE(maps.at("camera").cpuSubmit.average, 99.0);
        QCOMPARE(maps.at("camera").gpu.samples, size_t(0));  // unavailable isn't zero
        QCOMPARE(maps.at("camera").phases[0].samples, size_t(0));
        thread.reportSubmission("", 12, -1, "main backend");
        maps = thread.metrics().maps;
        QCOMPARE(maps.at("").gpu.samples, size_t(1));
        QCOMPARE(maps.at("").lastGpuMs, -1.0);
        thread.start();  // consume the pending lists on the CPU, without mixing them into submit timings
        QTRY_COMPARE(thread.metrics().cpuFrames, uint64_t(2));
        maps = thread.metrics().maps;
        QCOMPARE(maps.at("").cpuSubmit.average, 11.0);
        QCOMPARE(maps.at("").cpuRender.samples, size_t(1));
        QCOMPARE(maps.at("camera").cpuRender.samples, size_t(1));
        QVERIFY(!maps.at("").lastGpu);
        thread.submit(dl, 200);
        thread.submitAux("camera", dl, 8);
        QTRY_COMPARE(thread.metrics().maps.at("").unchanged, uint64_t(1));
        QTRY_COMPARE(thread.metrics().maps.at("camera").unchanged, uint64_t(1));
        QCOMPARE(thread.metrics().maps.at("").prepared, uint64_t(151));
        QCOMPARE(thread.metrics().maps.at("").cpuRender.samples, size_t(1));
        thread.removeAux("Camera");
        QTRY_VERIFY(!thread.metrics().maps.count("camera"));
    }

    void menusShareStateAcrossRebuilds() {
        SkinWindow window;
        auto* settings = window.displaySettings();
        auto* cpu = settings->findChild<QAction*>("rendererCpu");
        auto* gpu = settings->findChild<QAction*>("rendererGpu");
        auto* metrics = settings->findChild<QAction*>("performanceMetrics");
        auto* details = settings->findChild<QAction*>("performanceDetails");
        QVERIFY(cpu && gpu && metrics && details);
        QVERIFY(!details->isEnabled());
        QVERIFY(!details->isChecked());
        QVERIFY(cpu->isChecked());
        QVERIFY(!gpu->isEnabled());
        QVERIFY(!metrics->isChecked());
        QVERIFY(gpu->toolTip().contains("not built"));
        settings->setUseGpu(true);
        QVERIFY(!settings->useGpu());
        settings->setGpuAvailable(true);
        QVERIFY(window.loadSkin(skin).isEmpty());
        OptionsWindow options(&window, settings);
        QVERIFY(window.systemMenuButton());
        auto* systemClient = window.systemMenuButton()->menu()->actions()[1]->menu();
        auto* optionsClient = options.menuBar()->actions()[0]->menu();
        QVERIFY(systemClient->actions().contains(metrics));
        QVERIFY(optionsClient->actions().contains(metrics));
        QVERIFY(systemClient->actions().contains(details));
        QVERIFY(optionsClient->actions().contains(details));
        auto* renderer = systemClient->findChild<QMenu*>("rendererMenu");
        QVERIFY(renderer);
        QCOMPARE(renderer->actions(), QList<QAction*>({cpu, gpu}));
        QSignalSpy selected(settings, &DisplaySettings::rendererChanged);
        QSignalSpy toggled(settings, &DisplaySettings::performanceOverlayChanged);
        QSignalSpy commands(&window, &SkinWindow::menuCommand);
        QSignalSpy expanded(settings, &DisplaySettings::performanceDetailsChanged);
        gpu->trigger();
        metrics->trigger();
        QVERIFY(details->isEnabled());
        details->trigger();
        QVERIFY(settings->performanceDetails());
        QCOMPARE(expanded.size(), 1);
        QVERIFY(settings->useGpu());
        QVERIFY(!cpu->isChecked());
        QVERIFY(gpu->isChecked());
        QVERIFY(settings->performanceOverlay());
        QCOMPARE(selected.size(), 1);
        QCOMPARE(toggled.size(), 1);
        QCOMPARE(commands.size(), 0);  // local controls are never sent to the world as commands
        QVERIFY(window.loadSkin(skin).isEmpty());
        QVERIFY(gpu->isChecked());
        QVERIFY(metrics->isChecked());
        QVERIFY(details->isChecked());
        QVERIFY(window.systemMenuButton());
        QVERIFY(window.systemMenuButton()->menu()->actions()[1]->menu()->actions().contains(metrics));
        cpu->trigger();
        metrics->trigger();
        QVERIFY(!settings->useGpu());
        QVERIFY(!settings->performanceOverlay());
        QVERIFY(!details->isEnabled());
        QVERIFY(details->isChecked());  // preserve expansion preference while hidden
        QCOMPARE(selected.size(), 2);
        QCOMPARE(toggled.size(), 2);
        settings->setUseGpu(true);
        settings->setGpuAvailable(false);
        QVERIFY(cpu->isChecked());
        QVERIFY(!gpu->isEnabled());
        QVERIFY(!gpu->isChecked());
    }

    // A build without GPU support keeps the stock Client menu.
    void noDisplayEntriesWithoutGpu() {
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* client = window.systemMenuButton()->menu()->actions()[1]->menu();
        QVERIFY(!client->findChild<QMenu*>("rendererMenu"));
        QVERIFY(!client->actions().contains(window.displaySettings()->findChild<QAction*>("performanceMetrics")));
        OptionsWindow options(&window, window.displaySettings());
        QVERIFY(!options.menuBar()->actions()[0]->menu()->findChild<QMenu*>("rendererMenu"));
    }

    void clientMenusRemainVisibleInWindow() {
        SkinWindow window;
        window.displaySettings()->setGpuAvailable(true);
        QVERIFY(window.loadSkin(skin).isEmpty());
        QVERIFY(window.menuBar());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* button = window.systemMenuButton();
        QVERIFY(button);
        QVERIFY(button->isVisibleTo(&window));
        QVERIFY(window.rect().contains(button->mapTo(&window, button->rect().center())));
        bool opened = false;
        QTimer::singleShot(50, button->menu(), [&] {
            opened = button->menu()->isVisible();
            button->menu()->close();
        });
        QTest::mouseClick(button, Qt::LeftButton);
        QTRY_VERIFY(opened);
        auto* client = button->menu()->actions()[1]->menu();
        QVERIFY(client->findChild<QMenu*>("rendererMenu"));

        OptionsWindow options(&window, window.displaySettings());
        options.show();
        QVERIFY(QTest::qWaitForWindowExposed(&options));
        QVERIFY(options.menuBar()->isVisibleTo(&options));
        QVERIFY(options.menuBar()->actions()[0]->menu()->findChild<QMenu*>("rendererMenu"));
    }

    void overlayDoesNotChangeFrameHitsOrInput() {
        MapWidget map;
        map.resize(420, 260);
        QImage frame(420, 260, QImage::Format_RGBX8888);
        frame.fill(Qt::blue);
        auto hit = std::make_shared<HitScene>();
        map.setFrame(frame, hit);
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        auto plain = map.grab().toImage();
        map.setPerformanceText("CPU\nLast render: 1.23 ms\nMain renders/s: 20.0");
        QVERIFY(map.grab().toImage() != plain);
        QCOMPARE(map.frame(), frame);
        QCOMPARE(map.framesShown(), uint64_t(1));
        QCOMPARE(map.hitScene(), HitPtr(hit));
        QVERIFY(map.childAt(18, 18) == nullptr);  // no input-catching overlay widget
        QSignalSpy pointer(&map, &MapWidget::pointer);
        QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, QPoint(18, 18));
        QCOMPARE(pointer.size(), 2);
        auto event = qvariant_cast<MapPointer>(pointer[0][0]);
        QCOMPARE(event.scene, HitPtr(hit));
        QCOMPARE(event.ev.imgX, 18);
        QCOMPARE(event.ev.imgY, 18);
        map.setPerformanceText({});
        QCOMPARE(map.grab().toImage(), plain);
    }

    void monitorIsOptInAndSurvivesMapReplacement() {
        RenderThread thread;
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        QVERIFY(window.map());
        RenderMonitor monitor(thread, window);
        QVERIFY(!monitor.active());
        QVERIFY(!monitor.probing());
        QVERIFY(window.map()->performanceText().isEmpty());
        window.displaySettings()->setPerformanceOverlay(true);
        QVERIFY(monitor.active());
        QVERIFY(monitor.probing());
        QVERIFY(window.map()->performanceText().contains("waiting for frame"));
        QVERIFY(window.map()->performanceText().contains("Scene updates/s: 0.0"));
        QVERIFY(window.map()->performanceText().contains("Scene prepare: unavailable"));
        QVERIFY(window.loadSkin(skin).isEmpty());
        QVERIFY(window.map()->performanceText().contains("waiting for frame"));
        window.displaySettings()->setPerformanceOverlay(false);
        QVERIFY(!monitor.active());
        QVERIFY(!monitor.probing());
        QVERIFY(window.map()->performanceText().isEmpty());
        monitor.refresh();
        QVERIFY(window.map()->performanceText().isEmpty());
    }

    void monitorShowsIndependentMapsDetailsAndClearsAllOverlays() {
        RenderThread thread;
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* settings = window.displaySettings();
        settings->setGpuAvailable(true);
        settings->setUseGpu(true);
        RenderMonitor monitor(thread, window);
        auto dl = picture();
        thread.submit(dl, 2);
        thread.submitAux("camera", dl, 20);
        fugue::app::RenderPhases phases;
        phases.wallMs = {1, 2, 3, 4};
        phases.recordMs = {0.1, 0.2, 0.3, 0.4, 0.5};
        thread.reportSubmission("", 10, 6, "test main", phases);
        thread.reportSubmission("camera", 99, -1, "test camera");
        settings->setPerformanceOverlay(true);
        auto text = window.map()->performanceText();
        QVERIFY(text.contains("Map: main"));
        QVERIFY(text.contains("Scene prepare: 2.00 / 2.00 ms"));
        QVERIFY(text.contains("Render CPU (submit): 10.00 / 10.00 ms"));
        QVERIFY(text.contains("GPU: 6.00 / 6.00 ms"));
        QVERIFY(!text.contains("99.00"));
        QVERIFY(!text.contains("Preflight / acquire"));
        window.live()->setPopups({"cameras"});
        window.showBrowse("cameras", "window=cameras;size=420x260", "<div>camera</div>");
        window.live()->winset("camera", "parent=cameras;type=map;size=420x260;pos=0,0");
        window.syncFromLive();
        QVERIFY(window.auxMap("camera"));
        monitor.refresh();
        text = window.auxMap("camera")->performanceText();
        QVERIFY(text.contains("Map: camera"));
        QVERIFY(text.contains("Scene prepare: 20.00 / 20.00 ms"));
        QVERIFY(text.contains("Render CPU (submit): 99.00 / 99.00 ms"));
        QVERIFY(text.contains("GPU: unavailable"));
        settings->setPerformanceDetails(true);
        text = window.map()->performanceText();
        QVERIFY(text.contains("Preflight / acquire: 1.00 / 1.00 ms"));
        QVERIFY(text.contains("Plan / upload prep: 0.20 / 0.20 ms"));
        QVERIFY(text.contains("n=1"));
        QVERIFY(text.contains("not paired with CPU"));
        QVERIFY(window.auxMap("camera")->performanceText().contains("Preflight / acquire: unavailable"));
        // A zero-update interval reports zero, without treating retained timings as fresh work.
        monitor.refresh();
        QVERIFY(window.map()->performanceText().contains("Scene updates/s: 0.0"));
        QVERIFY(window.map()->performanceText().contains("Last scene render sample:"));
        QTest::qWait(60);  // establish the GUI probe, then deliberately block this thread
        QThread::msleep(140);
        QTest::qWait(10);
        monitor.refresh();
        QVERIFY(!window.map()->performanceText().contains("stalls >=50 ms: 0"));
        QVERIFY(window.map()->performanceText().contains("GUI lag p95:"));
        settings->setPerformanceOverlay(false);
        QVERIFY(!monitor.active());
        QVERIFY(!monitor.probing());
        QVERIFY(window.map()->performanceText().isEmpty());
        QVERIFY(window.auxMap("camera")->performanceText().isEmpty());
        monitor.refresh();
        QVERIFY(window.auxMap("camera")->performanceText().isEmpty());
    }

    void cpuMetricsAndUnavailableGpuSwitch() {
        // This target deliberately does not link rendergpu: deterministic CPU-only capability/fallback coverage.
        RenderThread thread;
        Stop stop{thread};
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* settings = window.displaySettings();
        settings->setGpuAvailable(true);  // simulate a selectable GPU which fails at execution time
        connect(settings, &DisplaySettings::rendererChanged, &thread, &RenderThread::setUseGpu);
        connect(&thread, &RenderThread::frame, &window, &SkinWindow::setMapFrame);
        RenderMonitor monitor(thread, window);
        settings->findChild<QAction*>("performanceMetrics")->trigger();
        QSignalSpy frames(&thread, &RenderThread::frame), aux(&thread, &RenderThread::auxFrame);
        QSignalSpy scenes(&thread, &RenderThread::sceneOnly);
        auto dl = picture();
        for (int i = 0; i < 10; i++) thread.submit(dl);
        thread.submitAux("camera", dl);
        thread.start();
        QTRY_COMPARE(frames.size(), 1);
        QTRY_COMPARE(aux.size(), 1);
        auto metrics = thread.metrics();
        QCOMPARE(metrics.mainFrames, uint64_t(1));
        QCOMPARE(metrics.auxFrames, uint64_t(1));
        QCOMPARE(metrics.cpuFrames, uint64_t(2));
        QCOMPARE(metrics.gpuFrames, uint64_t(0));
        QCOMPARE(metrics.dropped, uint64_t(9));
        QVERIFY(metrics.lastRenderMs >= 0);
        dl.hit = std::make_shared<HitScene>();
        thread.submit(dl);
        QTRY_COMPARE(scenes.size(), 1);
        QCOMPARE(thread.metrics().unchanged, uint64_t(1));
        settings->findChild<QAction*>("rendererGpu")->trigger();
        QTRY_COMPARE(frames.size(), 2);
        QTRY_COMPARE(aux.size(), 2);
        metrics = thread.metrics();
        QCOMPARE(metrics.fallbacks, uint64_t(2));
        QVERIFY(!metrics.lastGpu);
        QVERIFY(metrics.fallbackReason.find("not built") != std::string::npos);
        QCOMPARE(qvariant_cast<HitPtr>(frames.back()[1]), dl.hit);
        monitor.refresh();
        QVERIFY(settings->useGpu());  // fallback does not change the preference or radio selection
        QVERIFY(window.map()->performanceText().contains("Requested: GPU"));
        QVERIFY(window.map()->performanceText().contains("CPU fallback: GPU support was not built"));
        settings->findChild<QAction*>("rendererCpu")->trigger();
        QTRY_COMPARE(frames.size(), 3);
        QTRY_COMPARE(aux.size(), 3);
        monitor.refresh();
        QVERIFY(window.map()->performanceText().contains("Requested: CPU"));
        QVERIFY(!window.map()->performanceText().contains("CPU fallback:"));
        QVERIFY(thread.metrics().fallbackReason.empty());
        QCOMPARE(thread.metrics().cpuFrames, uint64_t(6));
        QCOMPARE(thread.metrics().fallbacks, uint64_t(2));
    }
};

int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    RenderControlsTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "rendercontrols_test.moc"

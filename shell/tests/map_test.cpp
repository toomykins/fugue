// Portable MapWidget configuration, menu routing and skin lifecycle checks.
#include <QtTest>
#include "tests/testmain.h"
#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <optional>
#include <vector>
#include <QStyleFactory>
#include "shell/browsehost.h"
#include "shell/gpubackend.h"
#include "shell/mapwidget.h"
#include "shell/skinwindow.h"
#include "shell/inputrouter.h"
#include "shell/tests/displayfixture.h"
using namespace fugue::shell;

static const char* skin = R"(menu "menu"
    elem
        name = "&File"
        command = ""
    elem
        name = "Hello"
        command = "hello"
        category = "&File"
window "main"
    elem "main"
        type = MAIN
        size = 640x480
        is-default = true
        menu = "menu"
    elem "map"
        type = MAP
        size = 480x480
        is-default = true
)";

static void wireDisplayCommands(SkinWindow& window) {
    QObject::connect(&window, &SkinWindow::menuCommand, &window, [&window](const QString& command) {
        const auto route = routeCommand(command.toStdString(), "", false);
        if (route.kind == Route::Winset) {
            window.live()->winset("", route.winsetBody.empty() ? route.text : route.winsetBody);
            window.syncFromLive();
        }
    });
}
static QAction* displayAction(SkinWindow& window, const QString& name) {
    for (auto* action : window.findChildren<QAction*>()) if (action->text() == name) return action;
    return nullptr;
}

class MapTest : public QObject {
    Q_OBJECT
private slots:
    // FUGUE_GPU_BACKEND asks for the map's graphics API (which one is created, the native suites check).
    void backendSelection() {
        const bool existed=qEnvironmentVariableIsSet("FUGUE_GPU_BACKEND");
        const auto original=qgetenv("FUGUE_GPU_BACKEND");
        const auto restore=qScopeGuard([&] {
            if(existed) qputenv("FUGUE_GPU_BACKEND",original);else qunsetenv("FUGUE_GPU_BACKEND");
        });
        qunsetenv("FUGUE_GPU_BACKEND");MapWidget ordinary;
        qputenv("FUGUE_GPU_BACKEND","opengl");MapWidget forced;
        QCOMPARE(forced.api(),QRhiWidget::Api::OpenGL);
        qputenv("FUGUE_GPU_BACKEND","OpenGL");MapWidget anyCase;
        QCOMPARE(anyCase.api(),QRhiWidget::Api::OpenGL);
        qputenv("FUGUE_GPU_BACKEND","d3d11");MapWidget direct;
        QCOMPARE(direct.api(),QRhiWidget::Api::Direct3D11);
        qputenv("FUGUE_GPU_BACKEND","metal");MapWidget metal;
        QCOMPARE(metal.api(),QRhiWidget::Api::Metal);
        qputenv("FUGUE_GPU_BACKEND","nonsense");MapWidget unknown;
        QCOMPARE(unknown.api(),ordinary.api());
        qputenv("FUGUE_GPU_BACKEND","vulkan");MapWidget vulkan;
        QCOMPARE(vulkan.api(),QRhiWidget::Api::Vulkan);
        // A software device names its API first: the map takes the API, the device is the drivers' business.
        qputenv("FUGUE_GPU_BACKEND","opengl-llvmpipe");MapWidget llvmpipe;
        QCOMPARE(llvmpipe.api(),QRhiWidget::Api::OpenGL);
        qputenv("FUGUE_GPU_BACKEND","vulkan-lavapipe");MapWidget lavapipe;
        QCOMPARE(lavapipe.api(),QRhiWidget::Api::Vulkan);
        qputenv("FUGUE_GPU_BACKEND","d3d11-warp");MapWidget warp;
        QCOMPARE(warp.api(),QRhiWidget::Api::Direct3D11);
        // A widget's own selection still overrides the environment.
        forced.setApi(ordinary.api());QCOMPARE(forced.api(),ordinary.api());
    }
    // Start-up leaves our variable and Qt's naming one backend, whatever either held (shell/gpubackend.h).
    void backendSettled() {
        const auto ours=qgetenv("FUGUE_GPU_BACKEND"),qts=qgetenv("QSG_RHI_BACKEND");
        const auto restore=qScopeGuard([&] {qputenv("FUGUE_GPU_BACKEND",ours);qputenv("QSG_RHI_BACKEND",qts);});
#if defined(Q_OS_WIN)
        QElapsedTimer probe; probe.start(); const bool vulkan=vulkanAvailable();
        qInfo("Vulkan usable: %d (asked in %lld ms)",int(vulkan),probe.elapsed());
        const QByteArray usual=vulkan ? "vulkan" : "d3d11"; // Vulkan where the machine can open a device with it
#elif defined(Q_OS_MACOS)
        const QByteArray usual="metal";
#else
        const QByteArray usual="opengl";
#endif
        auto settled=[](const char* asked,const char* qt,bool (*metal)()=nullptr) {
            if(asked) qputenv("FUGUE_GPU_BACKEND",asked);else qunsetenv("FUGUE_GPU_BACKEND");
            if(qt) qputenv("QSG_RHI_BACKEND",qt);else qunsetenv("QSG_RHI_BACKEND");
            const auto result=settleGpuBackend(metal);
            return result==qgetenv("FUGUE_GPU_BACKEND") && result==qgetenv("QSG_RHI_BACKEND") ? result : QByteArray("differ");
        };
        QCOMPARE(settled(nullptr,nullptr),usual);
        QCOMPARE(settled(nullptr,"opengl"),usual); // Qt's variable alone chooses nothing
        QCOMPARE(settled("OpenGL","d3d11"),QByteArray("opengl"));
        QCOMPARE(settled("vulkan",nullptr),QByteArray("vulkan"));
        QCOMPARE(settled("d3d11","vulkan"),QByteArray("d3d11"));
        QTest::ignoreMessage(QtWarningMsg,QRegularExpression("FUGUE_GPU_BACKEND=nonsense"));
        QCOMPARE(settled("nonsense","opengl"),usual);
#ifdef Q_OS_MACOS
        QCOMPARE(settled(nullptr,nullptr,[]{return false;}),QByteArray("opengl")); // a Mac without Metal
        QCOMPARE(settled("metal",nullptr,[]{return false;}),QByteArray("metal")); // asked for by name: as given
#endif
        // A software device: our variable keeps it, Qt's names the API, the drivers are told which device to load and
        // web pages are drawn in software. The variables this sets are this process's, so put them back.
        const char* touched[]={"LIBGL_ALWAYS_SOFTWARE","__GLX_VENDOR_LIBRARY_NAME","__EGL_VENDOR_LIBRARY_FILENAMES",
                               "VK_LOADER_DRIVERS_SELECT","QSG_RHI_PREFER_SOFTWARE_RENDERER","QTWEBENGINE_CHROMIUM_FLAGS"};
        std::vector<std::optional<QByteArray>> before;
        for(const char* name:touched) before.push_back(qEnvironmentVariableIsSet(name) ? std::optional(qgetenv(name)) : std::nullopt);
        const auto restoreDrivers=qScopeGuard([&] {
            for(size_t i=0;i<std::size(touched);++i)
                if(before[i]) qputenv(touched[i],*before[i]); else qunsetenv(touched[i]);
        });
        auto software=[](const char* asked) {
            qputenv("FUGUE_GPU_BACKEND",asked);qunsetenv("QTWEBENGINE_CHROMIUM_FLAGS");
            return settleGpuBackend();
        };
#if defined(Q_OS_WIN)
        QCOMPARE(software("d3d11-warp"),QByteArray("d3d11-warp"));
        QCOMPARE(qgetenv("QSG_RHI_BACKEND"),QByteArray("d3d11"));
        QCOMPARE(qgetenv("QSG_RHI_PREFER_SOFTWARE_RENDERER"),QByteArray("1"));
        QVERIFY(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").split(' ').contains("--disable-gpu"));
#elif !defined(Q_OS_MACOS)
        QCOMPARE(software("opengl-llvmpipe"),QByteArray("opengl-llvmpipe"));
        QCOMPARE(qgetenv("QSG_RHI_BACKEND"),QByteArray("opengl"));
        QCOMPARE(qgetenv("LIBGL_ALWAYS_SOFTWARE"),QByteArray("1"));
        QCOMPARE(qgetenv("__GLX_VENDOR_LIBRARY_NAME"),QByteArray("mesa"));
        QVERIFY(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").split(' ').contains("--disable-gpu"));
        QCOMPARE(software("vulkan-lavapipe"),QByteArray("vulkan-lavapipe"));
        QCOMPARE(qgetenv("QSG_RHI_BACKEND"),QByteArray("vulkan"));
        QCOMPARE(qgetenv("VK_LOADER_DRIVERS_SELECT"),QByteArray("*lvp*"));
        // Flags already given are kept, and --disable-gpu is not added twice.
        qputenv("FUGUE_GPU_BACKEND","opengl-llvmpipe");qputenv("QTWEBENGINE_CHROMIUM_FLAGS","--foo --disable-gpu");
        settleGpuBackend();
        QCOMPARE(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS"),QByteArray("--foo --disable-gpu"));
        // Another platform's software device is not this platform's backend.
        QTest::ignoreMessage(QtWarningMsg,QRegularExpression("FUGUE_GPU_BACKEND=d3d11-warp"));
        QCOMPARE(software("d3d11-warp"),usual);
        QVERIFY(qgetenv("QTWEBENGINE_CHROMIUM_FLAGS").isEmpty()); // a hardware backend leaves Chromium alone
#endif
    }
    void displaySettingsRoute() {
        // The checked menu item is not a MAP property. An omitted MAP mode
        // still resolves to Normal; exercise ingestion and command routing.
        SkinWindow fresh;
        std::string unset=displaySkin;
        for (const std::string line : {"        zoom = 2\n", "        zoom-mode = normal\n", "        letterbox = true\n"})
            unset.erase(unset.find(line),line.size());
        QVERIFY(fresh.loadSkin(unset).isEmpty()); wireDisplayCommands(fresh);
        auto* defaultMap=dynamic_cast<MapWidget*>(fresh.map()); QVERIFY(defaultMap);
        QCOMPARE(defaultMap->presentationSettings(),MapPresentationSettings{});
        displayAction(fresh,"&Blur")->trigger();
        QCOMPARE(defaultMap->presentationSettings().mode,MapPresentationSettings::Blur);
        displayAction(fresh,"&Normal")->trigger();
        QCOMPARE(defaultMap->presentationSettings(),MapPresentationSettings{});
        SkinWindow window;
        window.setMapViewSize(3,3);
        QVERIFY(window.loadSkin(displaySkin).isEmpty()); wireDisplayCommands(window);
        auto* map = dynamic_cast<MapWidget*>(window.map()); QVERIFY(map);
        QCOMPARE(map->presentationSettings().zoom, 2.0);
        window.live()->winset("mapwindow.secondary","is-visible=true"); window.syncFromLive();
        MapWidget* secondary=nullptr;
        for (auto* child : window.findChildren<QWidget*>())
            if (auto* other=dynamic_cast<MapWidget*>(child); other && other!=map) secondary=other;
        QVERIFY(secondary);
        QCOMPARE(secondary->presentationSettings().iconSize,16.0);
        for (const auto& name : {"&Distort", "&Normal", "&Blur"}) {
            auto* action = displayAction(window, name); QVERIFY(action); action->trigger();
            QCOMPARE(int(map->presentationSettings().mode), name == QString("&Blur") ? 2 : name == QString("&Normal") ? 1 : 0);
        }
        QCOMPARE(secondary->presentationSettings().mode,MapPresentationSettings::Distort);
        QCOMPARE(secondary->presentationSettings().iconSize,16.0);
        window.live()->winset("mapwindow.map", "icon-size=48"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().zoom, 0.0); // overrides the skin's positive zoom
        QCOMPARE(map->presentationSettings().iconSize, 48.0);
        window.live()->winset("mapwindow.map", "zoom=1.5;letterbox=false"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().iconSize, 0.0);
        QCOMPARE(map->presentationSettings().zoom, 1.5); QVERIFY(!map->presentationSettings().letterbox);
        window.live()->winset("mapwindow.map", "icon-size=0"); window.syncFromLive();
        QCOMPARE(map->presentationSettings().zoom, 0.0);
        QCOMPARE(map->presentationSettings().iconSize, 0.0);
    }
    void dimensions() {
        MapWidget map;
        QCOMPARE(map.viewSize(), QSize(0, 0));
        map.setViewSize(QSize(19, 13));
        QCOMPARE(map.viewSize(), QSize(19, 13));
        map.setViewSize(QSize(0, 13));
        QCOMPARE(map.viewSize(), QSize(0, 0));
    }
    void metricsOverlay() {
        MapWidget map;
        map.resize(640, 480);
        QVERIFY(!map.metricsVisible());
        map.setMetricsVisible(true);
        auto* label = map.findChild<QLabel*>("mapPerformanceOverlay");
        QVERIFY(label);
        QVERIFY(map.metricsVisible());
        QVERIFY(label->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(label->focusPolicy(), Qt::NoFocus);
        QVERIFY(label->text().contains("Target"));
        QVERIFY(label->text().contains("Update latency"));
        QVERIFY(label->text().contains("Upload"));
        QVERIFY(label->text().contains("GUI queue"));
        QVERIFY(label->text().contains("Render wait"));
        QVERIFY(label->text().contains("Render to submit"));
        QVERIFY(label->text().contains("Command recording"));
        QVERIFY(label->text().contains("Qt submission wait"));
        QVERIFY(label->text().contains("Display"));
        QVERIFY(label->text().contains("TIME (ms)"));
        QVERIFY(label->text().contains("AVG"));
        QVERIFY(label->text().contains("P95"));
        QVERIFY(!label->text().contains("(reported)"));
        QVERIFY(!label->text().contains("/ p95"));
        QCOMPARE(label->textFormat(),Qt::RichText);
        QVERIFY(label->text().contains("\nCOMPOSITION\n"));
        QVERIFY(label->text().contains("Generated passes"));
        QVERIFY(label->text().contains("Storage pixels"));
        QVERIFY(label->text().contains("-- / 4096"));
        const auto lines = label->text().split('\n');
        QCOMPARE(lines.at(4).indexOf("AVG") + 3, lines.at(5).indexOf("--") + 2);
        QVERIFY(label->text().contains("Pending"));
        QVERIFY(map.rect().contains(label->geometry()));
        map.setMetricsVisible(false);
        QVERIFY(label->isHidden());
    }
    void contextMenuSelection() {
        SkinWindow window;
        QSignalSpy picked(&window, &SkinWindow::mapMenuPicked);
        MapMenu menu;
        menu.token = 42; menu.global = QPoint(20, 20);
        QImage picture(16, 16, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::red);
        menu.items = {{0, "Floor & tile", "Examine"}, {1, "Object", "Use", picture}};
        window.showMapMenu(menu);
        auto* popup = window.findChild<QMenu*>("mapContextMenu");
        QVERIFY(popup);
        QCOMPARE(popup->actions().size(), 2);
        // The atom's picture beside its name, none for an atom without; the system's menu font, not the menu bar's.
        QCOMPARE(popup->actions().at(0)->text(), QString("Floor && tile"));
        QVERIFY(popup->actions().at(0)->icon().isNull());
        QVERIFY(!popup->actions().at(1)->icon().isNull());
        QVERIFY(!popup->testAttribute(Qt::WA_SetFont));
        auto* group = popup->actions().at(1)->menu();
        QVERIFY(group);
        group->actions().at(0)->trigger();
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.at(0).at(0).toULongLong(), quint64(42));
        QCOMPARE(picked.at(0).at(1).toInt(), 1);
        window.showMapMenu({});
        QVERIFY(!popup->isVisible());
    }
    void metricsMenu() {
        SkinWindow window;
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* action = window.findChild<QAction*>("mapPerformanceAction");
        QVERIFY(action);
        auto* map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QVERIFY(action->isCheckable());
        QVERIFY(!action->isChecked());
        QSignalSpy commands(&window, &SkinWindow::menuCommand);
        action->trigger();
        QVERIFY(action->isChecked());
        QVERIFY(map->metricsVisible());
        QCOMPARE(commands.count(), 0);
        QVERIFY(window.loadSkin(skin).isEmpty());
        map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map && map->metricsVisible());
        window.setMapMetricsVisible(false);
        QVERIFY(!action->isChecked());
        QVERIFY(!map->metricsVisible());
    }
    void skinRebuild() {
        SkinWindow window;
        window.setMapViewSize(19, 13); // bounds may precede the skin
        window.setMapMetricsVisible(true);
        QVERIFY(window.loadSkin(skin).isEmpty());
        auto* map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QCOMPARE(map->viewSize(), QSize(19, 13));
        window.setMapViewSize(11, 17);
        QCOMPARE(map->viewSize(), QSize(11, 17));
        QVERIFY(window.loadSkin(skin).isEmpty());
        map = dynamic_cast<MapWidget*>(window.map());
        QVERIFY(map);
        QCOMPARE(map->viewSize(), QSize(11, 17));
        QVERIFY(map->metricsVisible()); // toggle survives skin reconstruction
        window.setMapViewSize(0, 0); // reconnect clears old bounds
        QCOMPARE(map->viewSize(), QSize(0, 0));
    }
};
int FUGUE_TEST_ENTRY(int argc, char** argv) {
    registerSchemes();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    MapTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "map_test.moc"

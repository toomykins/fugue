// Desktop pixel checks require the test windows to stay foreground and unobstructed.
#include "tests/appearancejson.h"
#include <QtTest>
#include "tests/testmain.h"
#include <QBuffer>
#include <QCursor>
#include <QMouseEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QStyleFactory>
#include <bit>
#include <memory>
#include "shell/browsehost.h"
#include "shell/browserpane.h"
#include "shell/entitymap.h"
#include "shell/mapwidget.h"
#include "shell/skinwindow.h"
#ifndef Q_MOC_RUN
#include "shell/tests/nativedesktopfixture.h"
#include "shell/tests/backendcheck.h"
#endif
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

class MapDesktopTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const auto platform=QGuiApplication::platformName();
        QVERIFY2(platform!="offscreen" && platform!="minimal",
                 "Desktop MapWidget checks require a foreground, unobstructed desktop with capture support");
        const auto mismatch=test::backendMismatch(); QVERIFY2(mismatch.isEmpty(),qPrintable(mismatch));
    }
    void desktopCaptureRejectsObstruction() {
        MapWidget map; map.resize(320,240); map.setViewSize({1,1});
        auto mailbox=std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QImage green(32,32,QImage::Format_RGBA8888); green.fill(Qt::green);
        mailbox->publish(green,green.rect(),0,0,{{0,0,1,1},{},32,0,1});
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        map.show(); QVERIFY(QTest::qWaitForWindowExposed(&map));
        map.raise(); map.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&map));
        const QPoint local(160,120),sample=map.mapToGlobal(local);
        FUGUE_OWN_DESKTOP_POINT(map,sample);
        map.requestMapFrame(); QTRY_VERIFY(!submitted.empty());
        nativedesktopfixture::Capture capture(map,map,{local},[&]{return submitted.size();});
        auto color=[&]{const auto pixels=capture.grab();return capture.color(pixels,local);};
        QTRY_COMPARE(color(),QColor(Qt::green));

        // A nonactivating owned cover leaves Qt AND native foreground on MAP.
        // Submission/activation alone therefore cannot validate a desktop pixel.
        QWidget cover(nullptr,Qt::Tool|Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);
        cover.setAttribute(Qt::WA_ShowWithoutActivating);
        cover.setStyleSheet("background:#ef5fff");
        cover.setGeometry(QRect(sample-QPoint(40,40),QSize(80,80)));
        cover.show(); QVERIFY(QTest::qWaitForWindowExposed(&cover));
        QTRY_COMPARE(QGuiApplication::topLevelAt(sample),cover.windowHandle());
        QCOMPARE(QApplication::activeWindow(),&map);
        QVERIFY(!nativedesktopfixture::ownsPoint(map,sample));
        capture.grab(); QVERIFY(!capture.valid()); QVERIFY(!color().isValid());
        cover.hide();
        QTRY_VERIFY(nativedesktopfixture::ownsPoint(map,sample));
        QVERIFY(!color().isValid()); // never recover a contaminated observation
        nativedesktopfixture::Capture fresh(map,map,{local},[&]{return submitted.size();});
        auto freshColor=[&]{const auto pixels=fresh.grab();return fresh.color(pixels,local);};
        QTRY_COMPARE(freshColor(),QColor(Qt::green));
    }
    void disabledBrowserAboveMap() {
        // The browser's graphics API is the process's, as the map's is (shell/gpubackend.h): one backend per entry.
        QVector<MapPointer> events;
        SkinWindow window; window.resize(640,480); QVERIFY(window.loadSkin(skin).isEmpty());
        auto* map=dynamic_cast<MapWidget*>(window.controlWidget("main.map")); QVERIFY(map);
        window.setMapViewSize(2,2);
        auto metrics=std::make_shared<MapMetrics>(); auto mailbox=std::make_shared<MapMailbox>(metrics);
        map->setMailbox(mailbox);
        QImage green(64,64,QImage::Format_RGBA8888); green.fill(Qt::green);
        MapSprite sprite; sprite.image=green; sprite.kind=MapSprite::Screen; sprite.atom=42; sprite.haveKey=true;
        sprite.key=7; sprite.screenInteractive=true; sprite.iconBottom={0,64};
        MapGeometry geometry{QRect(0,0,2,2),QSize(10,10),32,0,9};
        mailbox->publish({},QRect(0,0,64,64),0,0,geometry,{sprite});
        map->setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        QSignalSpy submissions(map,&QRhiWidget::frameSubmitted);
        QPoint diagnosticSample(90,90);
        const auto diagnostic=qScopeGuard([&] {
            if(QTest::currentTestFailed()) nativedesktopfixture::diagnose(window,*map,diagnosticSample,submissions.size());
        });
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        // Activate after native exposure; activation alone is not proof that
        // the desktop compositor's sample belongs to this window.
        window.raise(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        map->requestMapFrame();
        QTRY_VERIFY_WITH_TIMEOUT(!submissions.empty(),5000);
        map->setFocus();
        auto live=window.live();
        live->winset("floating","parent=main;type=browser;size=1x1;is-visible=false;is-disabled=true");
        window.showBrowse("floating","window=floating",R"(<style>html,body{margin:0;background:rgb(255,0,0)}</style><b>Overlay</b><script>
          window.overlay={show(){BYOND.winset('floating',{isVisible:true,pos:(60*devicePixelRatio)+','+(60*devicePixelRatio),size:(100*devicePixelRatio)+'x'+(100*devicePixelRatio)})},hide(){BYOND.winset('floating',{isVisible:false})}};
        </script>)");
        auto* pane=window.browseHost()->paneFor("floating"); QVERIFY(pane);
        QTRY_VERIFY_WITH_TIMEOUT(pane->ready(),8000); QVERIFY(pane->isHidden());
        window.pageCall("floating","overlay.show",{});
        QTRY_VERIFY_WITH_TIMEOUT(pane->isVisible() && pane->size()==QSize(100,100),5000);
        const auto sample=pane->mapToGlobal(QPoint(30,30));
        diagnosticSample=map->mapFromGlobal(sample);
        FUGUE_OWN_DESKTOP_POINT(window,sample);
        QTRY_COMPARE(QApplication::focusWidget(),map);
        QCOMPARE(QApplication::widgetAt(sample),map); // disabled overlay is pointer transparent
        // Capture the desktop compositor, not HWND GetDC (which can return a
        // stale redirected surface on Windows), JS, or a forced MAP readback.
        nativedesktopfixture::Capture desktop(window,*map,{diagnosticSample},[&]{return submissions.size();});
        auto color=[&] { const auto pixels=desktop.grab(); return desktop.color(pixels,diagnosticSample); };
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::red),5000);
        QTest::mouseClick(QApplication::widgetAt(sample),Qt::LeftButton,Qt::NoModifier,map->mapFromGlobal(sample));
        QVERIFY(!events.empty()); QCOMPARE(events.back().click.over.ref,42u);
        // A newer pending frame cannot change this hover hit (HUD -> image).
        auto next=sprite; next.kind=MapSprite::Image; next.imageId=3; next.imageIdentity=20; next.targetIdentity=30; next.key=8;
        mailbox->publish({},QRect(0,0,64,64),0,0,geometry,{next});
        QMouseEvent motion(QEvent::MouseMove,map->mapFromGlobal(sample),sample,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QCursor::setPos(sample); QApplication::sendEvent(map,&motion);
        QVERIFY(events.back().screen); QCOMPARE(events.back().atomKey,uint16_t(7));
        map->requestMapFrame();
        QTRY_VERIFY_WITH_TIMEOUT(!events.empty() && events.back().imageIdentity==20,5000); // stationary re-pick after render
        window.pageCall("floating","overlay.hide",{}); QTRY_VERIFY(pane->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::green),5000);
        const auto loads=window.browseHost()->origin()->stats().pages;
        window.pageCall("floating","overlay.show",{}); QTRY_VERIFY(pane->isVisible());
        QCOMPARE(window.browseHost()->origin()->stats().pages,loads);
        // TG-style CSS alpha must blend with the live MAP, not an opaque page
        // backing store. Observe actual desktop pixels on both native backends.
        pane->page()->runJavaScript("document.documentElement.style.background='transparent';"
                                   "document.body.style.cssText='margin:0;height:100vh;background:rgba(0,0,0,0.65)'");
        auto near = [](QColor actual, QColor expected) {
            return std::abs(actual.red()-expected.red()) <= 2 &&
                std::abs(actual.green()-expected.green()) <= 2 &&
                std::abs(actual.blue()-expected.blue()) <= 2;
        };
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(89,89,89)),5000); // ordinary opaque browser
        auto changed=live->winset("floating","inner-background-color=transparent;is-disabled=false");
        QVERIFY(changed.unhandled.empty()); window.syncFromLive();
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(0,89,0)),5000);
        auto* hit=QApplication::widgetAt(sample);
        QVERIFY(hit && (hit==pane || pane->isAncestorOf(hit))); // enabled menu still takes input
        window.pageCall("floating","overlay.hide",{}); QTRY_VERIFY(pane->isHidden());
        QTRY_COMPARE_WITH_TIMEOUT(color(),QColor(Qt::green),5000);
        window.pageCall("floating","overlay.show",{}); QTRY_VERIFY(pane->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(0,89,0)),5000);
        QCOMPARE(window.browseHost()->origin()->stats().pages,loads);
        live->winset("floating","inner-background-color=none"); window.syncFromLive();
        QTRY_VERIFY_WITH_TIMEOUT(near(color(), QColor(89,89,89)),5000);
        QPointer<BrowserPane> old=pane; live->clearRuntimeBrowsers(); window.syncFromLive(); QVERIFY(old.isNull());
        QVERIFY(!window.browseHost()->paneFor("floating"));
        QVERIFY(desktop.valid());
        map->setInputHandler("main.map",true,{}); // callback does not outlive the test's vector
    }
    void screenOffsetRangePixels() {
        MapWidget map;
        map.resize(480,224); map.setViewSize({15,7});
        auto mailbox=std::make_shared<MapMailbox>(std::make_shared<MapMetrics>()); map.setMailbox(mailbox);
        QVector<MapPointer> events;
        map.setInputHandler("main.map",true,[&](const MapPointer& p){events.push_back(p);});
        fugue::model::World w; w.bounds={true,20,20,0,15,7,15,7};
        w.resync.haveOrigin=true; w.resync.originX=w.resync.originY=1; w.resources={{123,0},{456,0}};
        w.appearances[1].fields.iconResID=0; w.appearances[1].fields.layerBits=std::bit_cast<uint32_t>(1.f);
        setFields(w.appearances[1], R"({"screenLoc":"4:16,2:16 to 10:16,2:16"})");
        w.appearances[2].fields.iconResID=1; w.appearances[2].fields.layerBits=std::bit_cast<uint32_t>(2.f);
        setFields(w.appearances[2], R"({"screenLoc":"4:16,2:16"})");
        w.hudAppid={{42,1},{43,2}}; w.hudIdentity={{42,{2,99}},{43,{2,100}}};
        auto read=[](uint32_t id,QByteArray& out){
            QImage image(32,32,QImage::Format_RGBA8888); image.fill(id==123?Qt::blue:Qt::transparent);
            if(id==456) for(int y=10;y<22;++y) for(int x=10;x<22;++x) image.setPixelColor(x,y,Qt::green);
            QBuffer buffer(&out); buffer.open(QIODevice::WriteOnly); return image.save(&buffer,"PNG");
        };
        TurfMap terrain; EntityMap scene; auto frame=terrain.prepare(w,{},0,false);
        QSignalSpy submitted(&map,&QRhiWidget::frameSubmitted);
        const auto diagnostic=qScopeGuard([&] {
            if(QTest::currentTestFailed()) nativedesktopfixture::diagnose(map,map,{128,160},submitted.size());
        });
        auto publish=[&]{
            auto sprites=scene.prepare(w,frame,read,1,nullptr,true);
            mailbox->publish(frame.image,frame.source,0,0,frame.geometry,sprites);
            map.requestMapFrame();
        };
        map.show();
        QVERIFY(QTest::qWaitForWindowExposed(&map));
        map.raise(); map.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&map));
        QList<QPoint> samples{{128,160},{108,172},{340,172}};
        for(int x=116;x<=308;x+=32) samples.push_back({x,172});
        for(const auto& point:samples)
            FUGUE_OWN_DESKTOP_POINT(map,map.mapToGlobal(point));
        // Observe the desktop after normal submission, never grabFramebuffer().
        // Poll a fresh capture for readiness, then check all pixels in that same
        // desktop snapshot instead of recapturing the entire screen per pixel.
        nativedesktopfixture::Capture capture(map,map,samples,[&]{return submitted.size();});
        auto desktop=[&]{return capture.grab();};
        auto color=[&](const QImage& image,QPoint point){return capture.color(image,point);};
        for(bool occupied:{false,true}) {
            w.hudOrder={42}; w.hud={{42,fugue::proto::Hud::Record{}}};
            if(occupied) {w.hudOrder.push_back(43);w.hud[43]={};}
            for(int reopen=0;reopen<2;++reopen) {
                const int before=submitted.size(); publish();
                QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>before,5000);
                QImage pixels;
                QTRY_COMPARE_WITH_TIMEOUT(color(pixels=desktop(),{128,160}),QColor(occupied?Qt::green:Qt::blue),5000);
                for(int x=116;x<=308;x+=32) QCOMPARE(color(pixels,{x,172}),QColor(Qt::blue));
                QCOMPARE(color(pixels,{108,172}),QColor(Qt::black)); QCOMPARE(color(pixels,{340,172}),QColor(Qt::black));
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{128,160});
                QVERIFY(!events.empty()); QVERIFY(events.back().screen);
                QCOMPARE(events.back().click.over.ref,occupied?43u:42u);
                QVERIFY(w.hudObjectCurrent(events.back().click.over.ref,events.back().atomKey));
                // Removal invalidates worker-side input before a new frame is painted.
                w.hudOrder.clear(); w.hud.clear();
                QVERIFY(!w.hudObjectCurrent(events.back().click.over.ref,events.back().atomKey));
                const int closing=submitted.size(); publish();
                QTRY_VERIFY_WITH_TIMEOUT(submitted.size()>closing,5000);
                QTRY_COMPARE_WITH_TIMEOUT(color(pixels=desktop(),{128,160}),QColor(Qt::black),5000);
                QCOMPARE(color(pixels,{308,172}),QColor(Qt::black));
                QTest::mouseClick(&map,Qt::LeftButton,Qt::NoModifier,{128,160});
                QVERIFY(!events.back().screen); QCOMPARE(events.back().click.over.kind,fugue::proto::enc::RefTurf);
                w.hudOrder={42};w.hud={{42,fugue::proto::Hud::Record{}}};
                if(occupied) {w.hudOrder.push_back(43);w.hud[43]={};}
            }
        }
        QVERIFY(capture.valid());
        map.setInputHandler("main.map",true,{});
    }
};
int FUGUE_TEST_ENTRY(int argc, char** argv) {
    registerSchemes();
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    MapDesktopTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "mapdesktop_test.moc"

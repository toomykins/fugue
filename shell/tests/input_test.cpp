// Shell input wiring (offscreen QPA): the map widget turns Qt mouse events into MapPointer values hit-tested against the scene the frame was drawn from;
// a key no macro binds, an arrow included, sends nothing; routeCommand classifies a macro or typed command. Mouse frame sequences are in
// tests/mousechain_test.cpp.
#include <QApplication>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QtTest>
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/inputrouter.h"
#include "shell/screenshot.h"
#include <QTemporaryDir>
#include "shell/macrodialog.h"
#include <QCheckBox>
#include <QMenuBar>
#include <QToolButton>
#include <QTreeWidget>
#include "net/input.h"
#include "shell/mapwidget.h"
#include "shell/skinfont.h"
#include "shell/skinwindow.h"
#include <QLabel>

using namespace fugue::shell;
using namespace fugue::render;

static const char* kSkin = R"(macro "m"
	elem "q"
		name = "Q"
		command = "qmacro"
	elem
		name = "WEST"
		command = "MoveKey 8 1"
	elem
		name = "WEST+UP"
		command = "MoveKey 8 0"
	elem
		name = "NORTH"
		command = "MoveKey 1 1"
	elem
		name = "NORTH+UP"
		command = "MoveKey 1 0"
	elem
		name = "SOUTH"
		command = "lg south"
window "main"
	elem "main"
		type = MAIN
		size = 560x560
		is-default = true
		macro = "m"
	elem "map"
		type = MAP
		pos = 0,0
		size = 480x480
		anchor1 = 0,0
		anchor2 = 0,0
		icon-size = 32
		right-click = true
	elem "input"
		type = INPUT
		pos = 0,500
		size = 560x24
		anchor1 = 0,100
		anchor2 = 100,100
)";

static HitPtr oneAtomScene() {
    auto s = std::make_shared<HitScene>();
    s->cols = s->rows = 15;
    s->originX = s->originY = 1;
    s->atoms.push_back({"thing", 1, kAtomObj, 9, 3, 3, false});
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    s->world.push_back({sp, 2 * 32, (14 - 2) * 32, 0});  // cell (3,3)
    return s;
}

// oneAtomScene, with "thing" declaring a hover proc (appearance flag 0x800).
static HitPtr hoverScene() {
    auto s = std::make_shared<HitScene>(*oneAtomScene());
    s->atoms[0].hover = true;
    s->anyHover = true;
    return s;
}

// The default map and a second .dmf MAP, side by side.
static const char* kTwoMaps = R"(window "main"
	elem "main"
		type = MAIN
		size = 800x480
		is-default = true
	elem "map"
		type = MAP
		pos = 0,0
		size = 480x480
		icon-size = 32
	elem "map2"
		type = MAP
		pos = 500,0
		size = 192x192
		icon-size = 0
)";

// A secondary map's scene: hud2 (ref 4, a screen object declaring hover) in cell 1,1 of a 3x3 box.
static HitPtr map2Scene() {
    auto s = std::make_shared<HitScene>();
    s->secondary = true;
    s->cols = s->rows = 3;
    HitAtom a{"hud2", 1, kAtomObj, 4, 0, 0, true};
    a.hover = true;
    s->atoms.push_back(a);
    auto sp = std::make_shared<Sprite>();
    sp->w = sp->h = 32;
    sp->px.assign(32 * 32 * 4, 255);
    s->world.push_back({sp, 0, 64, 0});
    s->anyHover = true;
    return s;
}

struct KeySeen : QObject {
    QList<int> keys;
    bool eventFilter(QObject*, QEvent* e) override {
        if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) keys << static_cast<QKeyEvent*>(e)->key();
        return false;
    }
};

class InputTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
    FocusFilter* filt = nullptr;
private slots:
    void initTestCase() {
        qRegisterMetaType<MapPointer>();
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        filt = new FocusFilter;
        auto t = win->macros();
        filt->setMacros(t.sets.at(t.active));
        qApp->installEventFilter(filt);
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
    }
    void cleanupTestCase() {
        qApp->removeEventFilter(filt);
        delete filt;
        delete win;
    }

    void skinRightClickParamReachesTheWidget() { QVERIFY(win->map()->rightClickToWire()); }

    void pressMoveReleaseBecomePointerValues() {
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        // cell (3,3): image x 64..95, y from the top 12*32.. => (80, 400)
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseMove(win->map(), QPoint(120, 400));
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::ShiftModifier, QPoint(120, 400));
        QCOMPARE(spy.count(), 3);
        auto p0 = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p0.kind), int(MapPointer::Press));
        QCOMPARE(p0.ev.imgX, 80);
        QCOMPARE(p0.ev.imgY, 400);
        QCOMPARE(p0.ev.button, 1);
        QVERIFY(p0.scene);
        Naming n;
        QVERIFY(nameMouse(*p0.scene, p0.ev.imgX, p0.ev.imgY, n));  // the scene travels with the event
        QCOMPARE(n.overRef, 9u);
        QCOMPARE(int(spy.at(1).at(0).value<MapPointer>().kind), int(MapPointer::Move));
        auto p2 = spy.at(2).at(0).value<MapPointer>();
        QCOMPARE(int(p2.kind), int(MapPointer::Release));
        QVERIFY(p2.ev.mods.shift);
    }
    void rightClickNotDeclaredIsLocal() {
        win->map()->setRightClickToWire(false);
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QSignalSpy menu(win->map(), &MapWidget::localMenuRequested);
        QTest::mouseClick(win->map(), Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.at(0).at(1).toString(), QString("thing"));
        win->map()->setRightClickToWire(true);
        QTest::mouseClick(win->map(), Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 2);  // press + release, on the wire path
        QCOMPARE(spy.at(0).at(0).value<MapPointer>().ev.button, 3);
    }
    // client.show_popup_menus, as stock DreamDaemon 516 sends it: the whole 0x003b again, FormatFlags bit 28 set while it is
    // FALSE ("Fugue Popup" world, a verb toggling it). Stock DreamSeeker then shows no menu and sends the right click as
    // MouseDown/MouseUp/Click with right=1, on a map whose right-click is false.
    void popupMenusOffSendsTheRightClick() {
        auto body = [](const char* hex) {
            QByteArray b = QByteArray::fromHex(hex);
            return fugue::Bytes(b.begin(), b.end());
        };
        const fugue::Bytes on =
            body("010001010001000007000000042160000402ea01000000436f6d6d616e647300ff9406000004020000");
        const fugue::Bytes off =
            body("010001010001000007000000042160100402ea01000000436f6d6d616e647300ff9406000004020000");
        fugue::net::InputModel im;
        QVERIFY(im.showPopupMenus());  // the default, before any 0x003b
        im.onFrame(fugue::S2C::MouseSubs, off);
        QVERIFY(!im.showPopupMenus());
        QVERIFY(im.hooks().click);  // the click subscription is unchanged
        im.onFrame(fugue::S2C::MouseSubs, on);
        QVERIFY(im.showPopupMenus());

        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(false);
        QVERIFY(m->rightClickToWire());
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QTest::mouseClick(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 0);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).value<MapPointer>().ev.button, 3);
        m->setPopupMenus(true);
        QTest::mouseClick(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(spy.count(), 2);
        m->setRightClickToWire(true);
    }
    // The right button's two reads of client.show_popup_menus, measured on stock DreamSeeker 516 (GAME-CLIENT-SERVER-516.md
    // 9.1.1): the press sends MouseDown only with the flag FALSE, and the release decides the rest.
    void rightPressWithTheFlagOnWaitsForTheRelease() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(true);
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QSignalSpy menuAt(m, &MapWidget::localMenuPointer);
        QTest::mousePress(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 0);  // not at the press
        QCOMPARE(menuAt.count(), 0);
        QCOMPARE(spy.count(), 0);
        QTest::mouseRelease(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menuAt.count(), 1);
        QCOMPARE(spy.count(), 0);
    }
    // The flag went FALSE while the button was down (the pointer moved onto vg's camera static): no menu, and the release
    // reaches the wire as MouseUp and Click without a MouseDown.
    void flagOffByTheReleaseSendsTheClickWithoutAPress() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(true);
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QTest::mousePress(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        m->setPopupMenus(false);
        QTest::mouseRelease(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 0);
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Release));
        QVERIFY(p.pressUnsent);
        QCOMPARE(p.ev.button, 3);
        QCOMPARE(p.ev.imgX, 80);
        QCOMPARE(p.ev.imgY, 400);
        m->setPopupMenus(true);
    }
    // A press sent with the flag FALSE finishes as a click whatever the flag has become, and opens no menu.
    void sentPressFinishesAsAClickWhenTheFlagComesBack() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(false);
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QTest::mousePress(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        m->setPopupMenus(true);
        QTest::mouseRelease(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(menu.count(), 0);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(int(spy.at(0).at(0).value<MapPointer>().kind), int(MapPointer::Press));
        auto p = spy.at(1).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Release));
        QVERIFY(!p.pressUnsent);
    }
    // A right press outside the open verb menu closes it and is consumed (stock sends no MouseDown for it); its release then
    // acts as a right release of its own. Qt's popup grab swallows both, so the release is delivered to a window that is not
    // the map here, as it is on X11.
    QMenu* openVerbMenu() {
        auto* menu = new QMenu(win);
        menu->addAction("poke");
        MapWidget::watchPopup(menu);
        menu->popup(win->map()->mapToGlobal(QPoint(300, 100)));
        QTest::qWaitForWindowExposed(menu);
        return menu;
    }
    void rightPressOutsideTheMenuClosesItAndItsReleaseOpensANewOne() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(true);
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QMenu* old = openVerbMenu();
        const QPoint g = m->mapToGlobal(QPoint(80, 400));
        QTest::mousePress(old, Qt::RightButton, Qt::NoModifier, old->mapFromGlobal(g));
        QVERIFY(!old->isVisible());
        QCOMPARE(spy.count(), 0);   // consumed: no MouseDown
        QCOMPARE(menu.count(), 0);  // and nothing opens at the press
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, win->mapFromGlobal(g));
        QCOMPARE(menu.count(), 1);
        QCOMPARE(spy.count(), 0);
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, win->mapFromGlobal(g));  // taken once
        QCOMPARE(menu.count(), 1);
        delete old;
    }
    void rightPressOutsideTheMenuWithTheFlagOffSendsUpAndClick() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(true);
        QSignalSpy spy(m, &MapWidget::pointer);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QMenu* old = openVerbMenu();
        const QPoint g = m->mapToGlobal(QPoint(80, 400));
        QTest::mousePress(old, Qt::RightButton, Qt::NoModifier, old->mapFromGlobal(g));
        m->setPopupMenus(false);  // the hover the close reports turned it off
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, win->mapFromGlobal(g));
        QCOMPARE(menu.count(), 0);
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Release));
        QVERIFY(p.pressUnsent);
        QCOMPARE(p.ev.imgX, 80);
        QCOMPARE(p.ev.imgY, 400);
        m->setPopupMenus(true);
        delete old;
    }
    // Where the platform replays the closing press to the map, it is the same consumed press and its own release.
    void replayedClosingPressIsConsumedToo() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(false);  // already off at the press: a sent press would be a MouseDown
        QSignalSpy spy(m, &MapWidget::pointer);
        QMenu* old = openVerbMenu();
        QTest::mousePress(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 0);
        QTest::mouseRelease(m, Qt::RightButton, Qt::NoModifier, QPoint(80, 400));
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).value<MapPointer>().pressUnsent);
        m->setPopupMenus(true);
        delete old;
    }
    // A press inside the menu, or the left button outside it, is the menu's own business.
    void pressesTheMenuKeepsAreNotConsumed() {
        MapWidget* m = win->map();
        m->setRightClickToWire(false);
        m->setPopupMenus(true);
        QSignalSpy menu(m, &MapWidget::localMenuRequested);
        QMenu* old = openVerbMenu();
        QTest::mousePress(old, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
        QTest::mouseRelease(old, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, win->mapFromGlobal(m->mapToGlobal(QPoint(80, 400))));
        QCOMPARE(menu.count(), 0);
        old->hide();
        old = openVerbMenu();
        QTest::mousePress(old, Qt::LeftButton, Qt::NoModifier, old->mapFromGlobal(m->mapToGlobal(QPoint(80, 400))));
        QVERIFY(!old->isVisible());
        QTest::mouseRelease(win, Qt::LeftButton, Qt::NoModifier, win->mapFromGlobal(m->mapToGlobal(QPoint(80, 400))));
        QTest::mouseRelease(win, Qt::RightButton, Qt::NoModifier, win->mapFromGlobal(m->mapToGlobal(QPoint(80, 400))));
        QCOMPARE(menu.count(), 0);
        delete old;
    }
    // The verb popup swallows the pointer's motion; when it closes, the atom under the pointer is reported at once (with
    // right=1 while the button is still down), so the flag follows what is under the pointer and not what was under it
    // when the popup opened.
    void popupCloseReportsTheHover() {
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, hoverScene());
        MapWidget* m = win->map();
        QEvent leave0(QEvent::Leave);
        QCoreApplication::sendEvent(m, &leave0);
        QCoreApplication::processEvents();
        QSignalSpy spy(m, &MapWidget::pointer);
        m->reportHoverAfterPopup(m->mapToGlobal(QPoint(81, 401)), false);
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Hover));
        QVERIFY(!p.motion);
        QCOMPARE(p.ev.button, 0);
        QCOMPARE(p.ev.imgX, 81);
        QCOMPARE(p.ev.imgY, 401);
        m->reportHoverAfterPopup(m->mapToGlobal(QPoint(81, 401)), true);  // the same pixel again: still reported
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).value<MapPointer>().ev.button, 3);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(m, &leave);
        QCoreApplication::processEvents();
        win->setMapFrame(f, oneAtomScene());
        QTest::mouseMove(m, QPoint(70, 401));  // the post after the last hover scene
    }
    // DreamSeeker's status bar names the atom under the pointer (DM Reference, `statusbar`), with no button held.
    // See MapWidget::updateHover for what it names and the one way it differs from stock.
    void statusBarNamesTheAtomUnderThePointer() {
        QLabel* bar = win->statusBar();
        QVERIFY(bar && !bar->isHidden());
        auto s = std::make_shared<HitScene>(*oneAtomScene());
        auto sp = s->world[0].sprite;
        s->atoms.push_back({"ghost", 0, kAtomObj, 10, 5, 5, false});  // mouse_opacity 0
        s->world.push_back({sp, 4 * 32, (14 - 4) * 32, 1});           // cell (5,5)
        s->atoms.push_back({"", 1, kAtomObj, 11, 7, 7, false});       // name = ""
        s->world.push_back({sp, 6 * 32, (14 - 6) * 32, 2});           // cell (7,7)
        s->atoms.push_back({"space", 1, kAtomTurf, 0, 9, 9, false});  // vg's space marker, as the scene hands it over
        s->world.push_back({sp, 8 * 32, (14 - 8) * 32, 3});           // cell (9,9)
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, s);
        QSignalSpy wire(win->map(), &MapWidget::pointer);
        QTest::mouseMove(win->map(), QPoint(80, 400));  // over "thing"
        QCOMPARE(bar->text(), QString("thing"));
        QTest::mouseMove(win->map(), QPoint(10, 10));  // over no sprite at all
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(4 * 32 + 5, 10 * 32 + 5));  // mouse_opacity 0: nothing under it
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(6 * 32 + 5, 8 * 32 + 5));  // nameless atom: blank, not a label
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(8 * 32 + 5, 6 * 32 + 5));  // the marker is dropped upstream
        QCOMPARE(bar->text(), QString("space"));
        QCOMPARE(wire.count(), 0);  // nothing on this map declares a hover proc: no pointer is posted
        // a new frame re-resolves under a still pointer: the atom moved away
        win->setMapFrame(f, oneAtomScene());
        QCOMPARE(bar->text(), QString());
        QTest::mouseMove(win->map(), QPoint(80, 400));
        QCOMPARE(bar->text(), QString("thing"));
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);  // off the map: blank
        QCOMPARE(bar->text(), QString());
    }
    // Button-less motion is a Hover pointer (render::HoverTracker decides what reaches the wire), one per image pixel; a
    // new atom under a still pointer is a Hover with motion = false; leaving the control is a Leave. Nothing is a Hover
    // while a button is held. Every pointer names the map control.
    void hoverMotionBecomesHoverPointers() {
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, hoverScene());
        QEvent leave0(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave0);
        QCoreApplication::processEvents();
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QTest::mouseMove(win->map(), QPoint(81, 401));
        QTest::mouseMove(win->map(), QPoint(81, 401));
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Hover));
        QVERIFY(p.motion);
        QCOMPARE(p.control, std::string("main.map"));
        QCOMPARE(p.ev.imgX, 81);
        QCOMPARE(p.ev.imgY, 401);
        QCOMPARE(p.ev.button, 0);
        QVERIFY(p.scene);
        auto empty = std::make_shared<HitScene>(*hoverScene());
        empty->world.clear();
        win->setMapFrame(f, empty);  // the atom left the cell under the pointer
        QCOMPARE(spy.count(), 2);
        p = spy.at(1).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Hover));
        QVERIFY(!p.motion);
        win->setMapFrame(f, empty);  // nothing changed
        QCOMPARE(spy.count(), 2);
        win->setMapFrame(f, hoverScene());
        QCOMPARE(spy.count(), 3);
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseMove(win->map(), QPoint(120, 400));
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(120, 400));
        QCOMPARE(spy.count(), 6);
        for (int i = 3; i < 6; i++) {
            p = spy.at(i).at(0).value<MapPointer>();
            QVERIFY(p.kind != MapPointer::Hover);
            QCOMPARE(p.control, std::string("main.map"));
        }
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);
        QCOMPARE(spy.count(), 6);  // posted after the event that caused it (leaveGoesAfterTheNextMapsMotion)
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 7);
        p = spy.at(6).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Leave));
        QCOMPARE(p.control, std::string("main.map"));
        win->setMapFrame(f, oneAtomScene());
    }
    // Button-less motion is posted only while something could report it: a hover proc on this map's atoms or the
    // world's client hover proc. The post after the last such scene still goes, so the report ending a hover is sent.
    void hoverIsPostedOnlyWhenSomethingWantsIt() {
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
        QTest::mouseMove(win->map(), QPoint(70, 401));  // the post after an earlier test's hover scene
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QTest::mouseMove(win->map(), QPoint(81, 401));
        QTest::mouseMove(win->map(), QPoint(90, 401));
        QCOMPARE(spy.count(), 0);
        auto client = std::make_shared<HitScene>(*oneAtomScene());
        client->clientHover = true;
        win->setMapFrame(f, client);
        QTest::mouseMove(win->map(), QPoint(91, 401));
        QCOMPARE(spy.count(), 1);
        win->setMapFrame(f, oneAtomScene());
        QTest::mouseMove(win->map(), QPoint(92, 401));
        QTest::mouseMove(win->map(), QPoint(93, 401));
        QCOMPARE(spy.count(), 2);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);
        QCoreApplication::processEvents();
    }
    // A new frame under a still pointer is not re-checked while any button is down, a press the map kept off the wire
    // (the letterbox) included.
    void stillPointerRecheckWaitsForEveryButton() {
        win->map()->setIconSize(32);
        QImage small(160, 160, QImage::Format_RGBX8888);
        small.fill(Qt::black);
        auto one = std::make_shared<HitScene>(*hoverScene());
        one->cols = one->rows = 5;
        one->world[0].y = 2 * 32;
        win->setMapFrame(small, one);
        QRect ir = win->map()->imageRect();
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QTest::mouseMove(win->map(), ir.topLeft() + QPoint(2 * 32 + 5, 2 * 32 + 5));
        QCOMPARE(spy.count(), 1);
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));  // on the letterbox: no Press
        QCOMPARE(spy.count(), 1);
        QTest::mouseMove(win->map(), ir.topLeft() + QPoint(2 * 32 + 5, 2 * 32 + 5));
        auto gone = std::make_shared<HitScene>(*one);
        gone->world.clear();
        win->setMapFrame(small, gone);
        QCOMPARE(spy.count(), 1);
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::NoModifier, ir.topLeft() + QPoint(2 * 32 + 5, 2 * 32 + 5));
        win->setMapFrame(small, one);
        QCOMPARE(spy.count(), 2);
        win->map()->setIconSize(0);
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);
        QCoreApplication::processEvents();
    }
    // Stock sends nothing when its right-click menu opens over the map; a leave held for it goes when the menu closes
    // with the pointer elsewhere.
    void noLeaveForTheLocalMenu() {
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, hoverScene());
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        QMenu menu;
        menu.addAction("poke");
        menu.popup(win->map()->mapToGlobal(QPoint(80, 400)));
        QVERIFY(QApplication::activePopupWidget());
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(win->map(), &leave);
        QCoreApplication::processEvents();
        for (const auto& args : spy) QVERIFY(args.at(0).value<MapPointer>().kind != MapPointer::Leave);
        QCursor::setPos(win->mapToGlobal(QPoint(-50, -50)));
        menu.hide();
        QCoreApplication::processEvents();
        QVERIFY(!spy.isEmpty());
        QCOMPARE(int(spy.last().at(0).value<MapPointer>().kind), int(MapPointer::Leave));
        win->setMapFrame(f, oneAtomScene());
    }
    // A second .dmf MAP is a secondary map: built, named by its full id, fed by setAuxMapFrame with its scene, announced
    // by mapReady. Leaving one map for the other, the new map's report comes first and the old map's leave after it, as
    // stock sends them; a drag from one map onto the other names the map under the pointer.
    void secondDmfMapTakesInput() {
        win->hide();  // button-less test motion moves the cursor: only this window may be under it
        struct Reshow {
            SkinWindow* w;
            ~Reshow() {
                w->show();
                w->activateWindow();
            }
        } reshow{win};
        SkinWindow w;
        QSignalSpy ready(&w, &SkinWindow::mapReady);
        QVERIFY(w.loadSkin(kTwoMaps).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        MapWidget* m2 = w.auxMap("map2");
        QVERIFY(m2 && w.map() && m2 != w.map());
        QCOMPARE(m2->controlId(), std::string("main.map2"));
        QCOMPARE(w.mapWidgets().size(), 2);
        QCOMPARE(ready.count(), 2);
        QImage f(480, 480, QImage::Format_RGBX8888), f2(96, 96, QImage::Format_RGBX8888);  // map2 shows f2 at 2x
        f.fill(Qt::black);
        f2.fill(Qt::black);
        w.setMapFrame(f, hoverScene());
        w.setAuxMapFrame("map2", f2, map2Scene());
        QVERIFY(m2->hitScene());
        std::vector<MapPointer> seen;
        for (MapWidget* m : w.mapWidgets())
            connect(m, &MapWidget::pointer, &w, [&seen](const MapPointer& p) { seen.push_back(p); });
        QTest::mouseMove(w.map(), QPoint(80, 400));
        QCoreApplication::processEvents();
        seen.clear();
        QTest::mouseMove(m2, QPoint(32, 160));  // Qt: the old map's leave, then the new map's motion
        QCoreApplication::processEvents();
        QTest::mouseMove(m2, QPoint(33, 161));  // the same image pixel, but the leave ended the hover: posted
        QCOMPARE(int(seen.size()), 3);
        QCOMPARE(int(seen[0].kind), int(MapPointer::Hover));
        QCOMPARE(seen[0].control, std::string("main.map2"));
        QCOMPARE(int(seen[1].kind), int(MapPointer::Leave));
        QCOMPARE(seen[1].control, std::string("main.map"));
        QCOMPARE(int(seen[2].kind), int(MapPointer::Hover));
        QCOMPARE(seen[2].ev.imgX, seen[0].ev.imgX);
        QCOMPARE(seen[2].ev.imgY, seen[0].ev.imgY);
        QTest::mouseMove(m2, QPoint(32, 161));
        QCOMPARE(int(seen.size()), 3);
        seen.clear();
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QPoint over2 = w.map()->mapFromGlobal(m2->mapToGlobal(QPoint(32, 160)));
        QTest::mouseMove(w.map(), over2);
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, over2);
        QCOMPARE(int(seen.size()), 3);
        QCOMPARE(seen[0].control, std::string("main.map"));
        // winX/winY are screen pixels on both controls, so the drag threshold measures the real distance.
        const QPoint g0 = w.map()->mapToGlobal(QPoint(80, 400)), g2 = m2->mapToGlobal(QPoint(32, 160));
        QCOMPARE(seen[0].ev.winX, g0.x());
        QCOMPARE(seen[0].ev.winY, g0.y());
        for (int i : {1, 2}) {
            QCOMPARE(seen[size_t(i)].control, std::string("main.map2"));
            QCOMPARE(seen[size_t(i)].scene, m2->hitScene());
            QCOMPARE(seen[size_t(i)].ev.imgX, 16);
            QCOMPARE(seen[size_t(i)].ev.imgY, 80);
            QCOMPARE(seen[size_t(i)].ev.winX, g2.x());
            QCOMPARE(seen[size_t(i)].ev.winY, g2.y());
        }
        w.hide();
    }
    // While a button is held the topmost window under the pointer decides what a motion or release names: a camera map
    // in a popup lying over the main map is that map, the popup's page is the popup's window id, and outside every
    // window is no control at all. No leave follows for the main map when the release lands on the popup.
    void heldPointerNamesTheTopmostWindow() {
        win->hide();
        struct Reshow {
            SkinWindow* w;
            ~Reshow() {
                w->show();
                w->activateWindow();
            }
        } reshow{win};
        SkinWindow w;
        QVERIFY(w.loadSkin(kTwoMaps).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QImage f(480, 480, QImage::Format_RGBX8888), f2(96, 96, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        f2.fill(Qt::black);
        w.setMapFrame(f, oneAtomScene());
        auto* popup = new QWidget(&w, Qt::Dialog);  // a browse() popup, as shell/browsehost.cpp makes one
        popup->setProperty("fugueControl", "campop");
        auto* cam = new MapWidget(popup);
        cam->setControlId("campop.cam");
        cam->setGeometry(10, 10, 96, 96);
        cam->setFrame(f2, map2Scene());
        popup->setGeometry(QRect(w.map()->mapToGlobal(QPoint(200, 200)), QSize(200, 200)));
        popup->show();
        popup->raise();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        std::vector<MapPointer> seen;
        connect(w.map(), &MapWidget::pointer, &w, [&seen](const MapPointer& p) {
            if (p.kind != MapPointer::Hover) seen.push_back(p);
        });
        const QPoint overCam = w.map()->mapFromGlobal(cam->mapToGlobal(QPoint(40, 40)));
        const QPoint overPage = w.map()->mapFromGlobal(popup->mapToGlobal(QPoint(150, 150)));
        QVERIFY(w.map()->rect().contains(overCam) && w.map()->rect().contains(overPage));
        QCOMPARE(QApplication::widgetAt(w.map()->mapToGlobal(overCam)), cam);

        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseMove(w.map(), overCam);
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, overCam);
        QCOMPARE(int(seen.size()), 3);
        QCOMPARE(seen[0].control, std::string("main.map"));
        for (int i : {1, 2}) {
            QCOMPARE(seen[size_t(i)].control, std::string("campop.cam"));
            QCOMPARE(seen[size_t(i)].scene, cam->hitScene());
            QVERIFY(!seen[size_t(i)].offMap);
            QCOMPARE(seen[size_t(i)].ev.imgX, 40);
            QCOMPARE(seen[size_t(i)].ev.imgY, 40);
        }
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(w.map(), &leave);
        QCoreApplication::processEvents();
        QCOMPARE(int(seen.size()), 3);

        seen.clear();
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, overPage);
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(seen[1].offMap);
        QCOMPARE(seen[1].control, std::string("campop"));
        QVERIFY(!seen[1].scene);

        seen.clear();
        const QPoint outside = w.map()->mapFromGlobal(w.frameGeometry().bottomRight() + QPoint(200, 200));
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, outside);
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(seen[1].offMap);
        QCOMPARE(seen[1].control, std::string());

        // Inside the map control, over its letterbox, the pixel is extrapolated rather than clamped to the edge.
        w.map()->setIconSize(32);
        QImage small(160, 160, QImage::Format_RGBX8888);
        small.fill(Qt::black);
        w.setMapFrame(small, oneAtomScene());
        const QRect ir = w.map()->imageRect();
        seen.clear();
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, ir.topLeft() + QPoint(5, 5));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, ir.topLeft() + QPoint(-20, 5));
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(!seen[1].offMap);
        QCOMPARE(seen[1].ev.imgX, -20);
        w.hide();
    }
    // Where the platform does not report window positions (Wayland puts every top-level window at 0,0, so widgetAt follows
    // creation order), only the pressed control and its own window are looked at: a popup created later and reported over
    // the main map is not seen. A map with no scene yet is still that map.
    void heldPointerWithoutWindowPositions() {
        win->hide();
        struct Reshow {
            SkinWindow* w;
            ~Reshow() {
                MapWidget::setWindowPositionsKnown({});
                w->show();
                w->activateWindow();
            }
        } reshow{win};
        SkinWindow w;
        QVERIFY(w.loadSkin(kTwoMaps).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        w.setMapFrame(f, oneAtomScene());
        MapWidget* m2 = w.auxMap("map2");
        QVERIFY(m2 && !m2->hitScene());
        auto* popup = new QWidget(&w, Qt::Dialog);
        popup->setProperty("fugueControl", "campop");
        popup->setGeometry(w.geometry());
        popup->show();
        popup->raise();
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        const QPoint onMap(50, 50), onMap2 = w.map()->mapFromGlobal(m2->mapToGlobal(QPoint(20, 20)));
        QVERIFY(popup->isAncestorOf(QApplication::widgetAt(w.map()->mapToGlobal(onMap))) ||
                QApplication::widgetAt(w.map()->mapToGlobal(onMap)) == popup);
        MapWidget::setWindowPositionsKnown(false);
        std::vector<MapPointer> seen;
        connect(w.map(), &MapWidget::pointer, &w, [&seen](const MapPointer& p) {
            if (p.kind != MapPointer::Hover) seen.push_back(p);
        });

        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseMove(w.map(), onMap);
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, onMap);
        QCOMPARE(int(seen.size()), 3);
        for (int i : {1, 2}) {
            QVERIFY(!seen[size_t(i)].offMap);
            QCOMPARE(seen[size_t(i)].control, std::string("main.map"));
            QCOMPARE(seen[size_t(i)].scene, w.map()->hitScene());
        }

        seen.clear();
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, onMap2);
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(!seen[1].offMap);
        QCOMPARE(seen[1].control, std::string("main.map2"));
        QVERIFY(!seen[1].scene);

        // A control lying on the map in the same window (vg's mapwindow.credits browser) is what a release over it names.
        auto* overlay = new QWidget(w.map()->parentWidget());
        overlay->setProperty("fugueControl", "main.overlay");
        overlay->setGeometry(QRect(w.map()->mapTo(w.map()->parentWidget(), QPoint(100, 100)), QSize(60, 60)));
        overlay->show();
        overlay->raise();
        seen.clear();
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(120, 120));
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(seen[1].offMap);
        QCOMPARE(seen[1].control, std::string("main.overlay"));
        delete overlay;

        seen.clear();
        const QPoint outside = w.map()->mapFromGlobal(w.frameGeometry().bottomRight() + QPoint(200, 200));
        QTest::mousePress(w.map(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 400));
        QTest::mouseRelease(w.map(), Qt::LeftButton, Qt::NoModifier, outside);
        QCOMPARE(int(seen.size()), 2);
        QVERIFY(seen[1].offMap);
        QCOMPARE(seen[1].control, std::string());
        w.hide();
    }
    void letterboxAndMissingSceneSendNothing() {
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        win->map()->setIconSize(32);
        QImage small(160, 160, QImage::Format_RGBX8888);
        small.fill(Qt::black);
        win->setMapFrame(small, oneAtomScene());
        QRect ir = win->map()->imageRect();
        QVERIFY(ir.width() < win->map()->width());
        QTest::mousePress(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));  // in the letterbox
        QTest::mouseRelease(win->map(), Qt::LeftButton, Qt::NoModifier, QPoint(1, 1));
        QCOMPARE(spy.count(), 0);
        // inside the image the pointer maps through the zoom: image centre pixel
        QTest::mouseClick(win->map(), Qt::LeftButton, Qt::NoModifier, ir.center());
        QCOMPARE(spy.count(), 2);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QVERIFY(std::abs(p.ev.imgX - 80) <= 1 && std::abs(p.ev.imgY - 80) <= 1);
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
    }
    // A vertical notch over the map is a Wheel pointer naming the map control; a horizontal one sends nothing, except
    // Alt+wheel, which Qt on X11 delivers as horizontal and stock sends as a vertical notch.
    void wheelNotchBecomesAWheelPointer() {
        QCOMPARE(win->map()->controlId(), std::string("main.map"));
        QSignalSpy spy(win->map(), &MapWidget::pointer);
        auto wheel = [&](QPoint angle, Qt::KeyboardModifiers mods, Qt::MouseButtons buttons = Qt::NoButton) {
            QWheelEvent e(QPointF(80, 400), win->map()->mapToGlobal(QPointF(80, 400)), QPoint(), angle, buttons, mods,
                          Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(win->map(), &e);
        };
        wheel(QPoint(0, 120), Qt::NoModifier, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Wheel));
        QCOMPARE(p.wheelY, 120);
        QCOMPARE(p.ev.imgX, 80);
        QCOMPARE(p.ev.imgY, 400);
        QCOMPARE(p.ev.button, 1);
        wheel(QPoint(-120, 0), Qt::NoModifier);
        QCOMPARE(spy.count(), 1);
        wheel(QPoint(-120, 0), Qt::AltModifier);
        QCOMPARE(spy.count(), 2);
        p = spy.at(1).at(0).value<MapPointer>();
        QCOMPARE(p.wheelY, -120);
        QVERIFY(p.ev.mods.alt);
    }
    // A natively presented frame has no QImage in the widget: wheel, click, hover and the local menu use its size.
    void externalFrameTakesPointers() {
        MapWidget* map = win->map();
        win->setMapExternalFrame(QSize(480, 480), oneAtomScene(), [] { return QImage(); });
        QVERIFY(map->hasFrame());
        QSignalSpy spy(map, &MapWidget::pointer);
        QWheelEvent e(QPointF(80, 400), map->mapToGlobal(QPointF(80, 400)), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(map, &e);
        QCOMPARE(spy.count(), 1);
        auto p = spy.at(0).at(0).value<MapPointer>();
        QCOMPARE(int(p.kind), int(MapPointer::Wheel));
        QCOMPARE(p.ev.imgX, 80);
        QCOMPARE(p.ev.imgY, 400);
        QPoint cell(2 * 32 + 5, 12 * 32 + 5);  // the atom at (3,3)
        QTest::mouseClick(map, Qt::LeftButton, Qt::NoModifier, cell);
        QCOMPARE(spy.count(), 3);
        QCOMPARE(int(spy.at(1).at(0).value<MapPointer>().kind), int(MapPointer::Press));
        QMouseEvent move(QEvent::MouseMove, QPointF(cell), map->mapToGlobal(QPointF(cell)), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(map, &move);
        QCOMPARE(map->hoverName(), QString("thing"));
        QSignalSpy menu(map, &MapWidget::localMenuRequested);
        map->setRightClickToWire(false);
        QTest::mouseClick(map, Qt::RightButton, Qt::NoModifier, cell);
        map->setRightClickToWire(true);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.at(0).at(1).toString(), QString("thing"));
        QImage f(480, 480, QImage::Format_RGBX8888);
        f.fill(Qt::black);
        win->setMapFrame(f, oneAtomScene());
    }
    // Stock DreamSeeker walks only through a binding: an arrow nothing binds sends nothing, and the release of one whose
    // press ran a binding sends nothing unless it has a +UP binding of its own.
    void arrowsRunOnlyTheirBindings() {
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        KeySeen seen;
        win->map()->installEventFilter(&seen);
        QTest::keyPress(win->map(), Qt::Key_Right);
        QTest::keyRelease(win->map(), Qt::Key_Right);
        QTest::keyPress(win->map(), Qt::Key_Clear);
        QTest::keyRelease(win->map(), Qt::Key_Clear);
        QCOMPARE(mac.count(), 0);
        QCOMPARE(seen.keys,
                 QList<int>({Qt::Key_Right, Qt::Key_Right, Qt::Key_Clear, Qt::Key_Clear}));  // passed on, not taken
        win->map()->removeEventFilter(&seen);
        QTest::keyPress(win->map(), Qt::Key_Down);
        QTest::keyRelease(win->map(), Qt::Key_Down);
        QCOMPARE(mac.count(), 1);
        QCOMPARE(mac.at(0).at(1).toString(), QString("lg south"));
        QVERIFY(!mac.at(0).at(2).toBool());
    }
    // A skin with no macro set: no key runs anything (stock), the default skin's arrows included.
    void noMacroSetMeansNoKeys() {
        FocusFilter f;
        f.setMacros(fugue::skin::MacroSet{});
        qApp->removeEventFilter(filt);
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        for (int k : {Qt::Key_Up, Qt::Key_Down, Qt::Key_Left, Qt::Key_Right, Qt::Key_Clear}) {
            QTest::keyPress(win->map(), Qt::Key(k));
            QTest::keyRelease(win->map(), Qt::Key(k));
        }
        QCOMPARE(mac.count(), 0);
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
    }
    // tg's SSinput set: Any and Any+UP fire for every key, the modifier keys too, before the key's own binding.
    void anyFiresForEveryKeyBeforeItsOwn() {
        fugue::skin::MacroSet set;
        for (auto [spec, cmd] : {std::pair{"Any", "KeyDown [[*]]"},
                                 {"Any+UP", "KeyUp [[*]]"},
                                 {"SHIFT+Any", "ShiftDown [[*]]"},
                                 {"Z", "zz"},
                                 {"X+REP", "xrep"},
                                 {"CTRL+Q", "ctrlq"}})
            fugue::skin::editUserMacro(set, "", spec, cmd);
        FocusFilter f;
        f.setMacros(set);
        qApp->removeEventFilter(filt);
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        auto got = [&] {
            QStringList l;
            for (const auto& a : mac)
                l << a.at(0).toString() + "|" + a.at(1).toString() + (a.at(2).toBool() ? "|up" : "");
            mac.clear();
            return l;
        };
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyClick(win->map(), Qt::Key_Z);
        QCOMPARE(got(), QStringList({"Z|KeyDown [[*]]", "|zz", "Z|KeyUp [[*]]|up"}));
        auto send = [&](QEvent::Type t, int k, Qt::KeyboardModifiers m, bool autoRep = false) {
            QKeyEvent e(t, k, m, QString(), autoRep);  // one event per key: QTest would add its own modifier presses
            QCoreApplication::sendEvent(win->map(), &e);
        };
        send(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
        send(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier);
        send(QEvent::KeyRelease, Qt::Key_A, Qt::ShiftModifier);
        send(QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier);
        QCOMPARE(got(), QStringList({"Shift|ShiftDown [[*]]", "A|ShiftDown [[*]]", "A|KeyUp [[*]]|up",
                                     "Shift|KeyUp [[*]]|up"}));
        send(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
        send(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, true);
        send(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
        QCOMPARE(got(),
                 QStringList({"X|KeyDown [[*]]", "|xrep", "X|KeyUp [[*]]|up"}));  // the keyboard's repeat runs nothing
        QTest::keyClick(win->map(), Qt::Key_Up);                                  // Any claims an unbound arrow
        QCOMPARE(got(), QStringList({"North|KeyDown [[*]]", "North|KeyUp [[*]]|up"}));
        // the command bar: typing fires no Any, a key that neither types nor edits does, and keeps its edit
        auto* in = win->findChild<QLineEdit*>();
        in->clear();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyClicks(in, "ab");
        QTest::keyClick(in, Qt::Key_Left);
        QTest::keyClick(in, Qt::Key_C);
        QTest::keyClick(in, Qt::Key_Q, Qt::ControlModifier);
        QCOMPARE(in->text(), QString("acb"));
        QCOMPARE(got(), QStringList({"West|KeyDown [[*]]", "West|KeyUp [[*]]|up", "|ctrlq"}));
        in->clear();
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
    }
    void ownBindingFallsBackToFewerModifiers() {
        fugue::skin::MacroSet set;
        for (auto [spec, cmd] :
             {std::pair{"Z", "zz"}, {"CTRL+Z+UP", "zup"}, {"F5", "f5"}, {"North", "north"}, {"SHIFT", "shiftkey"}})
            fugue::skin::editUserMacro(set, "", spec, cmd);
        FocusFilter f;
        f.setMacros(set);
        qApp->removeEventFilter(filt);
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        auto got = [&] {
            QStringList l;
            for (const auto& a : mac) l << a.at(1).toString();
            mac.clear();
            return l;
        };
        auto send = [&](QWidget* w, QEvent::Type t, int k, Qt::KeyboardModifiers m) {
            QKeyEvent e(t, k, m);
            QCoreApplication::sendEvent(w, &e);
        };
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        for (Qt::KeyboardModifiers m :
             {Qt::KeyboardModifiers(Qt::ShiftModifier), Qt::KeyboardModifiers(Qt::AltModifier),
              Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier}) {
            send(win->map(), QEvent::KeyPress, Qt::Key_Z, m);
            send(win->map(), QEvent::KeyRelease, Qt::Key_Z, m);
            QCOMPARE(got(), (m & Qt::ControlModifier) ? QStringList({"zz", "zup"}) : QStringList({"zz"}));
        }
        // a release is named by the modifiers held at its press
        send(win->map(), QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Z, Qt::NoModifier);
        QCOMPARE(got(), QStringList({"zz", "zup"}));
        send(win->map(), QEvent::KeyPress, Qt::Key_Shift, Qt::ControlModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Shift, Qt::ControlModifier);
        QCOMPARE(got(), QStringList({"shiftkey"}));
        send(win->map(), QEvent::KeyPress, Qt::Key_Up, Qt::ShiftModifier);
        QCOMPARE(got(), QStringList({"north"}));
        send(win->map(), QEvent::KeyRelease, Qt::Key_Up, Qt::ShiftModifier);
        // REP bindings repeat on the client's clock (repeatTick), never on the keyboard's auto-repeat; a hold repeats
        // only if the combination that claimed its press has a REP binding, and each repeat follows the modifiers held now
        for (auto [spec, cmd] : {std::pair{"X+REP", "xrep"},
                                 {"CTRL+X", "ctrlx"},
                                 {"Y+REP", "yrep"},
                                 {"CTRL+Y+REP", "ctrlyrep"},
                                 {"K", "k"},
                                 {"K+REP", "krep"},
                                 {"F2", "f2"},
                                 {"F2+REP", "f2rep"},
                                 {"W+REP", "wrep"},
                                 {"CTRL+W", "ctrlw"},
                                 {"J", "j"},
                                 {"CTRL+J+REP", "ctrljrep"}})
            fugue::skin::editUserMacro(set, "", spec, cmd);
        f.setMacros(set);
        auto autoRep = [&](int k, Qt::KeyboardModifiers m) {
            QKeyEvent e(QEvent::KeyPress, k, m, QString(), true);
            QCoreApplication::sendEvent(win->map(), &e);
        };
        auto tick = [&](int n = 1) {
            for (int i = 0; i < n; i++) f.repeatTick(f.nowMs() + 1000);
        };
        auto mod = [&](QEvent::Type t, int k, Qt::KeyboardModifiers m) { send(win->map(), t, k, m); };
        mod(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        send(win->map(), QEvent::KeyPress, Qt::Key_X, Qt::ControlModifier);
        autoRep(Qt::Key_X, Qt::ControlModifier);
        tick();
        mod(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
        tick();
        send(win->map(), QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
        QCOMPARE(got(), QStringList({"ctrlx"}));
        mod(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        send(win->map(), QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
        autoRep(Qt::Key_Y, Qt::ControlModifier);
        tick();
        mod(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
        tick(2);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Y, Qt::NoModifier);
        tick();
        QCOMPARE(got(), QStringList({"ctrlyrep", "ctrlyrep", "yrep", "yrep"}));
        // a key bound both ways (bay binds F2 and F2+REP): the press runs K, and K+REP starts at the keyboard's first
        // auto-repeat, then follows the client's clock
        for (int k : {int(Qt::Key_K), int(Qt::Key_F2)}) {
            send(win->map(), QEvent::KeyPress, k, Qt::NoModifier);
            tick();
            autoRep(k, Qt::NoModifier);
            tick(2);
            autoRep(k, Qt::NoModifier);
            send(win->map(), QEvent::KeyRelease, k, Qt::NoModifier);
            tick();
        }
        QCOMPARE(got(), QStringList({"k", "krep", "krep", "krep", "f2", "f2rep", "f2rep", "f2rep"}));
        // W+REP claims the press; with Ctrl down the repeat passes over CTRL+W, which has no REP binding
        send(win->map(), QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        mod(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        tick();
        mod(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier);
        // J claims the press and has no REP binding: no repeat even with Ctrl down
        send(win->map(), QEvent::KeyPress, Qt::Key_J, Qt::NoModifier);
        mod(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        tick();
        mod(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_J, Qt::NoModifier);
        QCOMPARE(got(), QStringList({"wrep", "wrep", "j"}));
        // the first repeat waits 1.75 ticks after the press
        f.setFrameTiming(40'000'000, 40'000'000);
        send(win->map(), QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
        f.repeatTick(f.nowMs() + 60);
        QCOMPARE(got(), QStringList({"xrep"}));
        f.repeatTick(f.nowMs() + 75);
        QCOMPARE(got(), QStringList({"xrep"}));
        send(win->map(), QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
        f.setFrameTiming(0, 0);
        // only the newest held repeating key repeats; releasing it hands the repeat back
        send(win->map(), QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
        tick();
        send(win->map(), QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier);
        tick();
        send(win->map(), QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier);  // a key whose hold does not repeat
        tick();
        send(win->map(), QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Y, Qt::NoModifier);
        tick();
        send(win->map(), QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier);
        tick();
        QCOMPARE(got(), QStringList({"xrep", "xrep", "yrep", "yrep", "yrep", "xrep"}));
        // a modifier key matches only its bare binding: CTRL+SHIFT never fires
        fugue::skin::editUserMacro(set, "", "CTRL+SHIFT", "ctrlshift");
        fugue::skin::editUserMacro(set, "", "CTRL", "ctrlkey");
        f.setMacros(set);
        send(win->map(), QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        send(win->map(), QEvent::KeyPress, Qt::Key_Shift, Qt::ControlModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
        send(win->map(), QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
        send(win->map(), QEvent::KeyPress, Qt::Key_Control, Qt::ShiftModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier | Qt::ShiftModifier);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier);
        QCOMPARE(got(), QStringList({"ctrlkey", "shiftkey", "shiftkey", "ctrlkey"}));
        fugue::skin::editUserMacro(set, "SHIFT", "", "");  // a modifier key never repeats
        fugue::skin::editUserMacro(set, "", "SHIFT+REP", "shiftrep");
        f.setMacros(set);
        send(win->map(), QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
        tick(2);
        send(win->map(), QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier);
        QCOMPARE(got(), QStringList({"shiftrep"}));
        fugue::skin::editUserMacro(set, "SHIFT+REP", "SHIFT", "shiftkey");
        f.setMacros(set);
        // the command bar: Shift+letter still types; Alt and Ctrl combinations and the function keys fall back
        auto* in = win->findChild<QLineEdit*>();
        in->clear();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyClick(in, 'Z', Qt::ShiftModifier);
        QCOMPARE(in->text(), QString("Z"));
        QCOMPARE(got(), QStringList({"shiftkey"}));  // QTest presses Shift first, into the empty bar
        QTest::keyClick(in, Qt::Key_Z, Qt::AltModifier);
        QTest::keyClick(in, Qt::Key_F5, Qt::ShiftModifier);
        QCOMPARE(got(), QStringList({"zz", "f5"}));
        QCOMPARE(in->text(), QString("Z"));
        in->clear();
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
    }
    // The input bar's own-binding rules, as stock DreamSeeker 516 applies them with every combination bound exactly
    // and through the modifier fallback.
    void inputBarKeys() {
        fugue::skin::MacroSet set;
        for (auto [spec, cmd] : {std::pair{"Z", "z"},
                                 {"CTRL+Z", "ctrlz"},
                                 {"CTRL+A", "ctrla"},
                                 {"ALT+C", "altc"},
                                 {"North", "north"},
                                 {"CTRL+North", "ctrlnorth"},
                                 {"West", "west"},
                                 {"West+UP", "westup"},
                                 {"CTRL+West+UP", "ctrlwestup"},
                                 {"Back", "back"},
                                 {"CTRL+Back", "ctrlback"},
                                 {"Back+UP", "backup"},
                                 {"Delete", "delete"},
                                 {"CTRL+Delete", "ctrldelete"},
                                 {"Return", "return"},
                                 {"Space", "space"},
                                 {"Tab", "tab"},
                                 {"Escape", "escape"},
                                 {"Q+UP", "qup"},
                                 {"SHIFT", "shiftkey"},
                                 {"F5", "f5"}})
            fugue::skin::editUserMacro(set, "", spec, cmd);
        FocusFilter f;
        f.setMacros(set);
        qApp->removeEventFilter(filt);
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        auto* in = win->findChild<QLineEdit*>();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        auto key = [&](const char* text, int k, Qt::KeyboardModifiers m = Qt::NoModifier) {
            in->setText(text);
            in->setCursorPosition(in->text().size());
            QKeyEvent p(QEvent::KeyPress, k, m), r(QEvent::KeyRelease, k, m);
            QCoreApplication::sendEvent(in, &p);
            QCoreApplication::sendEvent(in, &r);
            QStringList l;
            for (const auto& a : mac) l << a.at(1).toString();
            mac.clear();
            return l.join(",");
        };
        const Qt::KeyboardModifiers none = Qt::NoModifier, shift = Qt::ShiftModifier, ctrl = Qt::ControlModifier,
                                    alt = Qt::AltModifier;
        // characters type; Ctrl or Alt runs the binding, but Ctrl+Z/C/X/V are the field's
        QCOMPARE(key("", Qt::Key_Z), QString());
        QCOMPARE(key("", Qt::Key_Z, ctrl), QString());
        QCOMPARE(key("", Qt::Key_Z, ctrl | shift), QString());
        QCOMPARE(key("", Qt::Key_Z, alt), QString("z"));
        QCOMPARE(key("", Qt::Key_A, ctrl), QString("ctrla"));
        QCOMPARE(key("", Qt::Key_A, ctrl | shift), QString("ctrla"));
        QCOMPARE(key("", Qt::Key_C, alt | shift), QString("altc"));
        QCOMPARE(key("ab", Qt::Key_Q), QString());  // a typed key's release is the field's too
        // navigation keys run their binding without Ctrl, text or not; with Ctrl the release still runs the bare +UP
        QCOMPARE(key("ab", Qt::Key_Up), QString("north"));
        QCOMPARE(key("", Qt::Key_Up, shift), QString("north"));
        QCOMPARE(key("", Qt::Key_Up, ctrl), QString());
        QCOMPARE(key("ab", Qt::Key_Left, shift | alt), QString("west,westup"));
        QCOMPARE(key("ab", Qt::Key_Left, ctrl), QString("westup"));
        QCOMPARE(key("", Qt::Key_Left, ctrl | shift), QString("westup"));
        // Backspace, Return, Space and the modifier keys only while the field is empty; Delete also not with Ctrl
        QCOMPARE(key("", Qt::Key_Backspace, ctrl), QString("ctrlback,backup"));
        QCOMPARE(key("ab", Qt::Key_Backspace, ctrl), QString("backup"));
        QCOMPARE(key("", Qt::Key_Return, shift), QString("return"));
        QCOMPARE(key("ab", Qt::Key_Return), QString());
        QCOMPARE(key("", Qt::Key_Space), QString("space"));
        QCOMPARE(key("ab", Qt::Key_Space), QString());
        QCOMPARE(key("", Qt::Key_Shift), QString("shiftkey"));
        QCOMPARE(key("ab", Qt::Key_Shift), QString());
        QCOMPARE(key("", Qt::Key_Delete), QString("delete"));
        QCOMPARE(key("ab", Qt::Key_Delete), QString());
        QCOMPARE(key("", Qt::Key_Delete, ctrl), QString());
        // always: Tab (Shift+Tab is Backtab to Qt), Escape, the function keys
        QCOMPARE(key("ab", Qt::Key_Tab), QString("tab"));
        QCOMPARE(key("ab", Qt::Key_Backtab, shift), QString("tab"));
        QCOMPARE(key("ab", Qt::Key_Escape, ctrl), QString("escape"));
        QCOMPARE(key("ab", Qt::Key_F5, ctrl | shift), QString("f5"));
        in->clear();
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
    }
    // The right-click verb menu is a QMenu popup, so a key released while it is open is delivered to the menu. A held key's release must
    // reach the server wherever it lands: the popup, another zone, or nowhere at all (application deactivated).
    void heldKeyReleasedInsideAPopupStillReleases() {
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        auto fired = [&](int i) { return mac.at(i).at(1).toString() + (mac.at(i).at(2).toBool() ? "|up" : ""); };
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyPress(win->map(), Qt::Key_Left);
        QCOMPARE(mac.count(), 1);
        QCOMPARE(fired(0), QString("MoveKey 8 1"));
        QMenu menu(win);
        menu.addAction("poke");
        menu.popup(win->mapToGlobal(QPoint(80, 80)));
        QVERIFY(QTest::qWaitFor([&] { return QApplication::activePopupWidget() == &menu; }));
        QVERIFY(qApp->focusWidget() != &menu);  // the popup holds the keyboard without becoming the focus widget
        QTest::keyRelease(&menu, Qt::Key_Left);
        QCOMPARE(mac.count(), 2);
        QCOMPARE(fired(1), QString("MoveKey 8 0|up"));
        menu.close();
        QVERIFY(QTest::qWaitFor([&] { return !QApplication::activePopupWidget(); }));
        // released once: a second copy (window and widget both receive it) finds nothing held
        QTest::keyRelease(win->map(), Qt::Key_Left);
        QCOMPARE(mac.count(), 3);  // an unmatched release still routes on the focus widget
        // a release that lands in the command bar is the held key's too (plain keys there never route)
        QTest::keyPress(win->map(), Qt::Key_Up);
        auto* in = win->findChild<QLineEdit*>();
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyRelease(in, Qt::Key_Up);
        QCOMPARE(mac.count(), 5);
        QCOMPARE(fired(4), QString("MoveKey 1 0|up"));
    }
    void heldKeyIsReleasedWhenTheApplicationDeactivates() {
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyPress(win->map(), Qt::Key_Up);
        QCOMPARE(mac.count(), 1);
        QEvent deact(QEvent::ApplicationDeactivate);
        QCoreApplication::sendEvent(qApp, &deact);
        QCOMPARE(mac.count(), 2);
        QCOMPARE(mac.at(1).at(1).toString(), QString("MoveKey 1 0"));
        QVERIFY(mac.at(1).at(2).toBool());
        QCoreApplication::sendEvent(qApp, &deact);  // nothing held any more
        QCOMPARE(mac.count(), 2);
    }

    void routing_data() {
        QTest::addColumn<QString>("cmd");
        QTest::addColumn<bool>("up");
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("dir");
        QTest::addColumn<bool>("down");
        QTest::addColumn<QString>("text");
        QTest::newRow("verb") << "say \"hi\"" << false << int(Route::Command) << 0 << true << "say \"hi\"";
        QTest::newRow("movekey") << "MoveKey 8 0" << true << int(Route::Command) << 0 << true << "MoveKey 8 0";
        QTest::newRow("empty masks") << "  " << false << int(Route::Swallow) << 0 << true << "";
        QTest::newRow(".north") << ".north" << false << int(Route::Move) << 1 << true << "";
        QTest::newRow(".southwest up") << ".SouthWest" << true << int(Route::Move) << 10 << false << "";
        QTest::newRow(".center") << ".center" << false << int(Route::Move) << 0 << true << "";
        QTest::newRow("macro swap") << ".winset \"mainwindow.macro=hotkeymode mapwindow.map.focus=true\"" << false
                                    << int(Route::SetMacroSet) << 0 << true << "hotkeymode";
        QTest::newRow("winset no macro") << ".winset \"rpanewindow.left=;\"" << false << int(Route::Winset) << 0 << true
                                         << "rpanewindow.left=;";
        QTest::newRow("winset unquoted") << ".winset rpane.lbl.text=hi" << false << int(Route::Winset) << 0 << true
                                         << "rpane.lbl.text=hi";
        QTest::newRow("quit") << ".quit" << false << int(Route::Quit) << 0 << true << "";
        QTest::newRow("reconnect") << ".Reconnect" << false << int(Route::Reconnect) << 0 << true << "";
        QTest::newRow("unknown dot cmd is NOT a verb")
            << ".profile" << false << int(Route::Unsupported) << 0 << true << ".profile";
        QTest::newRow("screenshot dialog") << ".screenshot" << false << int(Route::Screenshot) << 0 << true << "";
        QTest::newRow("screenshot auto") << ".screenshot auto" << false << int(Route::Screenshot) << 0 << true
                                         << "auto";
        QTest::newRow("screenshot AUTO") << "  .Screenshot  AUTO " << false << int(Route::Screenshot) << 0 << true
                                         << "auto";
        QTest::newRow(".macros opens the macro editor") << ".macros" << false << int(Route::Macros) << 0 << true << "";
        QTest::newRow(".options opens Options and Messages")
            << ".options" << false << int(Route::Options) << 0 << true << "";
        QTest::newRow("any-key") << "say [[*]]" << false << int(Route::Command) << 0 << true << "say \"K\"";
        QTest::newRow("any-key quoted") << "say \"x[[*]]\" \"[[*]]\"" << false << int(Route::Command) << 0 << true
                                        << "say \"x\"K\"\" \"K\"";
    }
    // A macro command runs line by line, as stock does with tg's Escape (`.winset ...`, `.output ...`, Reset-Held-Keys).
    void macroCommandRunsEachLine() {
        auto lines = [](const char* c) {
            QStringList l;
            for (const std::string& s : macroCommandLines(c)) l << QString::fromStdString(s);
            return l.join("|");
        };
        QCOMPARE(lines("lg one\r\nlg two\r\nlg three"), QString("lg one|lg two|lg three"));
        QCOMPARE(lines("lg one\r\n\r\nlg two"), QString("lg one|lg two"));
        QCOMPARE(lines("lg one  \r\n lg two\r\n\tlg three"), QString("lg one  "));
        QCOMPARE(lines("  lg one"), QString());
        QCOMPARE(lines(".winset \"input.text=hello\"\r\nlg two"), QString(".winset \"input.text=hello\"|lg two"));
        QCOMPARE(lines(""), QString());
    }
    // `.output control text` runs locally: tg's say hotkey opens tgui_say with `.output tgui_say.browser:update <json>`.
    void dotOutputIsLocal() {
        Route r = routeCommand(".output tgui_say.browser:update %7B%22type%22%3A%22open%22%7D", "", false);
        QCOMPARE(int(r.kind), int(Route::Output));
        QCOMPARE(QString::fromStdString(r.text), QString("tgui_say.browser:update"));
        QCOMPARE(QString::fromStdString(r.arg), QString("%7B%22type%22%3A%22open%22%7D"));
        r = routeCommand(".output \"output\" \"hello there\"", "", false);
        QCOMPARE(int(r.kind), int(Route::Output));
        QCOMPARE(QString::fromStdString(r.text), QString("output"));
        QCOMPARE(QString::fromStdString(r.arg), QString("hello there"));
        QCOMPARE(int(routeWorldCommand(".output map:x a").kind), int(Route::Output));  // never sent as a verb
        QCOMPARE(int(routeCommand(".output", "", false).kind), int(Route::Unsupported));
    }
    void anyKeyNameIsADmString() {
        QCOMPARE(QString::fromStdString(routeCommand("KeyDown [[*]]", "[", false).text), QString("KeyDown \"\\[\""));
        QCOMPARE(QString::fromStdString(routeCommand("KeyDown [[*]]", "\\", false).text), QString("KeyDown \"\\\\\""));
        QCOMPARE(QString::fromStdString(routeCommand("lg Q \"[[*]]\" [[*]]", "", false).text),
                 QString("lg Q \"\" "));  // not an Any macro
    }
    // Embedded winget: tg's `Any` macro carries the map's mouse position and size as four numbers.
    void embeddedWingetFormats() {
        std::vector<std::pair<std::string, std::string>> asked;
        SkinLookup look = [&](const std::string& ctl, const std::string& p, std::string& out) {
            asked.push_back({ctl, p});
            if (ctl == "map" && p == "mouse-pos") return out = "123,45", true;
            if (ctl == "map" && p == "size") return out = "480x480", true;
            if (ctl == "map" && p == "icon-size") return out = "32", true;
            if (ctl == "map" && p == "focus") return out = "true", true;
            if (ctl == "mainwindow" && p == "title") return out = "Dream \"Seeker\"", true;
            if (ctl == "input" && p == "text") return out = "12", true;
            return false;
        };
        auto ex = [&](const char* c) { return QString::fromStdString(routeCommand(c, "East", false, look).text); };
        QCOMPARE(ex("KeyDown [[*]] [[map.mouse-pos]] [[map.size]]"), QString("KeyDown \"East\" 123 45 480 480"));
        QCOMPARE(asked.size(), size_t(2));
        QCOMPARE(QString::fromStdString(asked[0].first + "|" + asked[0].second), QString("map|mouse-pos"));
        QCOMPARE(ex("lg [[map.size.x]] [[map.mouse-pos.y]] [[map.icon-size]] [[map.focus]]"),
                 QString("lg 480 45 32 1"));
        QCOMPARE(ex("lg [[mainwindow.title]]"), QString("lg \"Dream \\\"Seeker\\\"\""));
        QCOMPARE(ex("lg \"[[mainwindow.title]]\""), QString("lg \"Dream \\\"Seeker\\\"\""));
        QCOMPARE(ex("lg \"[[map.size]]\" [[map.size as escaped]] [[map.size as raw]] [[map.size as string]]"),
                 QString("lg \"480,480\" 480,480 480x480 \"480x480\""));
        QCOMPARE(ex("lg [[map.size as json]] [[map.focus as json]] [[mainwindow.title as json]]"),
                 QString("lg {\"x\":480,\"y\":480} true \"Dream \\\"Seeker\\\"\""));
        QCOMPARE(ex("lg [[map.mouse-pos as params]] [[mainwindow.title as params]]"),
                 QString("lg 123%2c45 Dream+%22Seeker%22"));
        QCOMPARE(ex("lg [[input.text]] [[nonesuch.size]] [[* as escaped]]"), QString("lg \"12\" \"\" East"));
        // Without a lookup only [[*]] expands.
        QCOMPARE(QString::fromStdString(routeCommand("KeyUp [[*]] [[map.size]]", "A", true).text),
                 QString("KeyUp \"A\" [[map.size]]"));
    }
    void winsetBodyIsAlwaysHandedOn() {
        // A hotkey toggle: the macro swap AND a checkbox AND the map focus travel together; the whole body must reach the live skin.
        Route r = routeCommand(
            ".winset \"mainwindow.macro=hotkeymode hotkey_toggle.is-checked=true mapwindow.map.focus=true\"", "",
            false);
        QCOMPARE(int(r.kind), int(Route::SetMacroSet));
        QCOMPARE(QString::fromStdString(r.winsetBody),
                 QString("mainwindow.macro=hotkeymode hotkey_toggle.is-checked=true mapwindow.map.focus=true"));
        QVERIFY(routeCommand("say hi", "", false).winsetBody.empty());
    }
    // A popup's on-close is the world's command: a world may define a verb whose name starts with a dot (`.windowclose <ref>`), and it must be sent.
    // From a key or the bar, the same text is not routed to the world.
    void worldCommandSendsTheWorldsOwnDotVerb() {
        Route r = routeWorldCommand(".windowclose 0x2001a2b");
        QCOMPARE(int(r.kind), int(Route::Command));
        QCOMPARE(QString::fromStdString(r.text), QString(".windowclose 0x2001a2b"));
        QCOMPARE(int(routeCommand(".windowclose 0x2001a2b", "", false).kind),
                 int(Route::Unsupported));  // unchanged for keys/bar
        r = routeWorldCommand("nanoclose [0x21004c7]");
        QCOMPARE(int(r.kind), int(Route::Command));
        QCOMPARE(QString::fromStdString(r.text), QString("nanoclose [0x21004c7]"));
        QCOMPARE(int(routeWorldCommand(".quit").kind), int(Route::Quit));  // builtins still local
        QCOMPARE(int(routeWorldCommand(".winset \"a.is-visible=false\"").kind), int(Route::Winset));
    }
    // .screenshot naming + save, as stock DreamSeeker 516 writes them (shell/screenshot.h). Writes only into a temp dir.
    void screenshotNamingMatchesDreamSeeker() {
        QDateTime t(QDate(2000, 1, 2), QTime(2, 46, 29));
        QCOMPARE(screenshotBaseName("shotworld", t), QString("shotworld 2000-01-02 024629"));
        QCOMPARE(screenshotBaseName("/vg/ Sta:tion* 13?", t),
                 QString("vg Station 13 2000-01-02 024629"));  // stock drops the bad characters, not replaces them
        QCOMPARE(screenshotBaseName("", t), QString("screenshot 2000-01-02 024629"));
        QCOMPARE(withPngSuffix("/x/zq"), QString("/x/zq.png"));
        QCOMPARE(withPngSuffix("/x/zq.png"), QString("/x/zq.png"));
        QCOMPARE(withPngSuffix(""), QString(""));  // a cancelled dialog stays cancelled
    }
    void screenshotSavesTheNativeFrameAndNeverOverwrites() {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        QImage frame(352, 352, QImage::Format_RGBX8888);  // view 5 at 32 px
        frame.fill(QColor(200, 40, 40));
        frame.setPixelColor(7, 9, QColor(40, 40, 200));
        QDateTime t(QDate(2000, 1, 2), QTime(2, 43, 14));
        QString dir = tmp.filePath("screenshots");  // does not exist yet: the save creates it
        QString p1 = screenshotAutoPath(dir, "shotworld", t);
        QCOMPARE(p1, dir + "/shotworld 2000-01-02 024314.png");
        QString err;
        QVERIFY2(saveScreenshot(frame, p1, &err), qPrintable(err));
        QImage back(p1);
        QCOMPARE(back.size(), QSize(352, 352));
        QVERIFY(!back.hasAlphaChannel());
        QCOMPARE(back.pixelColor(7, 9), QColor(40, 40, 200));
        QCOMPARE(back.pixelColor(0, 0), QColor(200, 40, 40));
        QString p2 = screenshotAutoPath(dir, "shotworld", t);  // F2+REP inside one second
        QCOMPARE(p2, dir + "/shotworld 2000-01-02 024314 2.png");
        QVERIFY(!saveScreenshot(QImage(), p2, &err));
        QVERIFY(!QFileInfo::exists(p2));
    }
    // Client > Macros (skin/usermacros.h, as stock DreamSeeker 516 behaves): the player's lists fire only for a key the skin's set leaves unbound,
    // this game's list before the global one; typing in the macro editor fires nothing.
    void userMacrosSitBeneathTheSkinSet() {
        QSignalSpy mac(filt, &FocusFilter::macroFired);
        fugue::skin::MacroSet game, global;
        fugue::skin::editUserMacro(global, "", "Q", "user q");  // the skin binds Q: in use
        fugue::skin::editUserMacro(global, "", "W", "global w");
        fugue::skin::editUserMacro(game, "", "W", "game w");
        fugue::skin::editUserMacro(global, "", "E", "global e");
        filt->setUserMacros(game, global);
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyClick(win->map(), Qt::Key_Q);
        QTest::keyClick(win->map(), Qt::Key_W);
        QTest::keyClick(win->map(), Qt::Key_E);
        QTest::keyClick(win->map(), Qt::Key_R);  // nobody binds R
        QCOMPARE(mac.count(), 3);
        QCOMPARE(mac.at(0).at(1).toString(), QString("qmacro"));
        QCOMPARE(mac.at(1).at(1).toString(), QString("game w"));
        QCOMPARE(mac.at(2).at(1).toString(), QString("global e"));
        // the editor owns its keys: an F-key typed into its Key field (a QLineEdit = the input zone) fires no macro
        fugue::skin::editUserMacro(global, "", "F5", "global f5");
        filt->setUserMacros(game, global);
        MacroEditDialog ed("New Macro", nullptr);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        ed.activateWindow();
        ed.keyEdit()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == ed.keyEdit(); }));
        QCOMPARE(int(FocusFilter::zoneOf(ed.keyEdit())), int(Zone::Other));
        QTest::keyClick(ed.keyEdit(), Qt::Key_F5);
        QCOMPARE(mac.count(), 3);
        filt->setUserMacros({}, {});
    }
    void macroEditorSplitsAndJoinsTheSpec() {
        fugue::skin::MacroSet s;
        fugue::skin::editUserMacro(s, "", "CTRL+SHIFT+F8+REP", "say \"hi\"", "myid");
        MacroEditDialog ed("Edit Macro", &s.entries[0]);
        QCOMPARE(ed.keyEdit()->text(), QString("F8"));
        QVERIFY(ed.box("ctrl")->isChecked() && ed.box("shift")->isChecked() && !ed.box("alt")->isChecked());
        QVERIFY(ed.box("rep")->isChecked() && !ed.box("up")->isChecked());
        QCOMPARE(ed.command(), QString("say \"hi\""));
        QCOMPARE(ed.id(), QString("myid"));
        QCOMPARE(ed.spec(), QString("CTRL+SHIFT+F8+REP"));
        ed.box("rep")->setChecked(false);
        ed.box("up")->setChecked(true);
        ed.box("shift")->setChecked(false);
        ed.keyEdit()->setText("west");
        QCOMPARE(ed.spec(), QString("CTRL+WEST+UP"));
        ed.keyEdit()->clear();
        QCOMPARE(ed.spec(), QString());  // blank key = delete
    }
    void macroDialogEditsTheChosenList() {
        fugue::skin::MacroSet global, game;
        fugue::skin::editUserMacro(global, "", "F6", "hello");
        fugue::skin::editUserMacro(global, "", "F1", "zap user");
        fugue::skin::editUserMacro(game, "", "F7", "hello");
        MacroDialog d(global, game, true);
        QVERIFY(d.scopeBox()->isEnabled() && d.scopeBox()->isChecked());  // default for a game with its own list
        QCOMPARE(d.list()->topLevelItemCount(), 1);
        QCOMPARE(d.list()->topLevelItem(0)->text(0), QString("F7"));
        d.scopeBox()->setChecked(false);  // the global list, sorted by key
        QCOMPARE(d.list()->topLevelItemCount(), 2);
        QCOMPARE(d.list()->topLevelItem(0)->text(0), QString("F1"));
        QCOMPARE(d.list()->topLevelItem(1)->text(1), QString("hello"));
        QVERIFY(d.commit("", "F9", "zap nine"));
        QVERIFY(d.commit("F1", "", "zap user"));  // blank key deletes
        QCOMPARE(int(d.globalSet().entries.size()), 2);
        QVERIFY(!d.globalSet().lookup(
            [] {
                fugue::skin::MacroKey k;
                bool r;
                fugue::skin::parseMacroName("F1", k, r);
                return k;
            }(),
            false));
        QCOMPARE(int(d.gameSet().entries.size()), 1);  // the other list is untouched
        // New Macro... through the real editor: fill it, OK, and the row is there
        MacroEditDialog* ed = d.openEditor(-1);
        QCOMPARE(ed->windowTitle(), QString("New Macro"));
        ed->keyEdit()->setText("F10");
        ed->commandEdit()->setText("hello");
        ed->accept();
        QCOMPARE(d.list()->topLevelItemCount(), 3);
        // double-click an existing row: Edit Macro, prefilled
        MacroEditDialog* ed2 = d.openEditor(0);
        QCOMPARE(ed2->windowTitle(), QString("Edit Macro"));
        QCOMPARE(ed2->keyEdit()->text(), QString("F10"));
        ed2->commandEdit()->clear();  // blank command deletes
        ed2->accept();
        QCOMPARE(d.list()->topLevelItemCount(), 2);
        MacroDialog noGame(global, game, false);  // no per-game list: greyed, global shown
        QVERIFY(!noGame.scopeBox()->isEnabled());
        QCOMPARE(noGame.list()->topLevelItemCount(), 2);
    }
    // Stock DreamSeeker 516 behaviour, driven through the real path: user F6 -> Rest and R -> "zap userR", beside a skin with sets "macro" (no R)
    // and "hotkeymode" (R -> zap hotkeyR). F6 Rest, R userR | switch to hotkeymode | F6 Rest, R hotkeyR | back | F6 Rest, R userR:
    // the user's lists sit beneath whichever set is active and survive the switch. This is a winset of the main window's macro, the
    // window's macroChanged, and the app's re-pick of the active set (app/main.cpp applyMacros).
    void userMacrosSurviveAMacroSetSwitch() {
        static const char* kTwoSets = R"(macro "macro"
	elem
		name = "F1"
		command = "hello"
macro "hotkeymode"
	elem
		name = "R"
		command = "zap hotkeyR"
window "mainwindow"
	elem "mainwindow"
		type = MAIN
		size = 320x320
		is-default = true
		macro = "macro"
	elem "map"
		type = MAP
		pos = 0,0
		size = 320x320
		anchor1 = 0,0
		anchor2 = 100,100
)";
        SkinWindow w;
        QVERIFY(w.loadSkin(kTwoSets).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        FocusFilter f;
        auto pick = [&] {
            auto t = w.macros();
            f.setMacros(t.sets.at(w.activeMacro().toStdString()));
        };
        pick();
        QObject::connect(&w, &SkinWindow::macroChanged, &w, pick);
        fugue::skin::MacroSet game, global;
        fugue::skin::editUserMacro(game, "", "F6", "Rest");
        fugue::skin::editUserMacro(game, "", "R", "zap userR");
        f.setUserMacros(game, global);
        qApp->removeEventFilter(filt);  // only this window's filter answers
        qApp->installEventFilter(&f);
        QSignalSpy mac(&f, &FocusFilter::macroFired);
        w.activateWindow();
        w.map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == w.map(); }));
        auto press = [&] {
            QTest::keyClick(w.map(), Qt::Key_F6);
            QTest::keyClick(w.map(), Qt::Key_R);
        };
        press();
        w.live()->winset("mainwindow", "macro=hotkeymode");  // vg/tg's hotkey-mode switch, as the world sends it
        QVERIFY(QTest::qWaitFor([&] { return w.activeMacro() == "hotkeymode"; }));
        press();
        w.live()->winset("mainwindow", "macro=macro");
        QVERIFY(QTest::qWaitFor([&] { return w.activeMacro() == "macro"; }));
        press();
        qApp->removeEventFilter(&f);
        qApp->installEventFilter(filt);
        QStringList got;
        for (const auto& a : mac) got << a.at(1).toString();
        QCOMPARE(got, QStringList({"Rest", "zap userR", "Rest", "zap hotkeyR", "Rest", "zap userR"}));
    }
    // The top-left system menu (shell/macrodialog.h): the window icon in the menu bar's top-left corner, opening
    // "Options & Messages" and Client > Screenshot... / Reconnect / Macros... / Quit, each routed as the client command it names.
    void systemMenuSitsTopLeftAndLeadsToMacros() {
        static const char* kMenuSkin = R"(menu "menu"
	elem
		name = "&File"
		command = ""
	elem
		name = "Say hello"
		command = "hello"
		category = "&File"
window "mainwindow"
	elem "mainwindow"
		type = MAIN
		size = 320x240
		is-default = true
		menu = "menu"
)";
        SkinWindow w;
        QVERIFY(w.loadSkin(kMenuSkin).isEmpty());
        w.resize(w.designSize());
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QToolButton* b = w.systemMenuButton();
        // the button costs the layout nothing: the bar is as tall as a bar with the same menu and no corner widget
        {
            QMenuBar ref;
            ref.setFont(skinMenuFont());
            ref.addMenu("&File")->addAction("Say hello");
            QCOMPARE(w.menuBar()->sizeHint().height(), ref.sizeHint().height());
        }
        QVERIFY(b);
        QCOMPARE(w.menuBar()->cornerWidget(Qt::TopLeftCorner), static_cast<QWidget*>(b));
        QVERIFY(b->isVisible());
        QPoint at = b->mapTo(&w, QPoint(0, 0));
        QVERIFY2(at.x() < 8 && at.y() < 8, "top-left of the window");
        QAction* file = w.menuBar()->actions().at(0);
        QVERIFY(w.menuBar()->actionGeometry(file).left() >= b->geometry().right());  // left of the skin's own menus
        QVERIFY(!b->icon().isNull());
        QImage px(16, 16, QImage::Format_ARGB32);
        px.fill(Qt::red);
        w.setWindowIcon(QIcon(QPixmap::fromImage(px)));  // a world's icon (setSkinIcon)
        QCOMPARE(b->icon().pixmap(16).toImage().pixelColor(8, 8), QColor(Qt::red));
        QMenu* sys = b->menu();
        QVERIFY(sys);
        QStringList top;
        for (QAction* a : sys->actions()) top << a->text();
        QCOMPARE(top, QStringList({"Options && Messages", "&Client"}));
        QMenu* client = sys->actions().at(1)->menu();
        QVERIFY(client);
        QStringList items;
        for (QAction* a : client->actions()) items << (a->isSeparator() ? QString("-") : a->text());
        QCOMPARE(items, QStringList({"&Screenshot...", "&Reconnect", "-", "&Macros...", "-", "&Quit"}));
        QSignalSpy cmd(&w, &SkinWindow::menuCommand);
        sys->actions().at(0)->trigger();
        client->actions().at(3)->trigger();
        client->actions().at(1)->trigger();
        QCOMPARE(cmd.count(), 3);
        QCOMPARE(int(routeCommand(cmd.at(0).at(0).toString().toStdString(), "", false).kind), int(Route::Options));
        QCOMPARE(int(routeCommand(cmd.at(1).at(0).toString().toStdString(), "", false).kind), int(Route::Macros));
        QCOMPARE(int(routeCommand(cmd.at(2).at(0).toString().toStdString(), "", false).kind), int(Route::Reconnect));
    }
    void optionsWindowLeadsToMacros() {
        OptionsWindow w;
        QSignalSpy cmd(&w, &OptionsWindow::command);
        QMenu* client = w.menuBar()->actions().at(0)->menu();
        QVERIFY(client);
        QAction* macros = nullptr;
        for (QAction* a : client->actions())
            if (a->text() == "&Macros...") macros = a;
        QVERIFY(macros);
        macros->trigger();
        QCOMPARE(cmd.count(), 1);
        QCOMPARE(cmd.at(0).at(0).toString(), QString(".macros"));
        QCOMPARE(int(routeCommand(cmd.at(0).at(0).toString().toStdString(), "", false).kind), int(Route::Macros));
    }
    void routing() {
        QFETCH(QString, cmd);
        QFETCH(bool, up);
        QFETCH(int, kind);
        QFETCH(int, dir);
        QFETCH(bool, down);
        QFETCH(QString, text);
        Route r = routeCommand(cmd.toStdString(), "K", up);
        QCOMPARE(int(r.kind), kind);
        QCOMPARE(r.dir, dir);
        QCOMPARE(r.down, down);
        QCOMPARE(QString::fromStdString(r.text), text);
    }
};

int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    InputTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "input_test.moc"

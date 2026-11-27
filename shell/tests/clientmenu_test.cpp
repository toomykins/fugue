#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QDialog>
#include <QShortcut>
#include <QTemporaryDir>
#include <QToolButton>
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include <QMenu>
#include <QMenuBar>
#include <QSignalSpy>
#include <QtTest>
#include "tests/testmain.h"
#include "shell/skinwindow.h"

using namespace fugue::shell;

static std::string menuSkin(bool menu) {
    return std::string(menu ? R"(menu "menu"
    elem
        name = "&File"
    elem
        name = "Hello"
        category = "&File"
        command = "hello"
)" : "") + R"(window "main"
    elem "main"
        type = MAIN
        size = 320x240
        is-default = true
        statusbar = false
        menu = "menu"
    elem "content"
        type = LABEL
        pos = 0,0
        size = 320x240
        anchor1 = 0,0
        anchor2 = 100,100
)";
}

// Deliver only the chord key: modifier prefixes have their own game-key lifetimes.
static void sendKey(QWidget* target, QEvent::Type type, int key, Qt::KeyboardModifiers mods, bool repeat = false) {
    QKeyEvent event(type, key, mods, QString(), repeat);
    QApplication::sendEvent(target, &event);
}
static constexpr auto kMenuMods = Qt::ControlModifier;
static void chord(QWidget* target, int key = Qt::Key_M, Qt::KeyboardModifiers mods = kMenuMods) {
    sendKey(target, QEvent::KeyPress, key, mods);
    sendKey(QApplication::activePopupWidget() ? QApplication::activePopupWidget() : target,
            QEvent::KeyRelease, key, Qt::NoModifier);
}
struct KeySink : QObject {
    int keys = 0;
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) ++keys;
        return false;
    }
};

class ClientMenuTest : public QObject {
    Q_OBJECT
private slots:
    void skinMenuTransitions() {
        SkinWindow w;
        w.resize(400, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QSignalSpy command(&w, &SkinWindow::menuCommand);
        for (bool menu : {true, false, true, true, false}) {
            QVERIFY(w.loadSkin(menuSkin(menu)).isEmpty());
            for (int snapshot = 0; snapshot < 3; ++snapshot) {
                w.syncFromLive();
                const int bar = w.menuBarHeight();
                QCOMPARE(bar > 0, menu);
                QCOMPARE(w.menuBar()->isVisible(), menu);
                QCOMPARE(w.designSize(), QSize(320, 240 + bar));
                QCOMPARE(w.controlWidget("main.content")->geometry(), QRect(0, bar, 400, 300 - bar));
                QCOMPARE(w.live()->snapshot().client.dy(), 300 - bar);
            }
            if (menu) {
                QCOMPARE(w.menuBar()->actions().size(), 1);
                w.menuBar()->actions().first()->menu()->actions().first()->trigger();
                QCOMPARE(command.takeFirst().first().toString(), QString("hello"));
            }
        }
    }
    void hotkeyLifetimeAndRebuilds() {
        SkinWindow w;
        FocusFilter filter;
        filter.setClientWindow(&w);
        qApp->installEventFilter(&filter);
        fugue::skin::MacroSet game;
        for (const char* spec : {"CTRL+M", "CTRL+M+UP", "Any", "Any+UP"})
            fugue::skin::editUserMacro(game, "", spec, "game-key");
        filter.setMacros(game);
        QSignalSpy macros(&filter, &FocusFilter::macroFired);
        QShortcut conflicting(QKeySequence("Ctrl+M"), &w);
        QSignalSpy shortcut(&conflicting, &QShortcut::activated);
        auto* popup = w.clientMenu();
        QVERIFY(popup);
        auto* metrics = popup->findChild<QAction*>("mapPerformanceAction");
        QVERIFY(metrics);
        w.resize(400, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.activateWindow();
        for (bool menu : {false, true, false, true}) {
            QVERIFY(w.loadSkin(menuSkin(menu)).isEmpty());
            auto* target = w.controlWidget("main.content");
            target->setFocusPolicy(Qt::StrongFocus);
            target->setProperty("fugueZone", "map");
            target->setFocus();
            QTRY_COMPARE(QApplication::focusWidget(), target);
            KeySink seen;
            target->installEventFilter(&seen);
            QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_M, kMenuMods);
            override.ignore();
            QApplication::sendEvent(target, &override);
            QVERIFY(override.isAccepted());
            sendKey(target, QEvent::KeyPress, Qt::Key_M, kMenuMods);
            QTRY_VERIFY(popup->isVisible());
            // Holding the key must not oscillate the popup, including X11 repeat-release pairs.
            sendKey(popup, QEvent::KeyRelease, Qt::Key_M, kMenuMods, true);
            sendKey(popup, QEvent::KeyPress, Qt::Key_M, kMenuMods, true);
            QVERIFY(popup->isVisible());
            sendKey(popup, QEvent::KeyRelease, Qt::Key_M, Qt::NoModifier);
            chord(popup);
            QTRY_VERIFY(!popup->isVisible());
            QCOMPARE(seen.keys, 0);
            QCOMPARE(macros.count(), 0);
            QCOMPARE(shortcut.count(), 0);
            QCOMPARE(w.clientMenu(), popup);
            QCOMPARE(w.findChildren<QAction*>("mapPerformanceAction").size(), 1);
            // Count our categories, not Qt's platform-specific private overflow menu.
            int gameMenus = 0;
            for (auto* child : w.findChildren<QMenu*>()) if (child->title() == "&File") ++gameMenus;
            QCOMPARE(gameMenus, menu ? 1 : 0);
            QCOMPARE(w.findChildren<QMenu*>("clientMenu").size(), 1);
            if (menu) QCOMPARE(w.systemMenuButton()->menu(), popup);
            w.setMapMetricsVisible(true);
            QVERIFY(metrics->isChecked());
            metrics->trigger();
            QVERIFY(!w.mapMetricsVisible());
            QVERIFY(!metrics->isChecked());
            chord(target);
            QTRY_VERIFY(popup->isVisible());
            QTest::keyClick(popup, Qt::Key_Escape);
            QTRY_VERIFY(!popup->isVisible());
            // Repeating the hotkey while the Client submenu has the grab also closes the whole popup.
            chord(target);
            auto* submenu = popup->actions().last()->menu();
            popup->setActiveAction(popup->actions().last());
            QTest::keyClick(popup, Qt::Key_Right);
            QTRY_VERIFY(submenu->isVisible());
            chord(submenu);
            QTRY_VERIFY(!popup->isVisible());
            QTRY_VERIFY(!submenu->isVisible());
        }
        // Fullscreen changes neither registration nor popup placement/ownership.
        w.showFullScreen();
        QTRY_VERIFY(w.isFullScreen());
        chord(&w);
        QTRY_VERIFY(popup->isVisible());
        chord(popup);
        QTRY_VERIFY(!popup->isVisible());
        QSignalSpy command(&w, &SkinWindow::menuCommand);
        popup->actions().first()->trigger();
        popup->actions().last()->menu()->actions().at(3)->trigger();
        QCOMPARE(command.count(), 2);
        QCOMPARE(command.at(0).first().toString(), QString(".options"));
        QCOMPARE(command.at(1).first().toString(), QString(".macros"));
    }
    void configurableChordInInputAndBrowser() {
        SkinWindow w;
        QVERIFY(w.loadSkin(menuSkin(false)).isEmpty());
        w.resize(400, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.activateWindow();
        FocusFilter filter;
        filter.setClientWindow(&w);
        qApp->installEventFilter(&filter);
        fugue::skin::MacroSet global, game, world;
        fugue::skin::editUserMacro(global, "", "CTRL+F", ".clientmenu");
        fugue::skin::editUserMacro(game, "", "CTRL+K", ".clientmenu");
        for (const char* spec : {"CTRL+F", "CTRL+F+UP", "CTRL+K", "CTRL+K+UP", "Any", "Any+UP"})
            fugue::skin::editUserMacro(world, "", spec, "game-key");
        filter.setMacros(world);
        // Exercise the existing on-disk preference format, not a new settings store.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto path = fugue::skin::userMacrosPath(dir.path().toStdString(), "");
        QVERIFY(fugue::skin::saveUserMacros(path, global));
        filter.setUserMacros({}, fugue::skin::loadUserMacros(path));
        QSignalSpy macros(&filter, &FocusFilter::macroFired);
        auto* input = new QLineEdit("unchanged", &w);
        input->show();
        input->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), input);
        KeySink seen;
        input->installEventFilter(&seen);
        chord(input, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(w.clientMenu()->isVisible());
        chord(w.clientMenu(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(!w.clientMenu()->isVisible());
        QCOMPARE(seen.keys, 0);
        QCOMPARE(input->text(), QString("unchanged"));
        QCOMPARE(macros.count(), 0);
        filter.setUserMacros(game, global);
        chord(input); // configured shortcut replaces the default
        QVERIFY(!w.clientMenu()->isVisible());
        chord(input, Qt::Key_F, Qt::ControlModifier); // game-specific replaces global
        QVERIFY(!w.clientMenu()->isVisible());
        macros.clear();
        sendKey(input, QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(w.clientMenu()->isVisible());
        // Changing config while held still consumes the original key release.
        filter.setUserMacros({}, global);
        sendKey(w.clientMenu(), QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier);
        QCOMPARE(macros.count(), 0);
        chord(w.clientMenu(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(!w.clientMenu()->isVisible());
        // A macro editor / other top-level dialog must still be able to capture the configured chord.
        QDialog dialog(&w);
        dialog.setProperty("fugueZone", "other");
        QLineEdit editor(&dialog);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        dialog.activateWindow();
        editor.setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), &editor);
        KeySink dialogKeys;
        editor.installEventFilter(&dialogKeys);
        chord(&editor, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!w.clientMenu()->isVisible());
        QCOMPARE(dialogKeys.keys, 2);
        dialog.close();
        input->hide();
        w.activateWindow();
        // Construct after installing the router: the browser's own Find handler must not steal a custom CTRL+F.
        auto* browser = new BrowserPane(&w);
        browser->setGeometry(0, 40, 400, 220);
        browser->findAllowed = [] { return true; };
        QSignalSpy loaded(browser, &QWebEngineView::loadFinished);
        browser->showHtml("<input autofocus><script>window.keys=[];"
                          "onkeydown=e=>keys.push(e.key);onkeyup=e=>keys.push(e.key)</script>");
        browser->show();
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.empty(), 15000);
        QVERIFY(loaded.first().first().toBool());
        browser->setFocus();
        QTRY_VERIFY(QApplication::focusWidget() && browser->isAncestorOf(QApplication::focusWidget()));
        QWidget* pageFocus = QApplication::focusWidget();
        KeySink browserKeys;
        pageFocus->installEventFilter(&browserKeys);
        chord(pageFocus, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(w.clientMenu()->isVisible());
        chord(w.clientMenu(), Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(!w.clientMenu()->isVisible());
        QVERIFY(!browser->findOpen());
        QCOMPARE(browserKeys.keys, 0);
        QCOMPARE(macros.count(), 0);
        bool evaluated = false;
        QStringList pageKeys;
        browser->page()->runJavaScript("window.keys", [&](const QVariant& result) {
            pageKeys = result.toStringList(); evaluated = true;
        });
        QTRY_VERIFY(evaluated);
        QVERIFY(pageKeys.empty());
    }
    void initiallyMenuless() {
        SkinWindow w;
        w.resize(400, 300);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        for (int load = 0; load < 3; ++load) {
            QVERIFY(w.loadSkin(menuSkin(false)).isEmpty());
            for (int snapshot = 0; snapshot < 3; ++snapshot) {
                w.syncFromLive();
                QCOMPARE(w.menuBarHeight(), 0);
                QVERIFY(!w.menuBar() || w.menuBar()->isHidden());
                QCOMPARE(w.designSize(), QSize(320, 240));
                QCOMPARE(w.controlWidget("main.content")->geometry(), QRect(0, 0, 400, 300));
            }
        }
    }
};

class ClientMenuNativeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const auto platform = QGuiApplication::platformName();
        QVERIFY2(platform != "offscreen" && platform != "minimal",
                 "Popup mouse grabs require a native window-system run");
    }
    void outsideClickDismissesPopup() {
        SkinWindow w;
        QVERIFY(w.loadSkin(menuSkin(false)).isEmpty());
        w.resize(500, 400);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.activateWindow();
        FocusFilter filter;
        filter.setClientWindow(&w);
        qApp->installEventFilter(&filter);
        chord(&w);
        auto* popup = w.clientMenu();
        QTRY_VERIFY(popup->isVisible());
        // A popup receives grabbed mouse events even when their position is outside its bounds.
        QTest::mouseClick(popup->windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(popup->width() + 40, popup->height() + 40));
        QTRY_VERIFY(!popup->isVisible());
    }
};

template<class Test>
static int runClientMenuTest(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    Test test;
    return QTest::qExec(&test, argc, argv);
}

int FUGUE_TEST_ENTRY(int argc, char** argv) {
    return runClientMenuTest<ClientMenuTest>(argc, argv);
}

int fugue_run_clientmenu_native(int argc, char** argv) {
    return runClientMenuTest<ClientMenuNativeTest>(argc, argv);
}
#include "clientmenu_test.moc"

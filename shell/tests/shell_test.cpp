// shell/ unit tests (offscreen QPA): skin -> widget geometry and the focus table.
#include <QApplication>
#include <QLineEdit>
#include <QFile>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>
#include <map>
#include "tests/testmain.h"
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/skinwindow.h"

using namespace fugue::shell;

static const char* kSkin = R"(macro "m"
	elem "q"
		name = "Q"
		command = "qmacro"
	elem "ck"
		name = "CTRL+K"
		command = "ctrlk"
window "main"
	elem "main"
		type = MAIN
		size = 560x560
		is-default = true
		macro = "m"
		title = "t"
	elem "map"
		type = MAP
		pos = 0,0
		size = 480x480
		anchor1 = 0,0
		anchor2 = 0,0
		icon-size = 32
	elem "input"
		type = INPUT
		pos = 0,500
		size = 560x24
		anchor1 = 0,100
		anchor2 = 100,100
	elem "browser"
		type = BROWSER
		pos = 0,524
		size = 560x36
		anchor1 = 0,100
		anchor2 = 100,100
)";

class ShellTest : public QObject {
    Q_OBJECT
    SkinWindow* win = nullptr;
    FocusFilter* filt = nullptr;
private slots:
    void initTestCase() {
        win = new SkinWindow;
        QVERIFY2(win->loadSkin(kSkin).isEmpty(), "skin parses");
        win->resize(win->designSize());
        win->show();
        QVERIFY(QTest::qWaitForWindowExposed(win));
        filt = new FocusFilter;
        auto t = win->macros();
        filt->setMacros(t.sets.at(t.active));
        qApp->installEventFilter(filt);
    }
    void cleanupTestCase() {
        qApp->removeEventFilter(filt);
        delete filt;
        delete win;
    }

    void paneClipsButtonWithoutResizingIt() {
        QFile file(QFINDTESTDATA("../../tests/data/splitter-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        SkinWindow window;
        QVERIFY(window.loadSkin(file.readAll().toStdString()).isEmpty());
        window.resize(window.designSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* button = window.findChild<QPushButton*>("row.menu");
        QVERIFY(button);
        QCOMPARE(button->height(), 6);  // 9px pane, y=5, 27px design button in a 30px design pane
        QCOMPARE(button->mask().boundingRect(), QRect(0, 0, button->width(), 4));
        // The clipped tail must not receive input in the adjacent pane.
        const QPoint tail = button->mapTo(&window, QPoint(1, 5));
        QVERIFY(window.childAt(tail) != button);
        QVERIFY(window.childAt(button->mapTo(&window, QPoint(1, 1))) == button);
        window.live()->winset("nested.split", "splitter=0");
        window.syncFromLive();
        QVERIFY(!button->isVisible());
        window.live()->winset("nested.split", "splitter=50");
        window.syncFromLive();
        QVERIFY(button->isVisible());
        QCOMPARE(button->height(), 216);
        QCOMPARE(button->mask().boundingRect().height(), 214);
        // A smaller design button fully inside the pane removes the previous mask.
        window.resize(640, 880 + window.statusBarHeight());
        window.live()->winset("row.menu", "size=88x1");
        window.syncFromLive();
        QVERIFY(button->mask().isEmpty());
        window.close();
        win->activateWindow();
        QVERIFY(QTest::qWaitFor([&] { return win->isActiveWindow(); }));
    }
    void singleChildBrowserFillsPane() {
        QFile file(QFINDTESTDATA("../../tests/data/child-sizing-world/skin.dmf"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        SkinWindow window;
        QVERIFY(window.loadSkin(file.readAll().toStdString()).isEmpty());
        window.resize(window.designSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* browser = window.findChild<BrowserPane*>("chat.browser");
        QVERIFY(browser);
        QCOMPARE(browser->geometry(), QRect(0, 160, 300, 150));
        QVERIFY(!window.splitterHandle("main.nested"));
        window.live()->winset("main.nested", "size=420x210");
        window.syncFromLive();
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 210));
        window.live()->winset("main.nested", "right=extra_right");
        window.syncFromLive();
        // Default axis is horizontal; attaching a second pane brings the divider back.
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 103));
        QVERIFY(window.splitterHandle("main.nested"));
        window.live()->winset("main.nested", "right=\"\"");
        window.syncFromLive();
        QCOMPARE(browser->geometry(), QRect(0, 160, 420, 210));
        QVERIFY(window.splitterHandle("main.nested")->isHidden());
        window.close();
        win->activateWindow();
        QVERIFY(QTest::qWaitFor([&] { return win->isActiveWindow(); }));
    }
    // Runtime winsets of the main window's size, pos and is-maximized and of its controls' sizes and places, step by step
    // as tests/data/winset-size-world/world.dm sends them, against what stock DreamSeeker 516.1689 answered (stock.log
    // there, at 96 dpi, where its pixels are this client's logical ones) and the widgets those answers describe.
    void runtimeSizeWinsets() {
        QFile skin(QFINDTESTDATA("../../tests/data/winset-size-world/world.dmf"));
        QFile log(QFINDTESTDATA("../../tests/data/winset-size-world/stock.log"));
        QVERIFY(skin.open(QIODevice::ReadOnly) && log.open(QIODevice::ReadOnly));
        std::map<QString, std::map<QString, QString>> stock;
        for (const QString& line : QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const QStringList words = line.trimmed().split(' ');
            for (const QString& word : words.mid(1)) {
                const auto eq = word.indexOf('=');
                stock[words[0].chopped(1)][word.left(eq)] = word.mid(eq + 1);
            }
        }
        SkinWindow window;
        // The world's Login() sizes the window before this client has the skin: the winset waits for the skin, and the
        // window opens at that size rather than the skin's design size, as stock's does.
        window.live()->winset("main", "size=240x240");
        QVERIFY(window.loadSkin(skin.readAll().toStdString()).isEmpty());
        window.resize(window.designSize());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.statusBarHeight(), 19);  // stock's strip at 96 dpi
        const struct {
            const char *label, *control, *params;
        } steps[] = {{"start", "", ""},
                     {"win288", "", "main.size=288x288"},
                     {"map160", "", "map.size=160x160"},
                     {"win224", "", "main.size=224x224"},
                     {"win288b", "", "main.size=288x288"},
                     {"children", "", "fixed.size=64x64;half.size=100x20"},
                     {"map0x0", "", "map.size=0x0"},
                     {"map100x0", "", "map.size=100x0"},
                     {"reset", "", "main.size=224x224;map.size=224x224"},
                     {"fixture", "", "main.size=288x288;map.size=288x288"},
                     {"ctlwin", "main", "size=256x256"},
                     {"ctlmap", "map", "size=128x128"},
                     {"fixed0x0", "", "fixed.size=0x0"},
                     {"win224c", "", "main.size=224x224"},
                     {"fixedpos", "", "fixed.size=32x32;fixed.pos=10,20"},
                     {"halfpos", "", "half.pos=8,100"},
                     {"halfboth", "", "half.pos=4,150;half.size=50x30"},
                     {"win288c", "", "main.size=288x288"},
                     {"mainpos", "", "main.pos=100,120"},
                     {"rtnew", "rt", "parent=main;type=map;pos=20,40;size=100x100;anchor1=0,0;anchor2=100,100"},
                     {"win224d", "", "main.size=224x224"},
                     {"rtsize", "", "rt.size=80x60"},
                     {"rtpos", "", "rt.pos=10,10"},
                     {"win256", "", "main.size=256x256"},
                     {"kidsize", "", "kid.size=96x96"},
                     {"panesize", "", "pane.size=40x40"},
                     {"pl0x0", "", "pl.size=0x0"},
                     {"loosesize", "", "loose.size=70x30"},
                     {"fltsize", "", "flt.size=120x80"},
                     {"max", "", "main.is-maximized=true"},
                     {"maxsize", "", "main.size=300x300"},
                     {"unmax", "", "main.is-maximized=false"}};
        auto winget = [&](const char* id, const char* param) {
            std::string out;
            return window.live()->winget(id, param, out) ? QString::fromStdString(out) : QString();
        };
        auto size = [](const QSize& s) { return QString("%1x%2").arg(s.width()).arg(s.height()); };
        auto client = [&] {
            return QSize(window.width(), window.height() - window.statusBarHeight() - window.menuBarHeight());
        };
        bool moved = false, sizedFloat = false;
        for (const auto& step : steps) {
            if (*step.params) window.live()->winset(step.control, step.params);
            window.syncFromLive();
            const QString label = step.label;
            const auto& want = stock.at(label);
            const bool maximized = label == "max" || label == "maxsize";
            QTRY_COMPARE(window.isMaximized(), maximized);

            QCOMPARE(winget("main", "is-maximized"), want.at("main.is-maximized"));
            // While maximized `size` is the size the window is restored to, `inner-size` the real client area.
            QTRY_COMPARE(winget("main", "size"), want.at("main.size"));
            QTRY_COMPARE(winget("main", "inner-size"), maximized ? size(client()) : want.at("main.inner-size"));
            if (maximized) continue;  // the controls follow a client area as big as this screen
            // the window's client area, the menu bar and status strip not counted
            QTRY_COMPARE(size(client()), want.at("main.size"));
            // outer-size is this client's own frame, not stock's
            QTRY_COMPARE(winget("main", "outer-size"), size(window.frameGeometry().size()));
            moved = moved || label == "mainpos";
            if (moved) {
                QCOMPARE(winget("main", "pos"), want.at("main.pos"));
                QTRY_COMPARE(QString("%1,%2").arg(window.frameGeometry().x()).arg(window.frameGeometry().y()),
                             want.at("main.pos"));
            }
            for (const char* id : {"map", "fixed", "half", "kid", "pl", "rt"}) {
                const QString key = id;
                QCOMPARE(winget(id, "size"), want.at(key + ".size"));
                QCOMPARE(winget(id, "pos"), want.at(key + ".pos"));
                if (key == "kid" || key == "rt") continue;
                auto* widget = window.findChild<QWidget*>((key == "pl" ? "pane." : "main.") + key);
                QVERIFY(widget);
                if (key == "pl") {  // its pos is in the pane
                    QCOMPARE(size(widget->size()), want.at(key + ".size"));
                    continue;
                }
                // The widget is what shows of the control: clipped to the client area and the status strip below it,
                // which a control can cover as in stock.
                const QStringList p = want.at(key + ".pos").split(','), s = want.at(key + ".size").split('x');
                const QRect shown = QRect(p[0].toInt(), p[1].toInt(), s[0].toInt(), s[1].toInt()) &
                                    QRect(QPoint(0, 0), client() + QSize(0, window.statusBarHeight()));
                QCOMPARE(widget->geometry().translated(0, -window.menuBarHeight()), shown);
            }
            // The floating window from its first size winset on: before it, stock answers 120x100 for its unshown
            // 100x100 window (no narrower than the system lets a captioned window be), this client the .dmf size.
            sizedFloat = sizedFloat || label == "fltsize";
            for (const char* id : {"pane", "loose", "ll", "flt", "fl"})
                if (sizedFloat || id[0] != 'f') QCOMPARE(winget(id, "size"), want.at(QString(id) + ".size"));
        }
        window.close();
        win->activateWindow();
        QVERIFY(QTest::qWaitFor([&] { return win->isActiveWindow(); }));
    }
    void designSizeAndMapRect() {
        // the MAIN window states no `statusbar`, so it gets DreamSeeker's default strip below the client area
        QCOMPARE(win->statusBarHeight(), 19);
        QCOMPARE(win->designSize(), QSize(560, 560 + 19));
        QCOMPARE(win->map()->geometry(), QRect(0, 0, 480, 480));
    }
    void relayoutOnResize() {
        win->resize(800, 700);
        QCOMPARE(win->map()->geometry().topLeft(), QPoint(0, 0));  // anchors none: designed extent kept
        QCOMPARE(win->map()->size(), QSize(480, 480));
        auto* in = win->findChild<QLineEdit*>();
        QVERIFY(in);
        QCOMPARE(in->width(), 800);                 // anchored 0%..100% horizontally
        QCOMPARE(in->y(), 500 + (700 - 19 - 560));  // anchored to the client area's bottom edge (above the status bar)
        win->resize(560, 560);
        QCOMPARE(in->width(), 560);
    }
    void focusTable() {
        QSignalSpy spy(filt, &FocusFilter::macroFired);
        auto* in = win->findChild<QLineEdit*>();
        auto* browser = win->findChild<BrowserPane*>();
        QVERIFY(in && browser);
        // map focused: a plain bound key fires a macro
        win->map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == win->map(); }));
        QTest::keyClick(win->map(), Qt::Key_Q);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toString(), QString("qmacro"));
        // input focused: plain keys type (no macro), Ctrl-combos fire
        in->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == in; }));
        QTest::keyClick(in, Qt::Key_Q);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(in->text(), QString("q"));
        QTest::keyClick(in, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toString(), QString("ctrlk"));
        QCOMPARE(in->text(), QString("q"));  // the macro swallowed it
        // browser focused: the page owns its keys
        browser->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return FocusFilter::zoneOf(qApp->focusWidget()) == Zone::Browser; }));
        QTest::keyClick(qApp->focusWidget(), Qt::Key_Q);
        QTest::keyClick(qApp->focusWidget(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(spy.count(), 2);
    }
    void macroSpecNames() {
        QKeyEvent e(QEvent::KeyPress, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&e)), QString("CTRL+SHIFT+WEST"));
        QKeyEvent m(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&m)), QString(""));
        QKeyEvent c(QEvent::KeyPress, Qt::Key_Clear, Qt::KeypadModifier);
        QCOMPARE(QString::fromStdString(macroSpecFor(&c)), QString("CENTER"));
    }
};

int FUGUE_TEST_ENTRY(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    ShellTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "shell_test.moc"

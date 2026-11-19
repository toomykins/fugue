// shell/ unit tests (offscreen QPA): skin -> widget geometry, the map widget's pixel path, and the focus table.
#include <QApplication>
#include <QLineEdit>
#include <QSignalSpy>
#include <QtTest>
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

    void designSizeAndMapRect() {
        // the MAIN window states no `statusbar`, so it gets DreamSeeker's default strip below the client area
        QCOMPARE(win->statusBarHeight(), 18);
        QCOMPARE(win->designSize(), QSize(560, 560 + 18));
        QCOMPARE(win->mapControlRect(), QRect(0, 0, 480, 480));
    }
    void relayoutOnResize() {
        win->resize(800, 700);
        QCOMPARE(win->mapControlRect().topLeft(), QPoint(0, 0));  // anchors none: designed extent kept
        QCOMPARE(win->mapControlRect().size(), QSize(480, 480));
        auto* in = win->findChild<QLineEdit*>();
        QVERIFY(in);
        QCOMPARE(in->width(), 800);                 // anchored 0%..100% horizontally
        QCOMPARE(in->y(), 500 + (700 - 18 - 560));  // anchored to the client area's bottom edge (above the status bar)
        win->resize(560, 560);
        QCOMPARE(in->width(), 560);
    }
    void mapPaintsFramePixelForPixel() {
        QImage f(480, 480, QImage::Format_RGBX8888);
        for (int y = 0; y < 480; y++)
            for (int x = 0; x < 480; x++) f.setPixel(x, y, qRgb(x & 255, y & 255, (x ^ y) & 255));
        win->setMapFrame(f);
        QImage g = win->grab().toImage().convertToFormat(QImage::Format_RGB32).copy(win->mapControlRect());
        QCOMPARE(g, f.convertToFormat(QImage::Format_RGB32));  // scale 1: an exact blit
        QCOMPARE(win->map()->scale(), 1);
    }
    void integerZoomIsPointSampled() {
        win->map()->setIconSize(64);  // zoom 2
        QImage f(100, 100, QImage::Format_RGBX8888);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 100; x++) f.setPixel(x, y, qRgb(x * 2, y * 2, 77));
        win->setMapFrame(f);
        QCOMPARE(win->map()->scale(), 2);
        QImage g = win->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QRect ir = win->map()->imageRect().translated(win->mapControlRect().topLeft());
        QCOMPARE(ir.size(), QSize(200, 200));
        for (int y = 0; y < 200; y += 7)
            for (int x = 0; x < 200; x += 5)  // every sampled pixel is a source pixel, no blends
                QCOMPARE(g.pixel(ir.x() + x, ir.y() + y), f.pixel(x / 2, y / 2) | 0xff000000u);
        win->map()->setIconSize(32);
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

int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    ShellTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "shell_test.moc"

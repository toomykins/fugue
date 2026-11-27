#include <QtTest>
#include "tests/testmain.h"
#include <QTemporaryDir>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include "shell/screenshot.h"
#include "shell/skinwindow.h"
#include "shell/focusfilter.h"
#include "shell/skinsync.h"
#include "shell/inputrouter.h"
#include "app/defaultskin.h"
using namespace fugue::shell;

class ScreenshotTest : public QObject {
    Q_OBJECT
private slots:
    void naming() {
        const QDateTime when(QDate(2000, 1, 2), QTime(2, 46, 29));
        QCOMPARE(screenshotBaseName("shotworld", when), QString("shotworld 2000-01-02 024629"));
        QCOMPARE(screenshotBaseName("/vg/ Sta:tion* 13?", when), QString("vg Station 13 2000-01-02 024629"));
        QCOMPARE(screenshotBaseName(" \t\\/:*?\"<>|\n ", when), QString("screenshot 2000-01-02 024629"));
        QCOMPARE(withPngSuffix("shot"), QString("shot.png"));
        QCOMPARE(withPngSuffix("shot.PNG"), QString("shot.PNG"));
        QCOMPARE(withPngSuffix(""), QString());
    }
    void nativeOpaquePngAndCollisions() {
        QTemporaryDir tmp; QVERIFY(tmp.isValid());
        const QDateTime when(QDate(2000, 1, 2), QTime(2, 46, 29));
        const auto dir = tmp.filePath("screenshots");
        QImage frame(7, 5, QImage::Format_RGBA8888); frame.fill(QColor(12, 34, 56, 127));
        frame.setPixelColor(0, 0, Qt::red); frame.setPixelColor(6, 4, Qt::blue);
        const auto first = screenshotAutoPath(dir, "world", when);
        QString error;
        QVERIFY2(saveScreenshot(frame, first, &error), qPrintable(error));
        const QImage loaded(first);
        QCOMPARE(loaded.size(), frame.size());
        QVERIFY(!loaded.hasAlphaChannel());
        QCOMPARE(loaded.pixelColor(0, 0), QColor(Qt::red));
        QCOMPARE(loaded.pixelColor(6, 4), QColor(Qt::blue));
        QCOMPARE(loaded.pixelColor(2, 2), QColor(12, 34, 56));
        const auto second = screenshotAutoPath(dir, "world", when);
        QVERIFY(second.endsWith(" 2.png"));
        QVERIFY(saveScreenshot(frame, second, &error));
        QVERIFY(screenshotAutoPath(dir, "world", when).endsWith(" 3.png"));
        QVERIFY(!saveScreenshot({}, second, &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!saveScreenshot(frame, "", &error));
        QVERIFY(!saveScreenshot(frame, tmp.path(), &error)); QVERIFY(!error.isEmpty());
        // A regular file blocks creation of the requested parent directory.
        QFile blocker(tmp.filePath("file")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        QVERIFY(!saveScreenshot(frame, blocker.fileName() + "/shot.png", &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(saveScreenshot(frame, second, &error)); QVERIFY(error.isEmpty());
        QCOMPARE(QImage(first), loaded); // repeat auto capture never overwrote the first
    }
    void defaultControls() {
        SkinWindow window;
        QVERIFY(window.loadSkin(fugue::app::kDefaultSkin).isEmpty());
        // Run the built-in macros through the actual focus filter.
        FocusFilter filter;
        applySkinMacros(window, filter);
        qApp->installEventFilter(&filter);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* input = window.controlWidget("mainwindow.input"); QVERIFY(input);
        input->setFocus(); QTRY_COMPARE(qApp->focusWidget(), input);
        QSignalSpy fired(&filter, &FocusFilter::macroFired);
        QTest::keyClick(input, Qt::Key_F2);
        QTest::keyClick(input, Qt::Key_F2, Qt::ShiftModifier);
        QCOMPARE(fired.size(), 2);
        QCOMPARE(fired[0][1].toString(), QString(".screenshot auto"));
        QCOMPARE(fired[1][1].toString(), QString(".screenshot"));
        qApp->removeEventFilter(&filter);
        QSignalSpy commands(&window, &SkinWindow::menuCommand);
        auto* file = window.menuBar()->actions().front()->menu(); QVERIFY(file);
        int shots = 0;
        for (auto* action : file->actions()) {
            if (!action->text().contains("screenshot", Qt::CaseInsensitive)) continue;
            action->trigger(); ++shots;
        }
        QCOMPARE(shots, 2); QCOMPARE(commands.size(), 2);
        QCOMPARE(commands[0][0].toString(), QString(".screenshot auto"));
        QCOMPARE(commands[1][0].toString(), QString(".screenshot"));
        for (const auto& command : commands)
            QCOMPARE(routeCommand(command[0].toString().toStdString(), "", false).kind, Route::Screenshot);
    }
};
FUGUE_TEST_MAIN(ScreenshotTest)
#include "screenshot_test.moc"

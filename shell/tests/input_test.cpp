// Shell input wiring: key bindings, command routing and macro dialogs (offscreen QPA).
#include <QApplication>
#include <QCursor>
#include <QLineEdit>
#include <QMenu>
#include <QSignalSpy>
#include <QtTest>
#include "tests/testmain.h"
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/inputrouter.h"
#include <QTemporaryDir>
#include "shell/macrodialog.h"
#include <QCheckBox>
#include <QMenuBar>
#include <QToolButton>
#include <QTreeWidget>
#include "net/input.h"
#include "shell/skinfont.h"
#include "shell/skinwindow.h"
#include <QLabel>

using namespace fugue::shell;

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
        QTest::newRow(".north with an argument") << ".north 3" << false << int(Route::Unsupported) << 0 << true << ".north 3";
        QTest::newRow("macro swap") << ".winset \"mainwindow.macro=hotkeymode mapwindow.map.focus=true\"" << false
                                    << int(Route::SetMacroSet) << 0 << true << "hotkeymode";
        QTest::newRow("winset no macro") << ".winset \"rpanewindow.left=;\"" << false << int(Route::Winset) << 0 << true
                                         << "rpanewindow.left=;";
        QTest::newRow("winset unquoted") << ".winset rpane.lbl.text=hi" << false << int(Route::Winset) << 0 << true
                                         << "rpane.lbl.text=hi";
        QTest::newRow("quit") << ".quit" << false << int(Route::Quit) << 0 << true << "";
        QTest::newRow("reconnect") << ".Reconnect" << false << int(Route::Reconnect) << 0 << true << "";
        QTest::newRow("screenshot dialog") << ".screenshot" << false << int(Route::Screenshot) << 0 << true << "";
        QTest::newRow("screenshot auto") << ".screenshot auto" << false << int(Route::Screenshot) << 0 << true
                                         << "auto";
        QTest::newRow("screenshot case and whitespace")
            << "  .Screenshot\t AUTO " << false << int(Route::Screenshot) << 0 << true << "auto";
        QTest::newRow("unknown dot cmd is NOT a verb")
            << ".profile" << false << int(Route::Unsupported) << 0 << true << ".profile";
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
    void localOutputRouting() {
        const std::string payload = "%7B%22type%22%3A%22toggle%22%2C%22payload%22%3Anull%7D";
        const auto r = routeCommand(".output mapwindow.escape_menu:update " + payload, "", false);
        QCOMPARE(r.kind, Route::Output);
        QCOMPARE(r.text, std::string("mapwindow.escape_menu:update"));
        QCOMPARE(r.arg, payload);  // not decoded until the page-call argument split
        const auto spaced = routeCommand("  .OuTpUt\tother.control:api.receive\t a%26b&%2526&  ", "", false);
        QCOMPARE(spaced.kind, Route::Output);
        QCOMPARE(spaced.text, std::string("other.control:api.receive"));
        QCOMPARE(spaced.arg, std::string("a%26b&%2526&"));
        const auto empty = routeCommand(".output page:update", "", false);
        QCOMPARE(empty.kind, Route::Output);
        QVERIFY(empty.arg.empty());
        QCOMPARE(routeWorldCommand(".output page:update x").kind, Route::Output);
        QCOMPARE(routeWorldCommand(".output").kind, Route::Unsupported);  // malformed, still never a verb
        QCOMPARE(routeCommand(".outputting x", "", false).kind, Route::Unsupported);
        const auto lines = macroCommandLines(
            ".winset \"mapwindow.escape_menu.is-visible=true\"\r\n.output mapwindow.escape_menu:update " + payload +
            "\r\nReset-Held-Keys");
        QCOMPARE(lines.size(), size_t(3));
        QCOMPARE(routeCommand(lines[0], "", false).kind, Route::Winset);
        QCOMPARE(routeCommand(lines[1], "", false).kind, Route::Output);
        QCOMPARE(routeCommand(lines[2], "", false).kind, Route::Command);
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
    // Stock 516.1688 macro-params-world: top-left control pixels, not bottom-left icon
    // pixels or letterbox-adjusted coordinates. Qt retains its logical-pixel sizing contract.
    void mapMacroParameters() {
        struct RestoreFocus {
            SkinWindow* w;
            ~RestoreFocus() {
                w->activateWindow();
                w->map()->setFocus();
            }
        } focus{win};
        SkinWindow local;
        QVERIFY(local.loadSkin(kSkin).isEmpty());
        local.resize(local.designSize());
        local.show();
        QVERIFY(QTest::qWaitForWindowExposed(&local));
        local.live()->winset("map", "pos=30,20;size=300x200");
        local.syncFromLive();
        QWidget* map = local.map();
        QVERIFY(map);
        QCOMPARE(map->size(), QSize(300, 200));
        struct RestoreCursor {
            QPoint pos = QCursor::pos();
            ~RestoreCursor() { QCursor::setPos(pos); }
        } restore;
        auto expand = [&](const char* command) {
            return QString::fromStdString(local.expandMapCommandParams(command));
        };
        for (const QPoint point :
             {QPoint(100, 50), QPoint(0, 0), QPoint(299, 199), QPoint(-10, -10), QPoint(119, 241)}) {
            QCursor::setPos(map->mapToGlobal(point));
            QCOMPARE(expand("[[map.mouse-pos]] [[map.size]]"), QString("%1 %2 300 200").arg(point.x()).arg(point.y()));
            QCOMPARE(expand("[[main.map.mouse-pos]] [[MAP.size]] [[map.size]]"),
                     QString("%1 %2 300 200 300 200").arg(point.x()).arg(point.y()));
            QCOMPARE(expand(R"("[[map.mouse-pos]]" "[[main.map.size]]")"),
                     QString("\"%1,%2\" \"300,200\"").arg(point.x()).arg(point.y()));
        }
        QCOMPARE(expand("KeyDown [[*]] [[map.size]]"), QString("KeyDown [[*]] 300 200"));
        // Preserve unsupported parameters/controls rather than inventing coordinates.
        const char* unsupported = "[[missing.size]] [[input.size]] [[map.unknown]] [[map.size";
        QCOMPARE(expand(unsupported), QString::fromLatin1(unsupported));
        QCOMPARE(expand(R"(Record \"[[map.size]]\" "[[map.size]]")"),
                 QString::fromLatin1(R"(Record \"300 200\" "300,200")"));
        local.live()->winset("map", "pos=60,40;size=360x240");
        local.syncFromLive();
        QCursor::setPos(map->mapToGlobal(QPoint(25, 35)));
        QCOMPARE(expand("[[map.mouse-pos]] [[map.size]]"), QString("25 35 360 240"));
    }
    void fiveArgumentKeyMacrosReachVerbPackets() {
        using namespace fugue;
        struct RestoreFocus {
            SkinWindow* w;
            ~RestoreFocus() {
                w->activateWindow();
                w->map()->setFocus();
            }
        } focus{win};
        SkinWindow local;
        QVERIFY(local.loadSkin(kSkin).isEmpty());
        local.resize(local.designSize());
        local.show();
        QVERIFY(QTest::qWaitForWindowExposed(&local));
        local.live()->winset("map", "pos=30,20;size=300x200");
        local.syncFromLive();
        FocusFilter filter;
        skin::MacroSet macros;
        skin::editUserMacro(macros, "", "Any", "KeyDown [[*]] [[map.mouse-pos]] [[map.size]]");
        skin::editUserMacro(macros, "", "Any+UP", "KeyUp [[*]] [[map.mouse-pos]] [[map.size]]");
        filter.setMacros(macros);
        qApp->installEventFilter(&filter);
        local.map()->setFocus();
        QVERIFY(QTest::qWaitFor([&] { return qApp->focusWidget() == local.map(); }));
        struct RestoreCursor {
            QPoint pos = QCursor::pos();
            ~RestoreCursor() { QCursor::setPos(pos); }
        } restore;
        std::vector<Route> routed;
        connect(&filter, &FocusFilter::macroFired, &local, [&](const QString& key, const QString& command, bool up) {
            for (const auto& line : macroCommandLines(command.toStdString()))
                routed.push_back(routeCommand(local.expandMapCommandParams(line), key.toStdString(), up));
        });
        QCursor::setPos(local.map()->mapToGlobal(QPoint(100, 50)));
        QTest::keyPress(local.map(), Qt::Key_W);
        QCursor::setPos(local.map()->mapToGlobal(QPoint(-10, -10)));
        QTest::keyRelease(local.map(), Qt::Key_W);
        QTest::keyClick(local.map(), Qt::Key_Up);
        QCOMPARE(routed.size(), size_t(4));
        for (bool wide : {false, true}) {
            net::InputModel input;
            input.setWideVerbIds(wide);
            for (uint16_t id : {uint16_t(10), uint16_t(11)}) {
                Bytes ad;
                if (wide)
                    putLe32(ad, id);
                else
                    putLe16(ad, id);
                const std::string name = id == 10 ? "KeyDown" : "KeyUp";
                ad.insert(ad.end(), name.begin(), name.end());
                ad.insert(ad.end(), {0, 0, 0, 0, 8, 0, 5});   // name/desc/category, head/src/flags, argc
                for (uint32_t mask : {4u, 8u, 8u, 8u, 8u}) {  // text, four numbers
                    putLe32(ad, mask);
                    ad.insert(ad.end(), {0, 0});
                }
                input.onFrame(0x0011, ad);
            }
            QCOMPARE(input.verbs().size(), size_t(2));
            auto source = [](uint32_t) { return std::vector<net::SrcCandidate>{{{8, 0}, "", false}}; };
            // The original failure is a local refusal, not a server movement decision.
            const auto bad = input.verbCommand("KeyDown \"W\" [[map.mouse-pos]] [[map.size]]", true, source);
            QVERIFY(!bad.error.empty());
            QVERIFY(bad.frames.empty());
            for (size_t i = 0; i < routed.size(); ++i) {
                QCOMPARE(routed[i].kind, Route::Command);
                const auto result = input.verbCommand(routed[i].text, true, source);
                QVERIFY2(result.error.empty(), result.error.c_str());
                QVERIFY(!result.missing && !result.resolve);
                QCOMPARE(result.frames.size(), size_t(1));
                std::vector<proto::enc::VerbArg> args(5);
                args[0].str = i < 2 ? "W" : "North";
                const float numbers[] = {i == 0 ? 100.f : -10.f, i == 0 ? 50.f : -10.f, 300.f, 200.f};
                for (size_t n = 0; n < 4; ++n) {
                    args[n + 1].tag = 1;
                    args[n + 1].num = numbers[n];
                }
                QCOMPARE(result.frames[0].body, proto::enc::encodeVerb(uint16_t(10 + i % 2), 8, 0, args, wide));
            }
        }
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
    // "Options & Messages" and the Client menu route local commands, never server verbs.
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
        QCOMPARE(items, QStringList(
                            {"&Screenshot...", "&Reconnect", "-", "&Macros...", "&Performance metrics", "-", "&Quit"}));
        QSignalSpy cmd(&w, &SkinWindow::menuCommand);
        QAction* metrics = client->actions().at(4);
        QVERIFY(metrics->isCheckable());
        QVERIFY(!metrics->isChecked());
        metrics->trigger();
        QVERIFY(w.mapMetricsVisible());
        QVERIFY(metrics->isChecked());
        QCOMPARE(cmd.count(), 0);  // local toggle never becomes a server command
        w.setMapMetricsVisible(false);
        QVERIFY(!metrics->isChecked());
        sys->actions().at(0)->trigger();
        client->actions().at(3)->trigger();
        client->actions().at(1)->trigger();
        client->actions().at(0)->trigger();
        QCOMPARE(cmd.count(), 4);
        QCOMPARE(int(routeCommand(cmd.at(0).at(0).toString().toStdString(), "", false).kind), int(Route::Options));
        QCOMPARE(int(routeCommand(cmd.at(1).at(0).toString().toStdString(), "", false).kind), int(Route::Macros));
        QCOMPARE(int(routeCommand(cmd.at(2).at(0).toString().toStdString(), "", false).kind), int(Route::Reconnect));
        QCOMPARE(int(routeCommand(cmd.at(3).at(0).toString().toStdString(), "", false).kind), int(Route::Screenshot));
        QCOMPARE(routeWorldCommand(".screenshot auto").kind, Route::Screenshot);
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

int FUGUE_TEST_ENTRY(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    InputTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "input_test.moc"

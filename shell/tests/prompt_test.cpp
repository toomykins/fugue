// PromptHost (alert plus every input() form, one outcome per request, queued one at a time), the INFO control as Qt widgets (tabs, rows,
// tab/row/verb clicks), and paste (Ctrl+V, Shift+Insert, middle-click primary selection) in every key-taking widget this client builds.
// Reply bytes are checked in tests/prompt_stat_test.cpp.
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QtTest>
#include "model/prompt.h"
#include "net/input.h"
#include "shell/browserpane.h"
#include "shell/focusfilter.h"
#include "shell/paste.h"
#include "shell/prompthost.h"
#include "shell/skinwindow.h"
#include "shell/statpanelwidget.h"

using namespace fugue::shell;
#include "prompt_skin.h"

namespace {
PromptRequest alertReq(quint32 id, QStringList buttons) { PromptRequest r; r.alert = true; r.id = id; r.title = "Fugue"; r.message = "Pick one"; r.buttons = buttons; return r; }
PromptRequest inputReq(quint32 id, quint32 mask, const QString& def) { PromptRequest r; r.alert = false; r.id = id; r.title = "Fugue"; r.message = "Pick one"; r.def = def; r.typeMask = mask; return r; }
void key(QWidget* w, int k, Qt::KeyboardModifiers m) { QTest::keyClick(w, Qt::Key(k), m); }
}

class PromptTest : public QObject {
    Q_OBJECT
private slots:
    // ---------------------------------------------------------------- alert
    void alertButtonsAndOneOutcome() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted), can(&h, &PromptHost::cancelled);
        h.present(alertReq(5, {"Yes", "No", "Maybe"}));
        QVERIFY(h.busy());
        QDialog* d = h.current(); QVERIFY(d);
        QVERIFY(d->findChild<QPushButton*>("promptButton:Yes") && d->findChild<QPushButton*>("promptButton:Maybe"));
        QVERIFY(d->findChild<QPushButton*>("promptButton:Yes")->isDefault());
        QCOMPARE(d->findChild<QLabel*>("promptMessage")->text(), QString("Pick one"));
        d->findChild<QPushButton*>("promptButton:No")->click();
        QCOMPARE(acc.count(), 1); QCOMPARE(can.count(), 0);
        QCOMPARE(acc.at(0).at(0).toUInt(), 5u); QCOMPARE(acc.at(0).at(1).toBool(), true); QCOMPARE(acc.at(0).at(2).toString(), QString("No"));
        QTRY_VERIFY(!h.busy());
    }
    // Stock DreamSeeker 516.1684 with stock DreamDaemon: an alert() (one, two or three buttons) and a non-null input() have no Cancel
    // button, ignore Escape and grey out the close box, so nothing is sent until an answer. Only an `as null|...` input() has Cancel;
    // its Cancel, Escape and close box all send the same null.
    void onlyANullInputCanBeCancelled() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted), can(&h, &PromptHost::cancelled);
        h.present(alertReq(1, {"OK"}));
        QDialog* d = h.current();
        QVERIFY(!(d->windowFlags() & Qt::WindowCloseButtonHint));  // no close box
        QVERIFY(!d->close());  // a window-manager close is refused
        QTest::keyClick(d, Qt::Key_Escape);
        QCOMPARE(can.count(), 0); QCOMPARE(acc.count(), 0); QVERIFY(h.busy());
        d->findChild<QPushButton*>("promptButton:OK")->click();
        QCOMPARE(acc.count(), 1);
        QTRY_VERIFY(!h.busy());
        for (quint32 mask : {0x4u, 0x8u, 0x800u, 0x20000u}) {  // as text / num / message / color: OK alone
            h.present(inputReq(2, mask, "x"));
            d = h.current();
            QVERIFY(!d->findChild<QPushButton*>("promptCancel"));
            QVERIFY(!(d->windowFlags() & Qt::WindowCloseButtonHint));
            QVERIFY(!d->close());
            QTest::keyClick(d, Qt::Key_Escape);
            QCOMPARE(can.count(), 0); QVERIFY(h.busy());
            d->findChild<QPushButton*>("promptOk")->click();
            QTRY_VERIFY(!h.busy());
        }
        PromptRequest list = inputReq(3, 0, ""); list.inList = true; list.choices = {"alpha"};
        h.present(list);
        QVERIFY(!h.current()->findChild<QPushButton*>("promptCancel"));
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QTRY_VERIFY(!h.busy());
        QCOMPARE(can.count(), 0);
        // as null|text: the close box, Escape and Cancel each cancel exactly once
        h.present(inputReq(4, 0x84, "x"));
        QVERIFY(h.current()->windowFlags() & Qt::WindowCloseButtonHint);
        h.current()->close();
        QCOMPARE(can.count(), 1); QCOMPARE(can.at(0).at(1).toBool(), false);
        QTRY_VERIFY(!h.busy());
        h.present(inputReq(5, 0x84, "x"));
        QTest::keyClick(h.current(), Qt::Key_Escape);
        QCOMPARE(can.count(), 2);
        QTRY_VERIFY(!h.busy());
        PromptRequest nl = inputReq(6, 0x1080, ""); nl.inList = true; nl.choices = {"alpha"};  // as null|anything in list
        h.present(nl);
        d = h.current();
        d->findChild<QPushButton*>("promptCancel")->click();
        d->close();  // a second close after the first outcome does not answer twice
        QCOMPARE(can.count(), 3); QCOMPARE(can.at(2).at(0).toUInt(), 6u);
        QCOMPARE(acc.count(), 1 + 5);
    }
    // A missing verb argument (a bare Say): the client's own dialog. Its outcome is argAccepted / argCancelled, never accepted / cancelled,
    // which would be turned into an input() reply for a proc that is not waiting.
    void verbArgumentDialog() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted), can(&h, &PromptHost::cancelled), aacc(&h, &PromptHost::argAccepted), acan(&h, &PromptHost::argCancelled);
        PromptRequest r; r.alert = false; r.verbArg = true; r.id = 1; r.title = "Say \"text\""; r.typeMask = 0x4;
        h.present(r);
        QDialog* d = h.current(); QVERIFY(d);
        QCOMPARE(d->windowTitle(), QString("Say \"text\""));
        QVERIFY(d->findChild<QLabel*>("promptMessage")->isHidden());  // no desc: DreamSeeker shows no text above the field
        auto* le = d->findChild<QLineEdit*>("promptField"); QVERIFY(le);
        QTest::keyClicks(le, "hello there");
        QTest::keyClick(le, Qt::Key_Return);
        QCOMPARE(aacc.count(), 1); QCOMPARE(aacc.at(0).at(0).toUInt(), 1u); QCOMPARE(aacc.at(0).at(1).toString(), QString("hello there"));
        QCOMPARE(acc.count(), 0); QCOMPARE(can.count(), 0);
        QTRY_VERIFY(!h.busy());
        // Escape: argCancelled, nothing else
        r.id = 2; h.present(r);
        QTest::keyClick(h.current(), Qt::Key_Escape);
        QCOMPARE(acan.count(), 1); QCOMPARE(acan.at(0).at(0).toUInt(), 2u); QCOMPARE(can.count(), 0);
        QTRY_VERIFY(!h.busy());
        // `as message`: multi-line
        r.id = 3; r.title = "Me \"text\""; r.typeMask = 0x800; r.message = "DESCPROBE"; h.present(r);
        QVERIFY(h.current()->findChild<QPlainTextEdit*>("promptField"));
        QVERIFY(!h.current()->findChild<QLabel*>("promptMessage")->isHidden());
        h.current()->findChild<QPushButton*>("promptCancel")->click();
        QTRY_VERIFY(!h.busy());
        // `as num`: a non-number is refused in place (the box stays open); a number goes through
        r.id = 4; r.title = "SetNum number"; r.typeMask = 0x8; r.message.clear(); h.present(r);
        d = h.current();
        le = d->findChild<QLineEdit*>("promptField");
        QTest::keyClicks(le, "abc");
        QTest::keyClick(le, Qt::Key_Return);
        d->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(aacc.count(), 1); QVERIFY(h.busy());
        le->setText("4.5");
        d->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(aacc.count(), 2); QCOMPARE(aacc.at(1).at(1).toString(), QString("4.5"));
        QTRY_VERIFY(!h.busy());
        // `in list` with the fetched choices: picking one answers it
        r.id = 5; r.title = "PickOne word"; r.typeMask = 0x4; r.inList = true; r.choices = {"alpha", "beta"}; h.present(r);
        auto* list = h.current()->findChild<QListWidget*>("promptList"); QVERIFY(list);
        QCOMPARE(list->currentRow(), 0);  // the first choice, preselected
        QVERIFY(h.current()->findChild<QLineEdit*>("promptField")->isHidden());  // a listbox alone
        list->setCurrentRow(1);
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(aacc.at(2).at(1).toString(), QString("beta"));
        QCOMPARE(acc.count(), 0); QCOMPARE(can.count(), 0);
        QTRY_VERIFY(!h.busy());
        r.id = 6; h.present(r);
        QVERIFY(!h.current()->findChild<QPushButton*>("promptNone"));   // no None for a required argument
        h.current()->findChild<QPushButton*>("promptCancel")->click();
        QTRY_VERIFY(!h.busy());
        // a nullable argument (a stat-panel click): OK, Cancel and None; None reports argNone only
        QSignalSpy anone(&h, &PromptHost::argNone);
        r.id = 7; r.title = "Set Default Language [anything]"; r.allowNull = true; h.present(r);
        QVERIFY(h.current()->findChild<QPushButton*>("promptCancel"));
        h.current()->findChild<QPushButton*>("promptNone")->click();
        QCOMPARE(anone.count(), 1); QCOMPARE(anone.at(0).at(0).toUInt(), 7u);
        QCOMPARE(aacc.count(), 3); QCOMPARE(acan.count(), 3); QCOMPARE(acc.count(), 0); QCOMPARE(can.count(), 0);
    }
    void queuedOneAtATimeInArrivalOrder() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(alertReq(1, {"A"})); h.present(inputReq(2, 0x4, "two")); h.present(alertReq(3, {"C"}));
        QCOMPARE(h.queued(), 2); QCOMPARE(h.currentId(), 1u);
        h.current()->findChild<QPushButton*>("promptButton:A")->click();
        QTRY_COMPARE(h.currentId(), 2u);
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QTRY_COMPARE(h.currentId(), 3u);
        h.current()->findChild<QPushButton*>("promptButton:C")->click();
        QCOMPARE(acc.count(), 3);
        QCOMPARE(acc.at(1).at(2).toString(), QString("two"));
        QTRY_VERIFY(!h.busy());
    }
    // Stock DreamSeeker closes a withdrawn (s2c 0x009d) text input() and alert() with nothing sent, and a queued in-list
    // prompt withdrawn before it showed never opens.
    void withdrawnPromptsCloseWithNoOutcome() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted), can(&h, &PromptHost::cancelled);
        h.present(inputReq(0, 0x84, "")); h.present(inputReq(1, 0x1080, "")); h.present(alertReq(0, {"Yes", "No"}));
        QCOMPARE(h.queued(), 2);
        h.withdraw(1, false);
        QCOMPARE(h.queued(), 1); QCOMPARE(h.currentId(), 0u);
        h.withdraw(0, true);   // the queued alert, not the open input
        QCOMPARE(h.queued(), 0); QVERIFY(h.busy());
        QPointer<QDialog> d = h.current();
        h.withdraw(0, false);
        QVERIFY(!h.busy());
        QTRY_VERIFY(d.isNull());
        h.present(alertReq(2, {"OK"}));   // non-cancellable, closed all the same
        h.withdraw(2, true);
        QVERIFY(!h.busy());
        QCOMPARE(acc.count(), 0); QCOMPARE(can.count(), 0);
    }
    // ---------------------------------------------------------------- input forms
    void textSeedsDefaultAndOkReturnsEdit() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(inputReq(7, 0x4, "seed"));
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        QVERIFY(f); QCOMPARE(f->text(), QString("seed")); QVERIFY(isPasteWired(f));
        f->setText("typed");
        QTest::keyClick(f, Qt::Key_Return);  // Enter accepts
        QCOMPARE(acc.count(), 1); QCOMPARE(acc.at(0).at(2).toString(), QString("typed")); QCOMPARE(acc.at(0).at(1).toBool(), false);
    }
    void numericIsATextFieldTheSessionConverts() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(inputReq(4, 0x8, "7"));
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        QCOMPARE(f->text(), QString("7")); QVERIFY(f->property("numeric").toBool());
        f->setText("abc");  // a non-number is still ANSWERED (as text), never silently dropped
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(0).at(2).toString(), QString("abc"));
    }
    void messageIsMultiline() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(inputReq(7, 0x800, "seed"));
        auto* f = h.current()->findChild<QPlainTextEdit*>("promptField");
        QVERIFY(f); QVERIFY(isPasteWired(f));
        f->setPlainText("multi\nline");
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(0).at(2).toString(), QString("multi\nline"));
    }
    void colorFieldAndSwatch() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(inputReq(9, 0x20000, "#ff0000"));
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        QCOMPARE(f->text(), QString("#ff0000")); QVERIFY(isPasteWired(f));
        QVERIFY(h.current()->findChild<QLabel*>("promptSwatch")->styleSheet().contains("#ff0000"));
        f->setText(" #00ff00 ");
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(0).at(2).toString(), QString("#00ff00"));  // verbatim text on the wire, trimmed
    }
    void inListShowsChoicesAndLetsYouType() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        PromptRequest r = inputReq(5, 0, ""); r.inList = true; r.choices = {"alpha", "beta", "gamma"};
        h.present(r);
        auto* list = h.current()->findChild<QListWidget*>("promptList");
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        QVERIFY(list && f); QCOMPARE(list->count(), 3); QVERIFY(isPasteWired(f));
        QVERIFY(f->isHidden());   // a listbox alone, as stock
        list->setCurrentRow(1);
        QCOMPARE(f->text(), QString("beta"));
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(0).at(2).toString(), QString("beta"));
        QTRY_VERIFY(!h.busy());
        r.choices = {};  // the fetch failed: typed text still works
        h.present(r);
        QCOMPARE(h.current()->findChild<QListWidget*>("promptList")->count(), 0);
        QVERIFY(!h.current()->findChild<QLineEdit*>("promptField")->isHidden());
        h.current()->findChild<QLineEdit*>("promptField")->setText("typed");
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(1).at(2).toString(), QString("typed"));
    }
    void hostTeardownLeavesNoStaleAccessibleInterface() {
        QAccessible::Id id = 0;
        {
            QWidget anchor; PromptHost h(&anchor);
            h.present(inputReq(3, 0, "x"));
            id = QAccessible::uniqueId(QAccessible::queryAccessibleInterface(h.current()));
            QVERIFY(id);
        }
        QVERIFY(!QAccessible::accessibleInterface(id));
    }
    // A default arrives as stock shows it (model::inputDefaultText): its line breaks and tabs stay in the field and go back on OK.
    void defaultKeepsLineBreaksAndSelectsItsChoice() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted);
        h.present(inputReq(1, 0x4, "aAPC\nb\tc"));
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        QCOMPARE(f->text(), QString("aAPC\nb\tc"));
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(0).at(2).toString(), QString("aAPC\nb\tc"));
        QTRY_VERIFY(!h.busy());
        h.present(inputReq(2, 0x800, "aAPC\nb\tc"));
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(acc.at(1).at(2).toString(), QString("aAPC\nb\tc"));
        QTRY_VERIFY(!h.busy());
        for (auto [def, row] : {std::pair{"CDapc", 1}, std::pair{"c\\dAPC", 0}, std::pair{"", 0}}) {
            PromptRequest r = inputReq(3, 0, def); r.inList = true; r.choices = {"aaAPC", "c\\dAPC"};
            h.present(r);
            QCOMPARE(h.current()->findChild<QListWidget*>("promptList")->currentRow(), row);
            QCOMPARE(h.current()->findChild<QLineEdit*>("promptField")->text(), r.choices[row]);
            h.current()->findChild<QPushButton*>("promptOk")->click();
            QTRY_VERIFY(!h.busy());
        }
    }
    // With no default, stock selects and answers the first choice for input() and for a verb argument, even past a lone `\` or an empty one.
    void noDefaultAnswersTheFirstChoice() {
        QWidget anchor; PromptHost h(&anchor);
        QSignalSpy acc(&h, &PromptHost::accepted), arg(&h, &PromptHost::argAccepted);
        PromptRequest r = inputReq(1, 0, ""); r.inList = true; r.choices = {"aAPC", "\\", "", "bAPC"};
        h.present(r);
        QCOMPARE(h.current()->findChild<QListWidget*>("promptList")->currentRow(), 0);
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(QByteArray::fromStdString(fugue::model::inputChoiceText(acc.at(0).at(2).toString().toStdString())).toHex(), QByteArray("6141504320"));
        QTRY_VERIFY(!h.busy());
        r.verbArg = true; r.id = 2;
        h.present(r);
        QCOMPARE(h.current()->findChild<QListWidget*>("promptList")->currentRow(), 0);
        h.current()->findChild<QPushButton*>("promptOk")->click();
        QCOMPARE(QByteArray::fromStdString(fugue::net::listPickText(arg.at(0).at(1).toString().toStdString())).toHex(), QByteArray("61415043"));
        QTRY_VERIFY(!h.busy());
    }
    // ---------------------------------------------------------------- paste
    void pasteChordsIntoALineEdit() {
        QLineEdit le; wirePaste(&le); le.show(); QVERIFY(QTest::qWaitForWindowExposed(&le)); le.setFocus();
        QGuiApplication::clipboard()->setText("clip");
        key(&le, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(le.text(), QString("clip"));  // pasted once: the filter consumes the event
        le.clear();
        key(&le, Qt::Key_Insert, Qt::ShiftModifier);
        QCOMPARE(le.text(), QString("clip"));
        le.clear();
        key(&le, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(le.text(), QString("clip"));
        le.clear();
        QGuiApplication::clipboard()->setText("line1\r\nline2\n");  // a single-line editor takes one line: breaks become spaces, the trailing one is dropped
        key(&le, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(le.text(), QString("line1 line2"));
        le.setText("ab"); le.setCursorPosition(1); le.setSelection(0, 1);  // replaces a selection
        QGuiApplication::clipboard()->setText("Z");
        key(&le, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(le.text(), QString("Zb"));
        le.setReadOnly(true); le.clear();
        key(&le, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(le.text(), QString());  // never into a read-only field
    }
    // The platform's own Paste shortcuts paste too (Cmd+V on macOS, where Ctrl stays the physical Control key).
    void platformPasteShortcutsPaste() {
        QLineEdit le; wirePaste(&le); le.show(); QVERIFY(QTest::qWaitForWindowExposed(&le)); le.setFocus();
        QGuiApplication::clipboard()->setText("clip");
        const auto bindings = QKeySequence::keyBindings(QKeySequence::Paste);
        QVERIFY(!bindings.isEmpty());
        for (const QKeySequence& ks : bindings) {
            le.clear();
            QTest::keySequence(&le, ks);
            QCOMPARE(le.text(), QString("clip"));
        }
        QKeyEvent plainV(QEvent::KeyPress, Qt::Key_V, Qt::NoModifier, "v");
        QVERIFY(!isPasteChord(&plainV));
    }
    void unwiredWidgetsAreNotFiltered() {
        QLineEdit le; QVERIFY(!isPasteWired(&le));
        const int before = le.findChildren<QObject*>().size();
        wirePaste(&le); QVERIFY(isPasteWired(&le));
        QCOMPARE(le.findChildren<QObject*>().size(), before + 1);
        wirePaste(&le); QCOMPARE(le.findChildren<QObject*>().size(), before + 1);  // idempotent: one filter
    }
    void pasteIntoTextEditsAndEditableCombo() {
        QGuiApplication::clipboard()->setText("a\nb");
        QPlainTextEdit pe; wirePaste(&pe); pe.show(); QVERIFY(QTest::qWaitForWindowExposed(&pe)); pe.setFocus();
        key(&pe, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(pe.toPlainText(), QString("a\nb"));  // multi-line editors keep the breaks
        QTextEdit te; wirePaste(&te); te.show(); QVERIFY(QTest::qWaitForWindowExposed(&te)); te.setFocus();
        key(&te, Qt::Key_Insert, Qt::ShiftModifier);
        QCOMPARE(te.toPlainText(), QString("a\nb"));
        QComboBox cb; cb.setEditable(true); wirePaste(&cb); cb.show(); QVERIFY(QTest::qWaitForWindowExposed(&cb)); cb.lineEdit()->setFocus();
        key(cb.lineEdit(), Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(cb.lineEdit()->text(), QString("a b"));
    }
    void middleClickPastesThePrimarySelection() {
        QClipboard* cb = QGuiApplication::clipboard();
        if (!cb->supportsSelection()) QSKIP("this platform has no primary selection (offscreen QPA): the middle-click arm needs X11");
        cb->setText("PRIMARY", QClipboard::Selection);
        cb->setText("CLIPBOARD", QClipboard::Clipboard);
        QLineEdit le; wirePaste(&le); le.show(); QVERIFY(QTest::qWaitForWindowExposed(&le));
        QTest::mouseClick(&le, Qt::MiddleButton, Qt::NoModifier, QPoint(5, le.height() / 2));
        QCOMPARE(le.text(), QString("PRIMARY"));
    }
    void pasteIntoViaApiHonoursTheMode() {  // pasteInto(Selection) is the testable half of the middle click
        QClipboard* cb = QGuiApplication::clipboard();
        QLineEdit le;
        QVERIFY(pasteInto(&le, QClipboard::Clipboard) == !cb->text().isEmpty());
        le.clear(); cb->setText("ONLY-CLIP");
        QVERIFY(pasteInto(&le, QClipboard::Clipboard)); QCOMPARE(le.text(), QString("ONLY-CLIP"));
        if (cb->supportsSelection()) { cb->setText("SEL", QClipboard::Selection); le.clear(); QVERIFY(pasteInto(&le, QClipboard::Selection)); QCOMPARE(le.text(), QString("SEL")); }
        else { le.clear(); QVERIFY(!pasteInto(&le, QClipboard::Selection)); }  // no primary selection here: nothing pastes, nothing breaks
    }
    void pasteInThePromptDialogFields() {
        QWidget anchor; PromptHost h(&anchor);
        QGuiApplication::clipboard()->setText("pasted");
        h.present(inputReq(1, 0x4, ""));
        auto* f = h.current()->findChild<QLineEdit*>("promptField");
        h.current()->show(); QVERIFY(QTest::qWaitForWindowExposed(h.current())); f->setFocus();
        key(f, Qt::Key_V, Qt::ControlModifier);
        QCOMPARE(f->text(), QString("pasted"));
        h.current()->findChild<QPushButton*>("promptOk")->click();  // a non-null input() has no Cancel
        QTRY_VERIFY(!h.busy());
        h.present(inputReq(2, 0x800, ""));
        auto* m = h.current()->findChild<QPlainTextEdit*>("promptField");
        h.current()->show(); QVERIFY(QTest::qWaitForWindowExposed(h.current())); m->setFocus();
        key(m, Qt::Key_Insert, Qt::ShiftModifier);
        QCOMPARE(m->toPlainText(), QString("pasted"));
    }
    void pasteIntoTheSkinsInputBarEvenWhenAMacroBindsCtrlV() {
        SkinWindow win;
        QVERIFY2(win.loadSkin(kPromptSkin).isEmpty(), "skin parses");
        win.resize(win.designSize()); win.show(); QVERIFY(QTest::qWaitForWindowExposed(&win));
        auto* in = qobject_cast<QLineEdit*>(win.controlWidget("default.input"));
        QVERIFY(in); QVERIFY(isPasteWired(in));
        FocusFilter ff; ff.setMacros(win.macros().sets.at("macro"));
        qApp->installEventFilter(&ff);
        QSignalSpy fired(&ff, &FocusFilter::macroFired);
        QGuiApplication::clipboard()->setText("from the clipboard");
        in->setFocus(); QVERIFY(in->hasFocus());
        key(in, Qt::Key_V, Qt::ControlModifier);
        qApp->removeEventFilter(&ff);
        QCOMPARE(in->text(), QString("from the clipboard"));
        QCOMPARE(fired.count(), 0);  // the skin's CTRL+V macro did not eat the paste
    }
    // ---------------------------------------------------------------- INFO control
    void statPanelRendersTabsAndRows() {
        StatPanelWidget w;
        StatView v; v.tabs = {"Status", "Values", "Commands"}; v.selected = 0;
        v.rows = {{"S-Health:", "100", false}, {"S-Ref:", "token", true}};
        w.setView(v);
        QCOMPARE(w.tabBar()->count(), 3); QCOMPARE(w.tabBar()->currentIndex(), 0);
        QCOMPARE(w.table()->rowCount(), 2); QCOMPARE(w.table()->columnCount(), 2);
        QCOMPARE(w.table()->item(1, 0)->text(), QString("S-Ref:")); QCOMPARE(w.table()->item(1, 1)->text(), QString("token"));
        QCOMPARE(w.property("fugueZone").toString(), QString("info"));
    }
    void tabClickAsksTheServerAndDoesNotSwitchLocally() {
        StatPanelWidget w; w.resize(400, 200); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        StatView v; v.tabs = {"Status", "Values", "Commands"}; v.selected = 0; v.rows = {{"a", "b", false}};
        w.setView(v);
        QSignalSpy req(&w, &StatPanelWidget::tabRequested);
        QRect r1 = w.tabBar()->tabRect(1);
        QTest::mouseClick(w.tabBar(), Qt::LeftButton, Qt::NoModifier, r1.center());
        QCOMPARE(req.count(), 1); QCOMPARE(req.at(0).at(0).toInt(), 1);
        // DreamDaemon sends only the SELECTED tab's rows: the strip follows the model, not the click
        QCOMPARE(w.tabBar()->currentIndex(), 0);
        QTest::mouseClick(w.tabBar(), Qt::LeftButton, Qt::NoModifier, w.tabBar()->tabRect(0).center());
        QCOMPARE(req.count(), 1);  // clicking the selected tab asks for nothing
        v.selected = 1; v.rows = {{"V-Int:", "42", false}};
        w.setView(v);  // the model moved: the strip and rows follow
        QCOMPARE(w.tabBar()->currentIndex(), 1); QCOMPARE(w.table()->rowCount(), 1);
        QCOMPARE(req.count(), 1);  // a programmatic switch is not a click
    }
    void atomRowsClickOthersDont() {
        StatPanelWidget w; w.resize(400, 200); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        StatView v; v.tabs = {"Status"}; v.rows = {{"S-Health:", "100", false}, {"S-Ref:", "token", true}};
        w.setView(v);
        w.setControlId("infowindow.info");
        QSignalSpy row(&w, &StatPanelWidget::rowClicked);
        auto at = [&](int r, int c) { return w.table()->visualRect(w.table()->model()->index(r, c)).center(); };
        QTest::mouseClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(0, 1));
        QCOMPARE(row.count(), 0);
        QTest::mouseClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 0));  // the LABEL: stock sent nothing
        QCOMPARE(row.count(), 0);
        QTest::mouseClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 1));
        QCOMPARE(row.count(), 1); QCOMPARE(row.at(0).at(0).toInt(), 1);
        QCOMPARE(row.at(0).at(1).toString(), QString("infowindow.info")); QCOMPARE(row.at(0).at(2).toString(), QString("left"));
        QCOMPARE(row.at(0).at(3).toInt(), 0); QCOMPARE(row.at(0).at(4).toBool(), false);
        QTest::mouseClick(w.table()->viewport(), Qt::RightButton, Qt::NoModifier, at(1, 1));  // stock sent nothing for a right click
        QCOMPARE(row.count(), 1);
        QTest::mouseClick(w.table()->viewport(), Qt::MiddleButton, Qt::ShiftModifier | Qt::ControlModifier, at(1, 1));
        QCOMPARE(row.count(), 2); QCOMPARE(row.at(1).at(2).toString(), QString("middle"));
        QCOMPARE(row.at(1).at(3).toInt(), int(StatPanelWidget::ModCtrl | StatPanelWidget::ModShift));
        // a double click: click, dblclick, click - stock's own 03, 04, 03
        QTest::mousePress(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 1));
        QTest::mouseRelease(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 1));
        QTest::mouseDClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 1));
        QTest::mouseRelease(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, at(1, 1));
        QCOMPARE(row.count(), 5);
        QCOMPARE(row.at(2).at(4).toBool(), false); QCOMPARE(row.at(3).at(4).toBool(), true); QCOMPARE(row.at(4).at(4).toBool(), false);
    }
    // An atom row draws its icon cell at the head of the value column, and a click in it counts as stock DreamSeeker counts it (render/scene.h statRowIcon):
    // a click 10 px in from the left and 10 px up from the bottom of a 32 px icon reads icon-x=10;icon-y=10; its corners read 1,1 and 32,32;
    // a click on the name right of it reads icon-x=70.
    void atomRowDrawsItsIconAndCountsClicksInIt() {
        StatPanelWidget w; w.resize(400, 200); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        QImage red(32, 32, QImage::Format_RGBA8888); red.fill(QColor(255, 0, 0));
        StatView v; v.tabs = {"Tile"};
        v.rows = {{"", "o_red", true, red}, {"", "o_noicon", true, {}}, {"Label:", "o_red", true, red}};
        w.setView(v);
        w.setControlId("infowindow.info");
        QRect c = w.table()->visualRect(w.table()->model()->index(0, 1));
        QVERIFY2(c.height() >= 33, "an icon row holds its 32 px cell plus stock's 1 px");
        QImage shot = w.table()->viewport()->grab().toImage();
        QCOMPARE(shot.pixelColor(c.left() + 10, c.top() + 10), QColor(255, 0, 0));
        QCOMPARE(shot.pixelColor(c.left() + 31, c.top() + 31), QColor(255, 0, 0));
        QVERIFY(shot.pixelColor(c.left() + 33, c.top() + 10) != QColor(255, 0, 0));  // the cell ends; the name follows
        QRect lc = w.table()->visualRect(w.table()->model()->index(2, 1));
        QCOMPARE(shot.pixelColor(lc.left() + 10, lc.top() + 10), QColor(255, 0, 0));  // a labelled row: the icon in the VALUE column
        QSignalSpy row(&w, &StatPanelWidget::rowClicked);
        auto click = [&](QPoint p) { QTest::mouseClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, p); QTest::qWait(500); };
        click(QPoint(c.left() + 9, c.top() + 22));
        click(QPoint(c.left(), c.top() + 31));
        click(QPoint(c.left() + 31, c.top()));
        click(QPoint(c.left() + 69, c.top() + 12));
        QRect nc = w.table()->visualRect(w.table()->model()->index(1, 1));
        click(QPoint(nc.left() + 5, nc.center().y()));
        QCOMPARE(row.count(), 5);
        auto xy = [&](int i) { return QString("%1:%2,%3").arg(row.at(i).at(5).toBool()).arg(row.at(i).at(6).toInt()).arg(row.at(i).at(7).toInt()); };
        QCOMPARE(xy(0), QString("1:10,10"));
        QCOMPARE(xy(1), QString("1:1,1"));
        QCOMPARE(xy(2), QString("1:32,32"));
        QCOMPARE(xy(3), QString("1:70,20"));
        QCOMPARE(row.at(4).at(0).toInt(), 1); QCOMPARE(row.at(4).at(5).toBool(), false);  // nothing drawn: no icon-x/icon-y
    }
    void verbTabIsAFourColumnGridThatRunsVerbs() {
        StatPanelWidget w; w.resize(500, 200); w.show(); QVERIFY(QTest::qWaitForWindowExposed(&w));
        StatView v; v.tabs = {"Status", "OOC"}; v.selected = 1; v.verbTab = true;
        for (const char* n : {"Changelog", "Check My BYOND Version", "Custom Event Info", "Fix NanoUI", "Who"}) v.rows.append({n, "", false});
        w.setView(v);
        QCOMPARE(w.table()->columnCount(), 4); QCOMPARE(w.table()->rowCount(), 2);  // row-major across 4 columns (stock layout)
        QCOMPARE(w.table()->item(0, 3)->text(), QString("Fix NanoUI")); QCOMPARE(w.table()->item(1, 0)->text(), QString("Who"));
        QSignalSpy vb(&w, &StatPanelWidget::verbClicked);
        QTest::mouseClick(w.table()->viewport(), Qt::LeftButton, Qt::NoModifier, w.table()->visualRect(w.table()->model()->index(0, 1)).center());
        QCOMPARE(vb.count(), 1); QCOMPARE(vb.at(0).at(0).toString(), QString("Check My BYOND Version"));
    }
    void skinWindowInfoControlIsTheStatPanel() {
        SkinWindow win;
        QVERIFY2(win.loadSkin(kPromptSkin).isEmpty(), "skin parses");
        win.resize(win.designSize()); win.show(); QVERIFY(QTest::qWaitForWindowExposed(&win));
        auto* info = qobject_cast<StatPanelWidget*>(win.controlWidget("default.info"));
        QVERIFY2(info, "INFO is a StatPanelWidget");
        StatView v; v.tabs = {"Status", "Values"}; v.rows = {{"S-Ref:", "token", true}};
        win.setStatView(v);
        QCOMPARE(info->tabBar()->count(), 2); QCOMPARE(info->table()->rowCount(), 1);
        QSignalSpy tab(&win, &SkinWindow::statTabRequested), row(&win, &SkinWindow::statRowClicked);
        QTest::mouseClick(info->tabBar(), Qt::LeftButton, Qt::NoModifier, info->tabBar()->tabRect(1).center());
        QCOMPARE(tab.count(), 1); QCOMPARE(tab.at(0).at(0).toInt(), 1);
        QTest::mouseClick(info->table()->viewport(), Qt::LeftButton, Qt::NoModifier, info->table()->visualRect(info->table()->model()->index(0, 1)).center());
        QCOMPARE(row.count(), 1);
        QCOMPARE(row.at(0).at(1).toString(), QString("default.info"));  // the INFO control's own id names the click's control
        // a view set before the control exists reaches it when it is built (a skin swap rebuilds the controls)
        QVERIFY(win.loadSkin(kPromptSkin).isEmpty());
        auto* again = qobject_cast<StatPanelWidget*>(win.controlWidget("default.info"));
        QVERIFY(again); QCOMPARE(again->tabBar()->count(), 2);
    }
};

int main(int argc, char** argv) {
    registerSchemes();
    QApplication app(argc, argv);
    PromptTest t;
    return QTest::qExec(&t, argc, argv);
}
#include "prompt_test.moc"

// Paste, wired the same way into every widget of this client that takes keys.
//
// Every editable widget is created through wirePaste() or the factories below. A source-scan test
// (tests/paste_scan_test.cpp) greps shell/ and app/ for editable widget construction and fails on any that is not
// registered, so a new key-taking widget cannot skip the paste handling.
//
// Chords: Ctrl+V, Ctrl+Shift+V, Shift+Insert, the platform's QKeySequence::Paste (the clipboard) and a middle click (the X11 primary selection, where the
// platform has one). The filter consumes the event so Qt never pastes twice, and so the behaviour is identical
// everywhere. A browser pane is not wired: Chromium owns its keys and pastes natively (shell/browserpane.cpp).
#pragma once
#include <QClipboard>
#include <QKeyEvent>
#include <QPoint>
#include <QString>

class QWidget;

namespace fugue::shell {

// Ctrl+V / Ctrl+Shift+V / Shift+Insert, or the platform's Paste shortcut (Cmd+V on macOS) (a press).
bool isPasteChord(const QKeyEvent* e);
// Normalises clipboard text for a SINGLE-line editor: line breaks become single spaces and the trailing one is dropped.
QString singleLine(QString text);
// Pastes the clipboard (Clipboard) or the X11 primary selection (Selection) into the editor at its cursor, replacing a
// selection. `at` (a widget-local point) moves the cursor first (a middle click pastes where it lands). Returns true if text
// went in. Handles QLineEdit, QTextEdit, QPlainTextEdit, QComboBox(editable), QAbstractSpinBox.
bool pasteInto(QWidget* w, QClipboard::Mode mode = QClipboard::Clipboard, const QPoint* at = nullptr);
// Installs the filter on `w` (and its viewport / inner line edit). Idempotent. Returns w for chaining.
template <class W>
W* wirePaste(W* w);
QWidget* wirePasteWidget(QWidget* w);
bool isPasteWired(const QWidget* w);

template <class W>
W* wirePaste(W* w) {
    wirePasteWidget(w);
    return w;
}

}  // namespace fugue::shell

#include "shell/paste.h"
#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace fugue::shell {
namespace {
const char* kWired = "fuguePasteWired";

QWidget* editorOf(QWidget* w) {
    if (auto* c = qobject_cast<QComboBox*>(w)) return c->isEditable() ? c->lineEdit() : nullptr;
    if (auto* s = qobject_cast<QAbstractSpinBox*>(w)) return s->findChild<QLineEdit*>();
    return w;
}

class PasteFilter : public QObject {
public:
    explicit PasteFilter(QWidget* target) : QObject(target), target_(target) {}
    bool eventFilter(QObject* o, QEvent* e) override {
        QWidget* ed = editorOf(target_);
        if (!ed) return false;
        switch (e->type()) {
            case QEvent::KeyPress: {
                auto* k = static_cast<QKeyEvent*>(e);
                if (!isPasteChord(k)) return false;
                if (ed->isEnabled()) pasteInto(ed, QClipboard::Clipboard);
                return true;  // consumed even when the clipboard is empty, so Qt does not paste again and no macro sees it
            }
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease: {
                auto* m = static_cast<QMouseEvent*>(e);
                if (m->button() != Qt::MiddleButton) return false;
                if (e->type() == QEvent::MouseButtonRelease && ed->isEnabled()) {
                    // the event may arrive on the viewport: map to the editor's coordinates
                    QPoint p = m->position().toPoint();
                    if (auto* w = qobject_cast<QWidget*>(o); w && w != ed) p = ed->mapFromGlobal(w->mapToGlobal(p));
                    pasteInto(ed, QClipboard::Selection, &p);
                }
                return true;
            }
            default: return false;
        }
    }

private:
    QWidget* target_;
};
}  // namespace

bool isPasteChord(const QKeyEvent* e) {
    if (e->type() != QEvent::KeyPress) return false;
    if (e->matches(QKeySequence::Paste)) return true;
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (e->key() == Qt::Key_V && (mods == Qt::ControlModifier || mods == (Qt::ControlModifier | Qt::ShiftModifier)))
        return true;
    if (e->key() == Qt::Key_Insert && mods == Qt::ShiftModifier) return true;
    return false;
}

QString singleLine(QString t) {
    t.replace("\r\n", "\n").replace('\r', '\n');
    while (t.endsWith('\n')) t.chop(1);
    t.replace('\n', ' ');
    return t;
}

bool pasteInto(QWidget* w, QClipboard::Mode mode, const QPoint* at) {
    if (!w) return false;
    QClipboard* cb = QGuiApplication::clipboard();
    if (mode == QClipboard::Selection && !cb->supportsSelection()) return false;  // no primary selection here
    QString text = cb->text(mode);
    if (text.isEmpty()) return false;
    if (auto* c = qobject_cast<QComboBox*>(w)) w = c->isEditable() ? c->lineEdit() : nullptr;
    if (!w) return false;
    if (auto* le = qobject_cast<QLineEdit*>(w)) {
        if (le->isReadOnly()) return false;
        if (at) le->setCursorPosition(le->cursorPositionAt(*at));
        le->insert(singleLine(text));
        return true;
    }
    if (auto* te = qobject_cast<QTextEdit*>(w)) {
        if (te->isReadOnly()) return false;
        if (at) te->setTextCursor(te->cursorForPosition(*at));
        te->insertPlainText(text);
        return true;
    }
    if (auto* pe = qobject_cast<QPlainTextEdit*>(w)) {
        if (pe->isReadOnly()) return false;
        if (at) pe->setTextCursor(pe->cursorForPosition(*at));
        pe->insertPlainText(text);
        return true;
    }
    if (auto* sb = qobject_cast<QAbstractSpinBox*>(w)) {
        if (QLineEdit* le = sb->findChild<QLineEdit*>()) {
            le->insert(singleLine(text));
            return true;
        }
    }
    return false;
}

QWidget* wirePasteWidget(QWidget* w) {
    if (!w || w->property(kWired).toBool()) return w;
    auto* f = new PasteFilter(w);
    w->installEventFilter(f);
    if (auto* a = qobject_cast<QAbstractScrollArea*>(w)) a->viewport()->installEventFilter(f);
    if (auto* c = qobject_cast<QComboBox*>(w)) {
        if (c->lineEdit()) c->lineEdit()->installEventFilter(f);
    }
    if (auto* s = qobject_cast<QAbstractSpinBox*>(w)) {
        if (auto* le = s->findChild<QLineEdit*>()) le->installEventFilter(f);
    }
    w->setProperty(kWired, true);
    return w;
}

bool isPasteWired(const QWidget* w) { return w && w->property(kWired).toBool(); }

}  // namespace fugue::shell

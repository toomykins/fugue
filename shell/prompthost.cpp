#include "shell/prompthost.h"
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <cstdio>
#include <functional>
#include "model/prompt.h"
#include "shell/fugueicon.h"
#include "shell/paste.h"

namespace fugue::shell {
namespace {
constexpr quint32 kNum = 0x0008, kNull = 0x0080, kMessage = 0x0800, kColor = 0x020000;

// Reports the dialog's single outcome through done_. Closing without an answer counts as a cancel only when the dialog
// is cancellable. Stock DreamSeeker offers no way out of an alert() or a non-null input() but an answer, so such a
// dialog is built with no close button, and Escape and the window close box do nothing.
class PromptDialog : public QDialog {
public:
    // Fugue's icon, not the world's, which the parent chain would otherwise supply (shell/fugueicon.h).
    PromptDialog(QWidget* parent, bool cancellable) : QDialog(parent, flags(cancellable)), cancellable_(cancellable) { setWindowIcon(fugueIcon()); }
    static Qt::WindowFlags flags(bool cancellable) {
        Qt::WindowFlags f = Qt::Dialog | Qt::WindowTitleHint;
        return f | (cancellable ? Qt::WindowCloseButtonHint : Qt::CustomizeWindowHint);   // no close box without the hint
    }
    std::function<void(bool ok, const QString& text)> done;
    std::function<QString()> value;
    void finish(bool ok) {
        if (finished_) return;
        finished_ = true;
        if (done) done(ok, ok && value ? value() : QString());
        hide();
        deleteLater();
    }
protected:
    void closeEvent(QCloseEvent* e) override {
        if (!cancellable_ && !finished_) { e->ignore(); return; }
        finish(false);
        e->accept();
    }
    void reject() override { if (cancellable_) finish(false); }   // Escape
    void accept() override { if (!accepts || accepts()) finish(true); }    // Enter on the default button
public:
    std::function<bool()> accepts;   // a value the field refuses keeps the dialog open
    bool none = false;               // finished by the None button
private:
    bool cancellable_ = true;
    bool finished_ = false;
};
}  // namespace

PromptHost::PromptHost(QWidget* anchor, QObject* parent) : QObject(parent), anchor_(anchor) {}
PromptHost::~PromptHost() {
    if (cur_) { static_cast<PromptDialog*>(cur_.data())->done = nullptr; cur_->deleteLater(); }
}

void PromptHost::present(const PromptRequest& r) {
    queue_.push_back(r);
    if (!busy()) showNext();
}

void PromptHost::withdraw(quint32 id, bool alert) {
    auto same = [id, alert](const PromptRequest& r) { return !r.verbArg && r.id == id && r.alert == alert; };
    for (auto it = queue_.begin(); it != queue_.end();) it = same(*it) ? queue_.erase(it) : it + 1;
    if (!cur_ || curDone_ || curVerbArg_ || curId_ != id || curAlert_ != alert) return;
    curDone_ = true;
    std::fprintf(stderr, "[prompt] %s id=%u withdrawn - closed\n", alert ? "alert" : "input", id);
    static_cast<PromptDialog*>(cur_.data())->done = nullptr;
    cur_->hide();
    cur_->deleteLater();
    cur_.clear();
    showNext();
}

void PromptHost::showNext() {
    if (queue_.empty()) return;
    PromptRequest r = queue_.front();
    queue_.pop_front();
    cur_ = build(r);
    curId_ = r.id;
    curAlert_ = r.alert;
    curVerbArg_ = r.verbArg;
    curDone_ = false;
    cur_->show();
    cur_->raise();
    cur_->activateWindow();
    std::fprintf(stderr, "[prompt] %s id=%u \"%s\" opened%s\n", r.verbArg ? "verb-argument" : r.alert ? "alert" : "input", r.id, r.title.toUtf8().constData(),
                 queue_.empty() ? "" : " (more queued)");
}

QDialog* PromptHost::build(const PromptRequest& r) {
    // The client's own verb-argument dialog keeps its Cancel, since nothing on the server waits on it. A world
    // prompt is cancellable only as an `as null` input().
    bool cancellable = r.verbArg || (!r.alert && (r.typeMask & kNull));
    auto* d = new PromptDialog(anchor_, cancellable);
    d->setObjectName(r.alert ? "promptAlert" : "promptInput");
    d->setWindowTitle(r.title.isEmpty() ? QString("Prompt") : r.title);
    d->setWindowModality(Qt::NonModal);
    auto* lay = new QVBoxLayout(d);
    auto* msg = new QLabel(r.message, d);
    msg->setObjectName("promptMessage");
    msg->setWordWrap(true);
    msg->setTextInteractionFlags(Qt::TextSelectableByMouse);
    msg->setMinimumWidth(280);
    lay->addWidget(msg);
    if (r.verbArg && r.message.isEmpty()) msg->hide();   // no text above the field when the verb has no desc
    quint32 id = r.id;
    bool alert = r.alert, verbArg = r.verbArg;
    auto report = [this, id, alert, verbArg, dlg = QPointer<PromptDialog>(d)](bool ok, const QString& text) {
        if (curDone_ || id != curId_) return;
        curDone_ = true;
        bool none = dlg && dlg->none;
        std::fprintf(stderr, "[prompt] %s id=%u %s%s\n", verbArg ? "verb-argument" : alert ? "alert" : "input", id,
                     none ? "none" : ok ? "answered " : "cancelled", ok && !none ? ("\"" + text.left(80) + "\"").toUtf8().constData() : "");
        if (verbArg) { if (none) emit argNone(id); else if (ok) emit argAccepted(id, text); else emit argCancelled(id); }
        else if (ok) emit accepted(id, alert, text); else emit cancelled(id, alert);
        // open the next one once this one is fully retired
        QMetaObject::invokeMethod(this, [this] { cur_.clear(); showNext(); }, Qt::QueuedConnection);
    };
    d->done = report;

    if (r.alert) {
        auto* row = new QHBoxLayout;
        row->addStretch();
        QStringList labels = r.buttons.isEmpty() ? QStringList{"OK"} : r.buttons;
        for (int i = 0; i < labels.size(); i++) {
            auto* b = new QPushButton(labels[i], d);
            b->setObjectName("promptButton:" + labels[i]);
            if (i == 0) b->setDefault(true);
            QString label = labels[i];
            QObject::connect(b, &QPushButton::clicked, d, [d, label] { d->value = [label] { return label; }; d->finish(true); });
            row->addWidget(b);
        }
        lay->addLayout(row);
        return d;
    }

    auto* ok = new QPushButton("OK", d);
    ok->setObjectName("promptOk");
    ok->setDefault(true);
    QPushButton* cancel = nullptr;
    if (cancellable) {   // stock draws "OK" alone for a non-null input()
        cancel = new QPushButton("Cancel", d);
        cancel->setObjectName("promptCancel");
        QObject::connect(cancel, &QPushButton::clicked, d, [d] { d->finish(false); });
    }
    QObject::connect(ok, &QPushButton::clicked, d, [d] { if (!d->accepts || d->accepts()) d->finish(true); });
    QPushButton* none = nullptr;
    if (r.verbArg && r.allowNull) {   // stock DreamSeeker: OK, Cancel, None
        none = new QPushButton("None", d);
        none->setObjectName("promptNone");
        QObject::connect(none, &QPushButton::clicked, d, [d] { d->none = true; d->finish(true); });
    }

    QWidget* focus = nullptr;
    if (r.inList) {
        auto* list = new QListWidget(d);
        list->setObjectName("promptList");
        list->addItems(r.choices);
        list->setMinimumHeight(120);
        auto* le = wirePaste(new QLineEdit(r.def, d));   // a list that could not be fetched still accepts typed values
        le->setObjectName("promptField");
        QObject::connect(list, &QListWidget::currentTextChanged, le, [le](const QString& t) { if (!t.isEmpty()) le->setText(t); });
        QObject::connect(list, &QListWidget::itemDoubleClicked, d, [d] { d->finish(true); });
        QObject::connect(le, &QLineEdit::returnPressed, d, [d] { d->finish(true); });
        std::string def = r.def.toStdString();
        for (int i = 0; i < list->count(); i++)
            if (model::inputChoiceMatches(list->item(i)->text().toStdString(), def)) { list->setCurrentRow(i); break; }
        if (list->currentRow() < 0 && list->count()) list->setCurrentRow(0);
        lay->addWidget(list);
        lay->addWidget(le);
        d->value = [le] { return le->text(); };
        focus = le;
        if (!r.choices.isEmpty()) {
            // Stock DreamSeeker shows a listbox alone, for input() and a verb argument alike, so Enter picks its selected choice.
            le->hide();
            focus = list;
        }
    } else if (r.typeMask & kColor) {
        auto* row = new QHBoxLayout;
        auto* le = wirePaste(new QLineEdit(r.def, d));
        le->setObjectName("promptField");
        auto* swatch = new QLabel(d);
        swatch->setObjectName("promptSwatch");
        swatch->setFixedSize(28, 20);
        auto paint = [swatch, le] {
            QColor c(le->text().trimmed());
            swatch->setStyleSheet(c.isValid() ? QString("background:%1;border:1px solid #444;").arg(c.name()) : "border:1px solid #444;");
        };
        QObject::connect(le, &QLineEdit::textChanged, swatch, paint);
        paint();
        auto* pick = new QPushButton("Choose...", d);
        pick->setObjectName("promptPick");
        QObject::connect(pick, &QPushButton::clicked, d, [d, le] {
            QColor c = QColorDialog::getColor(QColor(le->text().trimmed()), d, "Pick a color");
            if (c.isValid()) le->setText(c.name());
        });
        QObject::connect(le, &QLineEdit::returnPressed, d, [d] { d->finish(true); });
        row->addWidget(le, 1);
        row->addWidget(swatch);
        row->addWidget(pick);
        lay->addLayout(row);
        d->value = [le] { return le->text().trimmed(); };
        focus = le;
    } else if (r.typeMask & kMessage) {
        auto* te = wirePaste(new QPlainTextEdit(r.def, d));
        te->setObjectName("promptField");
        te->setMinimumHeight(120);
        lay->addWidget(te);
        d->value = [te] { return te->toPlainText(); };
        focus = te;
    } else {
        auto* le = wirePaste(new QLineEdit(r.def, d));
        le->setObjectName("promptField");
        le->setProperty("numeric", bool(r.typeMask & kNum));
        if (r.verbArg && (r.typeMask & kNum)) {
            // On stock DreamSeeker, OK on a non-number leaves the `as num` box open and sends nothing.
            auto numeric = [le] { bool ok = false; le->text().trimmed().toFloat(&ok); return ok; };
            QObject::connect(le, &QLineEdit::returnPressed, d, [d, numeric] { if (numeric()) d->finish(true); });
            d->accepts = numeric;
        } else {
            QObject::connect(le, &QLineEdit::returnPressed, d, [d] { d->finish(true); });
        }
        lay->addWidget(le);
        d->value = [le] { return le->text(); };
        focus = le;
    }
    auto* row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(ok);
    if (cancel) row->addWidget(cancel);
    if (none) row->addWidget(none);
    lay->addLayout(row);
    if (focus) { focus->setFocus(); if (auto* le = qobject_cast<QLineEdit*>(focus)) le->selectAll(); }
    return d;
}

}  // namespace fugue::shell

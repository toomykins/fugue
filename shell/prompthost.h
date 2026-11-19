// PromptHost shows the world's alert() and input() dialogs, and the client's own missing-verb-argument dialog
// (PromptRequest::verbArg), which shares their shapes and their one-at-a-time queue.
//
//   * One host for both families. alert() has up to three buttons; input() takes text, `as num`, `as message`,
//     `as color`, `as null` or `in list` (choices fetched through InputExpansion). Every editable field is registered
//     with shell/paste.h.
//   * Exactly one outcome per request: accepted(id, text) or cancelled(id). Only an `as null` input() can be
//     cancelled (Cancel, Escape or the window close box all give null). An alert() and a non-null input() have no
//     Cancel, and ignore Escape and the close box until answered (see model::Prompt::cancellable).
//     The session thread turns the outcome into the reply; this class never touches a socket.
//   * A prompt the server withdraws (S2C::PromptWithdraw) is closed, or dropped from the queue, with no outcome, as
//     stock DreamSeeker closes it; the next one then opens.
//   * Modality is one dialog at a time, in arrival order. The map stays interactive because each dialog is its own
//     top-level window, so the dialogs are not application-modal.
// GUI thread only.
#pragma once
#include <QDialog>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <deque>

namespace fugue::shell {

struct PromptRequest {
    bool alert = true;
    quint32 id = 0;
    QString title, message, def;
    QStringList buttons;   // alert (empty slots already removed)
    quint32 typeMask = 0;  // input: the `as` clause (model/prompt.h kInput*)
    bool inList = false;
    QStringList choices;  // `in list`
    // A missing verb argument (net::argPrompt) is the client's own dialog, not a world input(). Nothing on the server
    // waits on it, so its outcome is argAccepted, argCancelled or argNone, never accepted or cancelled. An `as num` field
    // refuses a non-number in place, as stock DreamSeeker does.
    bool verbArg = false;
    bool allowNull = false;  // verbArg: a nullable argument's dialog also has None, reported as argNone
};

class PromptHost : public QObject {
    Q_OBJECT
public:
    explicit PromptHost(QWidget* anchor, QObject* parent = nullptr);
    ~PromptHost() override;
    void present(const PromptRequest& r);   // queue (or show if idle)
    void withdraw(quint32 id, bool alert);  // a world prompt the server dropped: closed or unqueued, no outcome
    bool busy() const { return !cur_.isNull(); }
    int queued() const { return int(queue_.size()); }
    QDialog* current() const { return cur_; }  // the live dialog, reached through its named children
    quint32 currentId() const { return curId_; }
signals:
    void accepted(quint32 id, bool alert,
                  const QString& text);  // alert: the button's label; input: the typed/picked text
    void cancelled(quint32 id, bool alert);
    void argAccepted(quint32 id, const QString& text);  // verbArg requests only
    void argCancelled(quint32 id);
    void argNone(quint32 id);

private:
    void showNext();
    QDialog* build(const PromptRequest& r);
    QWidget* anchor_;
    std::deque<PromptRequest> queue_;
    QPointer<QDialog> cur_;
    quint32 curId_ = 0;
    bool curAlert_ = false, curVerbArg_ = false, curDone_ = false;
};

}  // namespace fugue::shell
Q_DECLARE_METATYPE(fugue::shell::PromptRequest)

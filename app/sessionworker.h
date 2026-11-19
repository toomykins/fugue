// SessionWorker: one thread that owns the net::Session, the model::Session and the render::ResStore.
// It is the model's single writer and only reader: frames are ingested, the thread assembles the map DrawList
// (handed to RenderThread) at the map cadence, and every kTickMs it diffs chat / stat / browse / skin into value-type
// Qt signals. The model is
// never touched from another thread, so it needs no lock.
//
// Input runs the other way: the GUI thread never touches the socket. Each user action is posted as a plain value
// (post*(), any thread) into a mutex-guarded inbox, which the session thread drains at the top of every loop
// iteration and turns into frames via net::InputModel and render::MouseChain. Writes come from the same thread,
// which owns the socket (net/session.h). MouseChain state (held button, drag record, double-click clock) lives only
// on that thread.
#pragma once
#include <QImage>
#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include "model/recorder.h"
#include "net/input.h"
#include "net/reconnect.h"
#include "net/session.h"
#include "shell/browsestore.h"
#include "shell/mapwidget.h"
#include "shell/prompthost.h"
#include "shell/statpanelwidget.h"
#include "skin/liveskin.h"

class RenderThread;

// One row of the right-click verb menu (model/verbmenu.h), as a value that crosses to the GUI thread.
struct VerbMenuItem {
    QString name;
    quint16 id = 0;
    bool viaArgument = false;
    quint8 kind = 0;  // the clicked atom: 1 obj 2 mob 3 turf
    quint32 ref = 0;  // its wire ref (turf: [x-1 u16][y-1 u16])
    QString
        atom;  // its name as the menu row shows it (a runtime-list pick names it to the server: net::atomArgumentText)
};
Q_DECLARE_METATYPE(VerbMenuItem)
Q_DECLARE_METATYPE(QList<VerbMenuItem>)
// One atom of the right-click STACK (render::hitStack: the atoms under the pixel, topmost first): its name and icon as the
// top-level row, its own eligible verbs as that row's submenu (empty = a disabled row that still names what was clicked).
struct MenuAtom {
    QString name;
    QImage icon;
    QList<VerbMenuItem> verbs;
};
Q_DECLARE_METATYPE(MenuAtom)
Q_DECLARE_METATYPE(QList<MenuAtom>)

class SessionWorker : public QThread {
    Q_OBJECT
public:
    SessionWorker(fugue::net::SessionConfig cfg, RenderThread* rt)
        : cfg_(std::move(cfg)),
          rt_(rt),
          rec_(fugue::model::Recorder::fromEnv(cfg_.host + ":" + std::to_string(cfg_.port))) {}
    // FUGUE_RECORD_DIR's recorder (model/recorder.h), or null. Its markers may be written from any thread.
    fugue::model::Recorder* recorder() const { return rec_.get(); }
    void requestStop() { stop_ = true; }
    // Ends the current session and connects again (DreamSeeker's .reconnect, Client > Reconnect). Thread-safe.
    void requestReconnect() { reconnect_ = true; }
    // The live skin the wire's skin / winset / winget traffic drives (shared with the GUI's SkinWindow). Call before
    // start(); without one the worker keeps a private skin and answers wingets from it.
    void setLiveSkin(std::shared_ptr<fugue::skin::LiveSkin> live) { live_ = std::move(live); }
    // The browse_rsc file cache the GUI's local origin serves pages' assets from; this thread fills it. Call before start().
    void setRscStore(std::shared_ptr<fugue::shell::BrowseRscStore> s) { rsc_ = std::move(s); }
    // Sound output spec (audio/sink.h: "" = $FUGUE_AUDIO then auto, "none", "wav:<path>", "cmd:a,b,c"). Call before start().
    void setAudioSpec(std::string spec) { audioSpec_ = std::move(spec); }

    // ---- thread-safe: callable from the GUI thread; each only appends to the inbox ----
    void postMouse(const fugue::shell::MapPointer& p, bool rightClickToWire);
    void postCommand(const QString& line,
                     bool promptNull = false);  // a command-bar line / macro command: verb or `say hi`;
                                                // promptNull: a stat-panel verb click (InputModel::verbCommand)
    void postStockMove(int dir);          // a skin's `.north`...`.southwest`: always the stock move (client.North()...)
    void postTopic(const QString& href);  // byond://?... link click
    // Right click on the map (no right-click=true): ask the session thread which verbs the atom under the cursor offers.
    // The answer comes back as verbMenuReady().
    void postMenuQuery(const fugue::shell::MapPointer& p, const QPoint& globalPos);
    // Invoke a verb chosen from that menu. `viaArgument`: the verb is the player's own and takes the atom as its first
    // argument; else the atom is the verb's src. kind: 1 obj 2 mob 3 turf; ref: the wire ref (a turf's is [x-1 u16][y-1 u16]).
    void postAtomVerb(quint16 verbId, quint8 kind, quint32 ref, bool viaArgument, const QString& atom = QString());

    // alert()/input(): the GUI's outcome for a PromptRequest. The session thread encodes the reply (model/prompt.h);
    // until then the world's DM proc stays parked.
    void postPromptAnswer(quint32 id, bool alert, const QString& text);
    void postPromptCancel(quint32 id, bool alert);
    // A missing verb argument's dialog (PromptRequest::verbArg): OK sends the verb with the value as that argument,
    // verbatim; Cancel sends nothing, as stock DreamSeeker does; None (a nullable argument) sends it null.
    void postArgAnswer(quint32 id, const QString& text);
    void postArgCancel(quint32 id);
    void postArgNone(quint32 id);  // None: the nullable argument goes out null
    // The INFO control: select tab `index` of the strip (StatTab), click the atom row at `row`, run a verb-category
    // entry.
    void postStatTab(int index);
    // an atom row's value was clicked: control = the INFO control's id, button "left"/"middle", mods = StatPanelWidget::RowMods,
    // icon/iconX/iconY = the click in the row's drawn icon cell (StatPanelWidget::rowClicked)
    void postStatRow(int row, const std::string& control, const std::string& button, int mods, bool dbl, bool icon,
                     int iconX, int iconY);
    // DreamSeeker's command-bar completion for a space typed at the end of `line` (net::InputModel::spaceCompletion),
    // answered from a snapshot of the verb list the session thread publishes. Thread-safe; false = type a plain space.
    bool spaceCompletion(const QString& line, QString& out) const;

    std::atomic<uint64_t> framesIngested{0};
signals:
    void loggedIn(const QString& err);  // "" = ok
    void skinLoaded();                  // a (new) skin went into the live skin; the GUI syncs from it on its own
    void verbMenuReady(const QPoint& globalPos, const QString& atomName, const QList<VerbMenuItem>& items);
    void verbStackReady(const QPoint& globalPos, const QList<MenuAtom>& atoms);  // the two-level right-click menu
    void chatLines(const QStringList& html);
    void promptRequested(
        const fugue::shell::PromptRequest& r);  // an alert()/input() the player must answer, or a missing verb argument
    void promptWithdrawn(quint32 id,
                         bool alert);  // the server dropped that alert()/input(): close its dialog unanswered
    void statView(const fugue::shell::StatView& v);  // the INFO control's tab strip + rows (changed)
    void browseHtml(const QString& name, const QString& params,
                    const QString& html);  // a page to show (every Browse, every browse_file offer)
    void outputFile(const QString& control,
                    const QString& html);  // output(file, "win.ctl"): the page for that browser control
    void pageCall(const QString& target, const QString& fn,
                  const QStringList& args);  // output(v, "win.ctl:fn") into a page's JS; target = "win.ctl"
    void browseClosed(const QString& name);
    void externalLink(const QString& url);  // link(url) to a web or mail address, for the desktop to open
    void worldAddress(const QString& host, quint16 port);  // a link() moved this window to another server address
    void worldName(const QString& name);
    void frameTiming(qint64 tickNs, qint64 frameNs);  // the world tick and the client frame (Timing), when they change
    // The main window's icon file (skin/winicon.h): its bytes as the world sent them, or empty for "no icon / not here yet".
    void windowIcon(const QString& name, const QByteArray& bytes);
    void popupMenus(bool shown);                // client.show_popup_menus (each MouseSubs)
    void worldCommand(const QString& command);  // winset(src, null, "command=..."): run as if typed
    void localOutput(
        const QString& line);  // a client-side refusal DreamSeeker prints itself (unknown verb, bad argument)
protected:
    void run() override;

private:
    fugue::net::SessionEnding runSession(fugue::net::Session& s, bool first, bool afterReboot);
    void noteSessionEnd(const std::string& line);  // stderr and <state dir>/session.log
    struct MouseJob {
        fugue::shell::MapPointer p;
        bool rightClick;
    };
    struct CmdJob {
        std::string line;
        bool promptNull = false;
    };
    struct MoveJob {
        int dir;
    };
    struct TopicJob {
        std::string href;
    };
    struct MenuJob {
        fugue::shell::MapPointer p;
        QPoint global;
    };
    struct AtomVerbJob {
        quint16 verbId;
        quint8 kind;
        quint32 ref;
        bool viaArgument;
        std::string atom;
    };
    struct PromptJob {
        quint32 id;
        bool alert;
        bool cancel;
        std::string text;
    };
    struct StatTabJob {
        int index;
    };
    struct StatRowJob {
        int row;
        std::string control, button;
        int mods;
        bool dbl;
        bool icon;
        int iconX, iconY;
    };
    struct ArgJob {
        quint32 id;
        bool cancel;
        std::string text;
        bool none = false;
    };
    using Job = std::variant<MouseJob, CmdJob, MoveJob, TopicJob, MenuJob, AtomVerbJob, PromptJob, StatTabJob,
                             StatRowJob, ArgJob>;
    void post(Job j);
    mutable std::mutex verbsMu_;
    std::shared_ptr<const fugue::net::InputModel> verbSnap_;  // the session thread's im, re-published when Verb lands
    std::mutex inMu_;
    std::deque<Job> inbox_;
    fugue::net::SessionConfig cfg_;
    RenderThread* rt_;
    std::unique_ptr<fugue::model::Recorder> rec_;
    std::shared_ptr<fugue::skin::LiveSkin> live_;
    std::shared_ptr<fugue::shell::BrowseRscStore> rsc_;
    std::string audioSpec_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> reconnect_{false};
    std::string linkTarget_;            // host:port a link() asked to join; session thread only
    std::set<std::string> openPopups_;  // browse() popups open, carried into the next session; session thread only
    std::string sessionLog_;
};

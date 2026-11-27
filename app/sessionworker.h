// SessionWorker owns net::Session and model::Session on its thread. It is the
// model's sole reader and writer; GUI code never accesses the model or socket.
//
// At map cadence, the worker prepares immutable terrain, sprites and geometry,
// publishing them through per-MAP mailboxes. GUI-thread QRhi resources compose
// and present those snapshots. Other UI updates cross as value-type Qt signals.
//
// Input travels back through post*() into a mutex-guarded inbox. The worker
// drains it, revalidates map targets against the current model, and encodes and
// sends network input. Rendered snapshots never grant direct model access.
#pragma once
#include <QByteArray>
#include <QImage>
#include <QRect>
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
#include "shell/mapmetrics.h"
#include "shell/mapmailbox.h"
#include "shell/prompthost.h"
#include "shell/statview.h"
#include "shell/statpanelwidget.h"
#include "skin/liveskin.h"

class SessionWorker : public QThread {
    Q_OBJECT
public:
    explicit SessionWorker(fugue::net::SessionConfig cfg)
        : cfg_(std::move(cfg)), rec_(fugue::model::Recorder::fromEnv(cfg_.host + ":" + std::to_string(cfg_.port))) {}
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
    void setMapMetrics(std::shared_ptr<fugue::shell::MapMetrics> metrics) {
        mapMetrics_ = std::move(metrics);
    }  // before start()
    void setMapRoutes(std::shared_ptr<fugue::shell::MapRoutes> routes) {
        mapRoutes_ = std::move(routes);
    }  // before start()
    void setMapMailbox(std::shared_ptr<fugue::shell::MapMailbox> mailbox) {
        mapMailbox_ = std::move(mailbox);
    }  // before start()

    // ---- thread-safe: callable from the GUI thread; each only appends to the inbox ----
    void postCommand(const QString& line,
                     bool promptNull = false);  // a command-bar line / macro command: verb or `say hi`;
                                                // promptNull: a stat-panel verb click (InputModel::verbCommand)
    void postStockMove(int dir);  // a skin's `.north`...`.southwest`: always the stock move (client.North()...)
    void postMapPointer(const fugue::shell::MapPointer& pointer);
    void postMapMenuPick(quint64 token, int index);
    void postTopic(const QString& href);  // byond://?... link click

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
    // An atom row's value was clicked: control = INFO control id, button "left"/"middle", mods = StatPanelWidget::RowMods.
    void postStatRow(int row, const std::string& control, const std::string& button, int mods, bool dbl,
                     quint64 generation, bool icon, int iconX, int iconY);
    // The pointer over the INFO control's rows (row -1: its empty space), or leaving them: hover reports through the
    // session's one hover record (shell::MapHover), with the maps'.
    void postStatHover(int row, const std::string& control, int mods, quint64 generation, bool icon, int iconX, int iconY);
    void postStatHoverLeft();
    // DreamSeeker's command-bar completion for a space typed at the end of `line` (net::InputModel::spaceCompletion),
    // answered from a snapshot of the verb list the session thread publishes. Thread-safe; false = type a plain space.
    bool spaceCompletion(const QString& line, QString& out) const;

    std::atomic<uint64_t> framesIngested{0};
signals:
    void loggedIn(const QString& err);  // "" = ok
    void skinLoaded();                  // a (new) skin went into the live skin; the GUI syncs from it on its own
    void chatLines(const QStringList& html);
    void promptRequested(
        const fugue::shell::PromptRequest& r);  // an alert()/input() the player must answer, or a missing verb argument
    void promptWithdrawn(quint32 id,
                         bool alert);  // the server dropped that alert()/input(): close its dialog unanswered
    void statView(const fugue::shell::StatView& v);  // the INFO control's tab strip + rows (changed)
    void browseHtml(const QString& name, const QString& params, const QString& html,
                    quint64 skinRevision);  // ordered against later popup winsets
    void outputFile(const QString& control,
                    const QString& html);  // output(file, "win.ctl"): the page for that browser control
    void pageCall(const QString& target, const QString& fn,
                  const QStringList& args);  // output(v, "win.ctl:fn") into a page's JS; target = "win.ctl"
    void browseClosed(const QString& name);
    void externalLink(const QString& url);  // link(url) to a web or mail address, for the desktop to open
    void worldAddress(const QString& host, quint16 port);  // a link() moved this window to another server address
    void worldName(const QString& name);
    void mapMenuReady(const fugue::shell::MapMenu& menu);
    void mapFrameAvailable();                 // coalesced notification only; renderer pulls the latest mailbox frame
    void mapViewSize(int columns, int rows);  // displayed tile extent from Bounds; zero clears the grid
    void hoverName(const QString& name);      // what the pointer is over on the default map, for the status bar (changed)
    void frameTiming(qint64 tickNs, qint64 frameNs);  // the world tick and the client frame (Timing), when they change
    // The main window's icon file (skin/winicon.h): its bytes as the world sent them, or empty for "no icon / not here yet".
    void windowIcon(const QString& name, const QByteArray& bytes);
    void worldCommand(const QString& command);  // winset(src, null, "command=..."): run as if typed
    void localOutput(
        const QString& line);  // a client-side refusal DreamSeeker prints itself (unknown verb, bad argument)
protected:
    void run() override;

private:
    fugue::net::SessionEnding runSession(fugue::net::Session& s, bool first, bool afterReboot);
    void noteSessionEnd(const std::string& line);  // stderr and <state dir>/session.log
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
    struct MapPointerJob {
        fugue::shell::MapPointer pointer;
    };
    struct MapPickJob {
        quint64 token;
        int index;
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
        quint64 generation;
        bool icon;
        int iconX, iconY;
    };
    struct StatHoverJob {
        bool leave = false;
        fugue::shell::StatHover hover;
    };
    quint64 statGeneration_ = 0;  // never reset across reconnects/world replacement
    struct ArgJob {
        quint32 id;
        bool cancel;
        std::string text;
        bool none = false;
    };
    using Job =
        std::variant<CmdJob, MoveJob, TopicJob, PromptJob, StatTabJob, StatRowJob, ArgJob, MapPointerJob, MapPickJob,
                     StatHoverJob>;
    void post(Job j);
    mutable std::mutex verbsMu_;
    std::shared_ptr<const fugue::net::InputModel> verbSnap_;  // the session thread's im, re-published when Verb lands
    std::mutex inMu_;
    std::deque<Job> inbox_;
    fugue::net::SessionConfig cfg_;
    std::unique_ptr<fugue::model::Recorder> rec_;
    std::shared_ptr<fugue::skin::LiveSkin> live_;
    std::shared_ptr<fugue::shell::BrowseRscStore> rsc_;
    std::string audioSpec_;
    std::shared_ptr<fugue::shell::MapMetrics> mapMetrics_;
    std::shared_ptr<fugue::shell::MapMailbox> mapMailbox_;
    std::shared_ptr<fugue::shell::MapRoutes> mapRoutes_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> reconnect_{false};
    std::string linkTarget_;            // host:port a link() asked to join; session thread only
    std::set<std::string> openPopups_;  // browse() popups open, carried into the next session; session thread only
    uint64_t mapSession_ = 0, mapMenuToken_ = 0;
    std::string sessionLog_;
};

// BrowseHost routes the browse() pages a world sends, and the replies those pages send back.
//
//   world -> client   open(name, params, html)  a page goes into the skin control declared by that name (docked),
//                                               else into a top-level popup (transient for the main window;
//                                               can_resize=0 pins min=max; titlebar=0 is frameless)
//                     close(name)
//                     pageCall(win, fn, args)   output(v, "win.ctl:fn"): window.fn(...) in that page, after READY
//   page -> client    byond://?...              a Topic (emitted as byondLink); '?..' navigation is the same
//                     byond://winset?id=X&size=..;pos=..   a popup window's geometry, or the skin's winset
//                     byond://winset?ctl.prop=v;command=..  split on ';' AND '&'; `command` runs as if typed
//                     byond://winget?id=&property=&callback=cb  answered LOCALLY: cb({...}) is called with an object
//                                               in the asking page; never a Topic
//                     cef_to_byond(url) / hubStorage   the same, over the XHR bridge (the reply is JS)
//   hidden            a page for a BROWSER control declared `is-visible = false` runs in a windowless pane; its JS
//                     is where vg's jukebox audio plays.
//   chat              a page opened into the skin's chat control is the world's own chat page; chat lines go to its
//                     output() once it is READY.
//
// GUI thread only.
#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>
#include <functional>
#include <memory>
#include "model/browsing.h"
#include "model/headlessacks.h"
#include "shell/browserpane.h"
#include "shell/browsestore.h"
#include "shell/localorigin.h"
#include "skin/liveskin.h"

class QLockFile;
class QWebEngineProfile;
class QWidget;

namespace fugue::shell {

class BrowseHost : public QObject {
    Q_OBJECT
public:
    // anchor: the main window; popups are transient for it.
    explicit BrowseHost(QWidget* anchor, QObject* parent = nullptr);
    // Where pages keep their web storage (IndexedDB, localStorage) between runs, as stock DreamSeeker's browser does.
    // Set before the first BrowseHost is made. "" (the default), or a directory another running client holds, keeps
    // storage for this run only. tgui's chat panel shows nothing until it finds its saved chat state.
    static void setStorageDir(const QString& dir);
    ~BrowseHost() override;

    std::shared_ptr<BrowseRscStore> store() const { return store_; }
    LocalOrigin* origin() { return origin_; }
    QWebEngineProfile* profile() const { return profile_; }
    void setLiveSkin(std::shared_ptr<skin::LiveSkin> live) { live_ = std::move(live); }
    // Set at shutdown: a popup closed afterwards is not closed by the player and runs no on-close.
    void setQuitting() { quitting_ = true; }
    // Does the skin declare a BROWSER control by this name ("ctl" or "window.ctl")? Decides docked vs popup.
    void setDeclaredPredicate(std::function<bool(const QString&)> f) { declared_ = std::move(f); }
    // A hidden skin window (not a control) that holds a BROWSER: its page runs windowless (SkinWindow::hiddenWindowBrowser).
    void setHiddenWindowPredicate(std::function<bool(const QString&)> f) { hiddenWindow_ = std::move(f); }
    // The chat lines already shown (replayed into the chat page when it first appears).
    void setChatBacklog(std::function<QStringList()> f) { backlog_ = std::move(f); }
    void setHubStorePath(const QString& p);

    void open(const QString& name, const QString& params, const QString& html, quint64 skinRevision = ~quint64(0));
    void close(const QString& name);
    void pageCall(const QString& window, const QString& fn, const QStringList& args);
    // The skin created or destroyed the BROWSER control `ctl` (in skin window `win`); the skin owns the pane.
    // byWindow: a browse() or output() naming the window `win` itself reaches this control too. Off for a control a
    // winset created at runtime, which shares its window with the .dmf's own controls (vg's tooltips in mapwindow).
    void adoptControl(const QString& win, const QString& ctl, BrowserPane* pane, bool byWindow = true);
    // The windowless page already running for that control, handed to the skin as the control's own pane with its page
    // state intact, or nullptr. Use it in place of a new pane, and do not adoptControl() it.
    BrowserPane* takeHidden(const QString& win, const QString& ctl);
    void releaseControl(BrowserPane* pane);
    // Applies new world title/visibility writes, including repeated values, without replaying old writes
    // over page/player changes. Hiding retains the running document and does not invoke on-close.
    void syncPopupProps(const skin::PropStore& props);

    // ---- chat
    bool chatHosted() const { return chatPane_ != nullptr; }
    bool chatReady() const { return chatPane_ && chatPane_->ready(); }
    void chatLine(const QString& html);

    // ---- introspection (winget and the test hooks)
    BrowserPane* paneFor(const QString& window) const;
    BrowserPane* paneForTarget(const QString& target) const;  // an output() target "[window.]control" -> its page
    QWidget* popupFor(const QString& window) const;
    // winget(null, "focus") for a widget inside a popup: "<popup>.browser"; "" when the widget is not in one.
    QString focusIdFor(QWidget* w) const;
    bool isHiddenPage(const QString& window) const;  // runs in a windowless pane (an is-visible=false skin control)
    QStringList openWindows() const;
    // winget(id, property-list) as the object a page's callback receives.
    QJsonObject wingetObject(const QString& id, const QString& props) const;
    // One byond:// URL from a page, already parsed. Returns the JS the pane should evaluate ("" = none).
    QString handleCall(BrowserPane* from, const model::ByondUrl& u);

signals:
    void byondLink(const QUrl& url);                // a Topic for the world
    void commandRequested(const QString& command);  // winset command=...
    void chatPageReady();
    void worldLink(const QString& url);  // a page link to another world (byond://host:port)
    void popupClosedByPlayer(const QString& name);
    // The world's `on-close` command for a popup the player closed (never one the world closed itself). It is set
    // by winset and runs exactly as if typed; see routeWorldCommand in shell/inputrouter.h.
    void onCloseCommand(const QString& name, const QString& command);

private:
    struct Slot {
        QString name, file, win;  // win: the skin window of a docked control
        BrowserPane* pane = nullptr;
        QWidget* popup = nullptr;
        model::BrowseParams p;
        bool docked = false, chat = false;
        bool hidden = false;  // unplaced skin browser: live page, adopted when its control is placed
        quint64 worldTitleWrite = 0, worldVisibilityWrite = 0;  // last consumed property-write stamps
    };
    struct Pending {
        QString params, html;
    };
    struct Ctl {
        QString win, ctl;
        BrowserPane* pane;
        bool byWindow = true;  // adoptControl
    };
    BrowserPane* makePane(QWidget* parent);
    Slot* slotFor(const QString& window);
    const Slot* slotFor(const QString& window) const;
    void show(Slot& s, const QString& html);
    void openHidden(const QString& name, const QString& file, const model::BrowseParams& p, const QString& html);
    void dropHidden(const QString& name);
    void wire(BrowserPane* p);
    QString applyWinset(BrowserPane* from, const model::ByondUrl& u);
    void applyPopupProps(Slot& s, const QString& prop, const QString& val, quint64 throughRevision = ~quint64(0));
    QString jsCallback(const QString& cb, const QJsonValue& v) const;
    static bool validCallback(const QString& cb);
    void topicFrom(const QString& raw);
    QJsonObject macroSet(const QString& set) const;

    QWidget* anchor_;
    QWebEngineProfile* profile_ = nullptr;
    QLockFile* storageLock_ = nullptr;
    std::shared_ptr<BrowseRscStore> store_ = std::make_shared<BrowseRscStore>();
    LocalOrigin* origin_ = nullptr;
    std::shared_ptr<skin::LiveSkin> live_;
    bool quitting_ = false;
    std::function<bool(const QString&)> declared_, hiddenWindow_;
    std::function<QStringList()> backlog_;
    QHash<QString, Slot> slots_;
    QList<Ctl> controls_;
    model::HeadlessAcks navAcks_;            // asset_cache_browser handshakes, answered natively
    QHash<QString, Pending> pendingDocked_;  // declared by the skin, control not yet created
    QHash<int, QPointer<BrowserPane>> panes_;
    BrowserPane* chatPane_ = nullptr;
    int nextPane_ = 1, chatLines_ = 0;
    QHash<QString, QString> hub_;
    QString hubPath_;
    QSet<QString> dropLogged_;
};

}  // namespace fugue::shell

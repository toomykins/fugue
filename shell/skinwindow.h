// SkinWindow: the DreamSeeker-shaped main window, a plain QWidget tree painted from a LiveSkin (skin/liveskin.h):
// the parsed .dmf, the winset overlay and the layout they imply.
//   MAP -> QRhi tile grid   OUTPUT -> QTextBrowser (chat)   INFO -> StatPanelWidget (tabs and rows)
//   INPUT -> QLineEdit    BROWSER -> BrowserPane          BUTTON and LABEL -> plain widgets (text from the overlay)
//   everything else (TAB, GRID, BAR, GAUGE...) -> an empty placeholder frame; CHILD and MAIN -> nothing.
//   The skin's `menu` -> a QMenuBar. A command is emitted as menuCommand and routed like a macro. Its top-left
//   corner holds the system-menu entries (Options & Messages, Client) behind a button showing the window icon, as
//   stock puts them under the caption icon (shell/macrodialog.h).
//
// Live state: every property a winset sets (is-visible, pos, size, text, is-checked, colours, a CHILD's splitter,
// the window title, the active macro set, focus) lives in the LiveSkin. The session thread and a skin button's
// `.winset` both mutate it. This widget never mutates it directly: syncFromLive() takes a consistent snapshot and
// brings the widgets to it. A widget is created the first time its control is visible and placed, since a
// BrowserPane is a whole Chromium view. After that it is only hidden.
// Fonts: a BUTTON, LABEL, INPUT or INFO takes its font-family, font-size and font-style (file or winset) as stock does,
// at a pixel size (shell/skinfont.h). The menu bar and status bar use shell/skinfont.h's menu font.
// Splitters: each CHILD divider the layout places (skin::Tree::splitters) gets a SplitterHandle. A drag goes into the
// LiveSkin (setSplitter) and the tree re-lays out, so the layout stays the only source of truth.
// Floating windows: a skin window that is neither the main window nor a pane is a top-level window of its own, hidden
// until a winset of its is-visible shows it (tg's tgui_say). Closing one hides it.
// Not implemented: TAB controls (tab panes),
// an OUTPUT's font, a LABEL's text-wrap, pos and size of the main window itself, a CHILD's `lock`, and saved-params persistence of a
// dragged splitter.
#pragma once
#include <functional>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include "shell/browsehost.h"
#include "shell/mapmetrics.h"
#include "shell/mapmailbox.h"
#include "shell/statpanelwidget.h"
#include "skin/layout.h"
#include "skin/liveskin.h"
#include "skin/macro.h"

class QTextBrowser;
class QLineEdit;
class QAction;
class QMenuBar;
class QToolButton;
class QLabel;

namespace fugue::shell {

class BrowserPane;

class SkinWindow : public QWidget {
    Q_OBJECT
public:
    explicit SkinWindow(QWidget* parent = nullptr);
    ~SkinWindow() override;
    // Parses (into the live skin) and builds. Returns "" or the parse error (the previous skin stays).
    QString loadSkin(const std::string& dmf);
    bool hasSkin() const { return live_->loaded(); }
    // The window size that gives the skin its designed client area (menu bar and status strip included), or the client
    // area a winset of the main window's size has asked for since the skin was built: stock opens the skin at the size
    // a world sets in Login() (516.1689).
    QSize designSize() const;

    // The live skin this window paints. Share it with the session thread (SkinFeed) so a wire winset reaches it.
    std::shared_ptr<skin::LiveSkin> live() const { return live_; }
    // Bring the widgets to the live skin's current state. GUI thread. Cheap when nothing changed; also reachable as
    // a queued call from the live skin's change callback (any thread).
    void syncFromLive();
    // The title bar. A main-window title from the world (its .dmf or a winset) wins, then world.name, then "Fugue".
    // The built-in default skin's own title is never shown: markBuiltinSkin() is called after loading it.
    void setWorldName(const QString& n) {
        worldName_ = n;
        updateTitle();
    }
    void markBuiltinSkin() {
        builtinFileId_ = builtFileId_;
        updateTitle();
    }
    // The window icon: the world's (the skin's MAIN `icon`, or a winset of it; skin/winicon.h) when `bytes` decode,
    // else Fugue's (shell/fugueicon.h). Empty `bytes` means the world names none or its file has not arrived.
    // browse() popups follow it; the client's own dialogs keep Fugue's.
    void setSkinIcon(const QString& name, const QByteArray& bytes);
    bool hasSkinIcon() const { return skinIcon_; }
    // Command-bar completion, asked when a space is typed at the end of an INPUT control. Returns the bar's new text
    // (`say` becomes `say "`) and true, or false to type a plain space. Unset means plain spaces.
    using SpaceCompleter = std::function<bool(const QString& line, QString& out)>;
    void setSpaceCompleter(SpaceCompleter c) { spaceCompleter_ = std::move(c); }

    void appendChat(const QStringList& html);
    void setStat(const QString& html);    // HTML stat text; the INFO control uses setStatView()
    void setStatView(const StatView& v);  // the model's tab strip + rows (shell/statpanelwidget.h)
    // browse() traffic from the session thread, as queued signals. Routing (skin control or popup, READY gating, the
    // chat page) is done by the BrowseHost (shell/browsehost.h).
    void showBrowse(const QString& name, const QString& params, const QString& html,
                    quint64 skinRevision = ~quint64(0));
    void closeBrowse(const QString& name);
    void showOutputFile(const QString& control, const QString& html);
    void pageCall(const QString& window, const QString& fn, const QStringList& args);  // output(v, "win.ctl:fn")
    // Local .output currently supports browser function calls, not OUTPUT text/replacement forms.
    BrowseHost* browseHost() const { return host_; }
    std::shared_ptr<BrowseRscStore> rscStore() const { return host_->store(); }
    bool declaresBrowser(const QString& name) const;  // does the skin file declare a BROWSER control by this name?
    bool declaresHiddenBrowser(const QString& name) const;  // ... and declare it `is-visible = false`?
    // `.output control[:function] value`: a page call into that browser. False when it names no page function.
    bool clientOutput(const QString& control, const QString& encodedValue);
    // The pane of a BROWSER a winset created at runtime inside a skin window (vg's tooltips); nullptr when none is built.
    BrowserPane* runtimeBrowser(const QString& name) const;
    bool hiddenWindowBrowser(const QString& name) const;  // a hidden skin window (not a control) with a BROWSER?

    void setMapViewSize(int columns, int rows);
    // What the pointer is over on the default map: the status bar shows it.
    void setHoverName(const QString& name);
    void mapFrameAvailable();
    void showMapMenu(const MapMenu& menu);
    std::shared_ptr<MapMetrics> mapMetrics() const { return mapMetrics_; }
    std::shared_ptr<MapMailbox> mapMailbox() const { return mapMailbox_; }
    std::shared_ptr<MapRoutes> mapRoutes() const { return mapRoutes_; }
    void setMapMetricsVisible(bool visible);
    bool mapMetricsVisible() const { return mapMetricsVisible_; }
    QWidget* map() const { return map_; }  // default MAP grid
    skin::MacroTable macros() const { return snap_.macros; }
    QString activeMacro() const { return QString::fromStdString(snap_.macro); }
    const skin::Tree& tree() const { return snap_.tree; }
    int controlCount() const { return int(widgets_.size()); }
    QString describe() const;  // one line per control: type name rect
    QWidget* controlWidget(const QString& windowDotName) const;
    // Keyboard macro MAP substitutions, sampled on the GUI thread in control-local logical pixels.
    // Bare pairs become two numeric arguments; quoted pairs become comma-separated text.
    // Other properties/unresolved controls remain untouched (not general skin-expression support).
    std::string expandMapCommandParams(const std::string& command) const;
    // The drag handle of a CHILD's divider by its "window.elem" path (nullptr when the layout places none).
    QWidget* splitterHandle(const QString& windowDotName) const;
    int menuBarHeight() const;
    QMenuBar* menuBar() const { return bar_; }
    // One client-owned popup, shared with the optional menu-bar corner and retained across skin replacement.
    QMenu* clientMenu() const { return clientMenu_; }
    void toggleClientMenu();
    // The system-menu button in the menu bar's top-left corner (nullptr until the skin has a menu bar).
    QToolButton* systemMenuButton() const { return sysBtn_; }
    // The main window's status strip along the bottom edge, below every control (0 when the skin's `statusbar` is
    // false). See kStatusBarH in skinwindow.cpp.
    int statusBarHeight() const;
    QLabel* statusBar() const { return status_; }

signals:
    void mapPointer(const fugue::shell::MapPointer& pointer);
    void mapMenuPicked(quint64 token, int index);
    void commandEntered(const QString& text);    // INPUT Enter: the app routes it (shell/inputrouter.h) to the session
    void buttonCommand(const QString& command);  // a skin BUTTON's `command`: routed exactly like a macro's
    void menuCommand(const QString& command);    // a menu item's `command`: ditto
    void macroChanged(const QString& name);   // the active macro set, or a set's bindings, differs from the last sync's
    void clientResized(const QSize& client);  // the skin's client area changed size (the app fires the skin's on-size)
    void byondLink(const QUrl& url);
    void worldLink(const QString& url);  // a page link to another world
    void worldCommand(
        const QString& command);       // a command the world registered, such as a popup's on-close (routeWorldCommand)
    void statTabRequested(int index);  // INFO: a tab was clicked; the session sends StatTab
    void statRowClicked(int row, const QString& control, const QString& button, int mods, bool dbl, quint64 generation,
                        bool icon, int iconX, int iconY);  // INFO: the session clicks that atom
    void statVerbClicked(const QString& verb);             // INFO: a verb-category entry; the session runs it
    // INFO: the pointer over the rows (row -1: the panel's empty space) and leaving them (StatPanelWidget::rowHovered).
    void statRowHovered(int row, const QString& control, int mods, quint64 generation, bool icon, int iconX, int iconY);
    void statHoverLeft();
protected:
    void resizeEvent(QResizeEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void showEvent(QShowEvent*) override;
    void changeEvent(QEvent*) override;
    // Closing the main window quits the client. The popups are transient for it, so the browse host is told first,
    // and the popups that close with it run no on-close.
    void closeEvent(QCloseEvent* e) override;
    // The background-color of each MAIN and CHILD control (the main window, a pane, a splitter's area), behind the widgets.
    void paintEvent(QPaintEvent*) override;

private:
    std::vector<std::pair<QRect, QColor>> fills_;
    SpaceCompleter spaceCompleter_;
    QString worldName_, skinTitle_;
    bool skinIcon_ = false;
    uint64_t builtinFileId_ = ~0ull;
    void updateTitle();
    void clearWidgets();
    void publishMapSizes();
    void rebuild(const skin::SkinSnapshot& s);
    void apply(const skin::SkinSnapshot& s);
    QWidget* makeWidget(const skin::Control& c);
    void applyProps(const skin::Control& c, QWidget* w, const skin::SkinSnapshot& s);
    void rebuildMenu(const skin::Menu& m);
    void syncMenuChecks(const skin::Menu& m);
    skin::Rect clientRect() const;
    void syncStatusBar(const skin::SkinSnapshot& s);
    int chromeHeight() const { return menuBarHeight() + statusBarHeight(); }
    // The environment half of winget(client, null, ...) (skin/liveskin.cpp kGlobalParams): dpi, screen position, screen size.
    void publishClientEnv();
    void publishFocus(QWidget* now);  // liveskin.h setGuiFocus
    void markMapShown();
    void markInfoShown();
    // Floating skin windows (skin/layout.h Tree::floats): one top-level widget each, made on first use.
    QWidget* floatWindow(const std::string& window, const skin::SkinSnapshot& s);
    void syncFloats(const skin::SkinSnapshot& s);
    std::map<std::string, QWidget*> floats_;       // lower-case window name -> its top-level widget
    std::map<std::string, std::string> floatPos_;  // the pos last applied to each

    std::shared_ptr<skin::LiveSkin> live_;
    skin::SkinSnapshot snap_;
    uint64_t builtFileId_ = 0, lastGen_ = ~0ull, lastFocusSeq_ = 0;
    uint64_t lastWindowSizeSeq_ = 0;  // the last main-window size winset given to this window (SkinSnapshot)
    uint64_t lastWindowPosSeq_ = 0, lastMaximizeSeq_ = 0;  // the same for its pos and is-maximized winsets
    QSize requestedClient_;  // the client area the last size winset asked for since the skin was built (designSize)
    QSize normalClient_;     // the client area while neither maximized nor full-screen (the restore size)
    QSize restoreClient_;    // a size winset that arrived while maximized: given to the window when it is restored
    std::optional<QPoint> restorePos_;  // likewise a pos winset
    bool zoomed_ = false;               // maximized or full-screen, as last published (LiveSkin::setWindowState)
    void publishWindowState();
    void publishFrame();
    std::string lastMacro_;
    uint64_t lastMacroGen_ = 0;
    std::map<std::string, QWidget*> widgets_;  // "window.name" -> widget (created lazily)
    std::map<std::string, std::string> types_;
    std::map<std::string, QWidget*> handles_;  // CHILD "window.elem" -> its SplitterHandle
    QWidget* map_ = nullptr;
    QSize mapViewSize_{0, 0};
    std::shared_ptr<MapMetrics> mapMetrics_ = std::make_shared<MapMetrics>();
    std::shared_ptr<MapMailbox> mapMailbox_ = std::make_shared<MapMailbox>(mapMetrics_);
    std::shared_ptr<MapRoutes> mapRoutes_ = std::make_shared<MapRoutes>();
    bool mapMetricsVisible_ = false;
    QPointer<QMenu> mapMenu_;
    bool mapPaneShown_ = false, infoPaneShown_ = false;  // LiveSkin::setPaneShown sent (per skin load)
    bool hasMapFrame_ = false;
    QSize lastClient_;
    QMenuBar* bar_ = nullptr;
    QToolButton* sysBtn_ = nullptr;
    QMenu* clientMenu_ = nullptr;
    QLabel* status_ = nullptr;
    QString hoverName_;  // status_'s text, kept while the skin hides the bar
    QString menuSig_;
    bool menuSigValid_ = false;
    // Actions of can-check items, which may share a name, and the is-checked state last shown for each. syncMenuChecks
    // pushes only a change, so a click's own write never re-checks a same-named sibling.
    std::multimap<std::string, QAction*> menuActs_;
    std::map<std::string, bool> menuChk_;
    QList<QTextBrowser*> outputs_;
    QList<StatPanelWidget*> infos_;
    StatView statView_;
    QHash<QString, BrowserPane*> browsers_;  // by control name (the part after the last '.')
    QStringList chat_;
    QString stat_;
    BrowseHost* host_ = nullptr;
    QSize design_{640, 480};
    bool syncing_ = false, syncQueued_ = false;
};

}  // namespace fugue::shell

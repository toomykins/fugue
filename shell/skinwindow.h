// SkinWindow: the DreamSeeker-shaped main window, a plain QWidget tree painted from a LiveSkin (skin/liveskin.h):
// the parsed .dmf, the winset overlay and the layout they imply.
//   MAP -> MapWidget      OUTPUT -> QTextBrowser (chat)   INFO -> StatPanelWidget (tabs and rows)
//   INPUT -> QLineEdit    BROWSER -> BrowserPane          BUTTON and LABEL -> plain widgets (text from the overlay)
//   everything else (TAB, GRID, BAR, GAUGE...) -> an empty placeholder frame; CHILD and MAIN -> nothing.
//   The default MAP is map(); every other MAP is a secondary map (setAuxMapFrame).
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
// at a pixel size (shell/skinfont.h), so it never depends on the platform's default font or dpi; a BUTTON's and a
// LABEL's text is laid out at stock's advance widths. The menu bar and status bar use shell/skinfont.h's menu font.
// Splitters: each CHILD divider the layout places (skin::Tree::splitters) gets a SplitterHandle. A drag goes into the
// LiveSkin (setSplitter) and the tree re-lays out, so the layout stays the only source of truth.
// Not implemented: TAB controls (tab panes), runtime-created maps outside a browse() popup, separate top-level skin windows (pane windows hosted in splitters are laid out),
// an OUTPUT's font, a LABEL's text-wrap, pos and size of the main window itself, a CHILD's `lock`, and saved-params persistence of a
// dragged splitter.
#pragma once
#include <functional>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include "shell/browsehost.h"
#include "shell/mapwidget.h"
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
class DisplaySettings;

class SkinWindow : public QWidget {
    Q_OBJECT
public:
    explicit SkinWindow(QWidget* parent = nullptr);
    ~SkinWindow() override;
    // Parses (into the live skin) and builds. Returns "" or the parse error (the previous skin stays).
    QString loadSkin(const std::string& dmf);
    bool hasSkin() const { return live_->loaded(); }
    // The window size that gives the skin its designed client area (menu bar included).
    QSize designSize() const;

    // The live skin this window paints. Share it with the session thread (SkinFeed) so a wire winset reaches it.
    std::shared_ptr<skin::LiveSkin> live() const { return live_; }
    // Bring the widgets to the live skin's current state. GUI thread. Cheap when nothing changed; also reachable as
    // a queued call from the live skin's change callback (any thread).
    void syncFromLive();
    // The title bar. A main-window title from the world (its .dmf or a winset) wins, then world.name, then "Fugue".
    // The built-in default skin's own title is never shown: markBuiltinSkin() is called after loading it.
    void setWorldName(const QString& n) { worldName_ = n; updateTitle(); }
    void markBuiltinSkin() { builtinFileId_ = builtFileId_; updateTitle(); }
    // The window icon: the world's (the skin's MAIN `icon`, or a winset of it; skin/winicon.h) when `bytes` decode,
    // else Fugue's (shell/fugueicon.h). Empty `bytes` means the world names none or its file has not arrived.
    // browse() popups follow it; the client's own dialogs keep Fugue's.
    void setSkinIcon(const QString& name, const QByteArray& bytes);
    bool hasSkinIcon() const { return skinIcon_; }
    // Command-bar completion, asked when a space is typed at the end of an INPUT control. Returns the bar's new text
    // (`say` becomes `say "`) and true, or false to type a plain space. Unset means plain spaces.
    using SpaceCompleter = std::function<bool(const QString& line, QString& out)>;
    void setSpaceCompleter(SpaceCompleter c) { spaceCompleter_ = std::move(c); }

    void setMapFrame(const QImage& img, HitPtr hit = {});
    void setMapExternalFrame(QSize size, HitPtr hit, std::function<QImage()> snapshot);
    QPixmap grabWithMaps(); // QWidget::grab plus native map presenters, which QWidget cannot capture itself
    // The render thread skipped an unchanged list: the frame on screen stands and only the hit scene is replaced.
    // A map widget built since the last frame gets that frame, so it never waits for the picture to change.
    void setMapHit(HitPtr hit);
    // A SECONDARY map control's picture (app/renderthread.h auxFrame): a map a winset created at runtime inside a browse()
    // popup - vg's security camera console, tgui's ByondUi (`winset id parent=<popup>;type=map;pos=x,y;size=WxH`). The
    // widget lives in the popup at the control's pos/size (skin/liveskin.h runtime_) and auto-fits the picture
    // like the default map (render/scene.h assembleSecondaryMap). `parent=` (ByondUi's unmount) destroys it.
    // The .dmf's other MAP controls are secondary maps too, under their elem name. `hit` is the scene the picture was
    // drawn from (render/scene.h assembleSecondaryMap withHit): the widget takes clicks, drags, the wheel and hover on it.
    void setAuxMapFrame(const QString& id, const QImage& img, HitPtr hit = {});
    void setAuxMapHit(const QString& id, HitPtr hit);   // the render thread skipped an unchanged list
    MapWidget* auxMap(const QString& id) const;   // nullptr when no such control is built
    QList<MapWidget*> mapWidgets() const;         // the default map and every secondary map built
    void appendChat(const QStringList& html);
    void setStat(const QString& html);   // HTML stat text; the INFO control uses setStatView()
    void setStatView(const StatView& v);   // the model's tab strip + rows (shell/statpanelwidget.h)
    // browse() traffic from the session thread, as queued signals. Routing (skin control or popup, READY gating, the
    // chat page) is done by the BrowseHost (shell/browsehost.h).
    void showBrowse(const QString& name, const QString& params, const QString& html);
    void closeBrowse(const QString& name);
    void showOutputFile(const QString& control, const QString& html);
    void pageCall(const QString& window, const QString& fn, const QStringList& args);   // output(v, "win.ctl:fn")
    BrowseHost* browseHost() const { return host_; }
    std::shared_ptr<BrowseRscStore> rscStore() const { return host_->store(); }
    bool declaresBrowser(const QString& name) const;   // does the skin file declare a BROWSER control by this name?
    bool declaresHiddenBrowser(const QString& name) const;   // ... and declare it `is-visible = false`?

    DisplaySettings* displaySettings() const { return display_; }
    MapWidget* map() const { return map_; }
    // The map control's rectangle inside this widget (for screenshot cropping).
    QRect mapControlRect() const { return map_ ? map_->geometry() : QRect(); }
    skin::MacroTable macros() const { return snap_.macros; }
    QString activeMacro() const { return QString::fromStdString(snap_.macro); }
    const skin::Tree& tree() const { return snap_.tree; }
    int controlCount() const { return int(widgets_.size()); }
    QString describe() const;   // one line per control: type name rect
    QWidget* controlWidget(const QString& windowDotName) const;
    // The drag handle of a CHILD's divider by its "window.elem" path (nullptr when the layout places none).
    QWidget* splitterHandle(const QString& windowDotName) const;
    int menuBarHeight() const;
    QMenuBar* menuBar() const { return bar_; }
    // The system-menu button in the menu bar's top-left corner (nullptr until the skin has a menu bar).
    QToolButton* systemMenuButton() const { return sysBtn_; }
    // The main window's status strip along the bottom edge, below every control (0 when the skin's `statusbar` is
    // false). See kStatusBarH in skinwindow.cpp.
    int statusBarHeight() const;
    QLabel* statusBar() const { return status_; }

signals:
    void commandEntered(const QString& text);   // INPUT Enter: the app routes it (shell/inputrouter.h) to the session
    void buttonCommand(const QString& command); // a skin BUTTON's `command`: routed exactly like a macro's
    void menuCommand(const QString& command);   // a menu item's `command`: ditto
    void macroChanged(const QString& name);     // the active macro set, or a set's bindings, differs from the last sync's
    void mapReady(fugue::shell::MapWidget* map);// a map widget (default or secondary) was (re)created: connect its pointer signals
    void clientResized(const QSize& client);    // the skin's client area changed size (the app fires the skin's on-size)
    void byondLink(const QUrl& url);
    void worldLink(const QString& url);         // a page link to another world
    void worldCommand(const QString& command);  // a command the world registered, such as a popup's on-close (routeWorldCommand)
    void statTabRequested(int index);       // INFO: a tab was clicked; the session sends 0x006b
    void statRowClicked(int row, const QString& control, const QString& button, int mods, bool dbl, bool icon, int iconX, int iconY);   // INFO: the session clicks that atom
    void statVerbClicked(const QString& verb);   // INFO: a verb-category entry; the session runs it
    // A secondary map's widget was (re)built: the render thread must draw that map's next list even when it is
    // unchanged (app/renderthread.h forgetAux), so a remounted widget always gets a frame.
    void auxMapBuilt(const QString& id);
    void auxMapRemoved(const QString& id);
protected:
    void resizeEvent(QResizeEvent*) override;
    void moveEvent(QMoveEvent*) override;
    void showEvent(QShowEvent*) override;
    // Closing the main window quits the client. The popups are transient for it, so the browse host is told first,
    // and the popups that close with it run no on-close.
    void closeEvent(QCloseEvent* e) override;
    // The background-color of each MAIN and CHILD control (the main window, a pane, a splitter's area), behind the widgets.
    void paintEvent(QPaintEvent*) override;
private:
    std::vector<std::pair<QRect, QColor>> fills_;
    DisplaySettings* display_ = nullptr;
    SpaceCompleter spaceCompleter_;
    QString worldName_, skinTitle_;
    bool skinIcon_ = false;
    uint64_t builtinFileId_ = ~0ull;
    void updateTitle();
    void clearWidgets();
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
    void markMapShown();
    void publishFocus(QWidget* now);   // liveskin.h setGuiFocus
    void markInfoShown();
    void syncRuntimeMaps(const skin::SkinSnapshot& s);

    std::shared_ptr<skin::LiveSkin> live_;
    skin::SkinSnapshot snap_;
    uint64_t builtFileId_ = 0, lastGen_ = ~0ull, lastFocusSeq_ = 0;
    std::string lastMacro_;
    uint64_t lastMacroGen_ = 0;
    std::map<std::string, QWidget*> widgets_;   // "window.name" -> widget (created lazily)
    std::map<std::string, std::string> types_;
    std::map<std::string, QWidget*> handles_;   // CHILD "window.elem" -> its SplitterHandle
    MapWidget* map_ = nullptr;
    MapWidget* lastMapSignalled_ = nullptr;
    std::map<std::string, QPointer<MapWidget>> auxMaps_;   // runtime MAP controls by lower-case id (syncRuntimeMaps)
    std::map<std::string, QImage> auxImg_;                 // their latest pictures, for a widget built after its frame
    std::map<std::string, HitPtr> auxHit_;                 // ... and the scenes they were drawn from
    std::set<std::string> dmfAux_;                         // the auxMaps_ keys that are .dmf controls (built by makeWidget)
    std::vector<QPointer<MapWidget>> pendingMaps_;         // .dmf secondary maps built in this sync: mapReady after it
    bool mapPaneShown_ = false, infoPaneShown_ = false;   // LiveSkin::setPaneShown sent (per skin load)
    QImage mapImg_;   // the last CPU frame handed to the map (implicitly shared with the widget's copy)
    bool hasMapFrame_ = false;
    QSize lastClient_;
    QMenuBar* bar_ = nullptr;
    QToolButton* sysBtn_ = nullptr;
    QLabel* status_ = nullptr;
    QString menuSig_;
    // Actions of can-check items, which may share a name, and the is-checked state last shown for each. syncMenuChecks
    // pushes only a change, so a click's own write never re-checks a same-named sibling.
    std::multimap<std::string, QAction*> menuActs_;
    std::map<std::string, bool> menuChk_;
    QList<QTextBrowser*> outputs_;
    QList<StatPanelWidget*> infos_;
    StatView statView_;
    QHash<QString, BrowserPane*> browsers_;     // by control name (the part after the last '.')
    QStringList chat_;
    QString stat_;
    BrowseHost* host_ = nullptr;
    QSize design_{640, 480};
    bool syncing_ = false, syncQueued_ = false;
};

}  // namespace fugue::shell

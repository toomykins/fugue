#include "shell/skinwindow.h"
#include "shell/macrodialog.h"
#include "shell/mapmenu.h"
#include "shell/mapwidget.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <QCloseEvent>
#include <QCursor>
#include <cstdlib>
#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QScreen>
#include <QScrollBar>
#include <QTextBrowser>
#include <QWindow>
#include <cstdio>
#include "model/browsing.h"
#include "shell/browserpane.h"
#include "shell/fugueicon.h"
#include "shell/paste.h"
#include "shell/skinfont.h"
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace fugue::shell {

namespace {
// A space typed at the end of the command bar asks the completer (`say` becomes `say "`). Anywhere else, or when
// the completer declines, the space types normally.
class SpaceCompleterFilter : public QObject {
public:
    SpaceCompleterFilter(QLineEdit* le, const SkinWindow::SpaceCompleter* c) : QObject(le), le_(le), c_(c) {}
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::KeyPress || !*c_) return QObject::eventFilter(o, e);
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() != Qt::Key_Space || (k->modifiers() & ~Qt::KeypadModifier) || le_->hasSelectedText() ||
            le_->cursorPosition() != le_->text().size())
            return QObject::eventFilter(o, e);
        QString out;
        if (!(*c_)(le_->text(), out)) return QObject::eventFilter(o, e);
        le_->setText(out);
        le_->setCursorPosition(out.size());
        return true;
    }

private:
    QLineEdit* le_;
    const SkinWindow::SpaceCompleter* c_;
};

// The verb menu takes no notice of the middle button: a middle press off it neither closes it nor reaches the map, and
// one on it picks nothing (stock 516.1689, tests/data/hover-world round3 mmidother).
class MenuIgnoresMiddle : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
        if ((e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonRelease ||
             e->type() == QEvent::MouseButtonDblClick) &&
            static_cast<QMouseEvent*>(e)->button() == Qt::MiddleButton)
            return true;
        return QObject::eventFilter(o, e);
    }
};

// A winset colour: "#rrggbb" or a name. "none", "" and anything unreadable give an invalid colour (the default look).
QColor skinColor(const std::string& v) {
    std::string t = skin::lowerAscii(skin::trimSpace(v));
    return t.empty() || t == "none" ? QColor() : QColor(QString::fromStdString(t));
}

// One CHILD's divider, as a widget the player can drag. Stock DreamSeeker behaves as follows:
//   - the grab area is the 4 px bar itself;
//   - the pointer shows the split cursor;
//   - show-splitter = false leaves nothing to grab; lock = left does not stop a drag;
//   - the panes do not move while the button is held; the layout changes on release only;
//   - on release the bar's left (top) edge lands on the pointer, with no grab offset kept, and the value is
//     (x - box.x0) / (box width - 4) * 100. Dragged past the window edge, it stops at 0 or 100.
// While the button is held this widget follows the pointer as the only feedback. The value then goes through
// LiveSkin::setSplitter, which uses the full [0,100]% range, as does winset (skin/layout.cpp clampFrac).
class SplitterHandle : public QWidget {
public:
    using Drop = std::function<void(const std::string& path, double pct)>;
    SplitterHandle(QWidget* parent, std::string path, Drop drop)
        : QWidget(parent), path_(std::move(path)), drop_(std::move(drop)) {
        setObjectName(QString::fromStdString("splitter:" + path_));
    }
    // sp is in client coordinates; top is the client area's offset in the parent (the menu bar).
    void place(const skin::Splitter& sp, int top) {
        sp_ = sp;
        top_ = top;
        setCursor(sp.vert ? Qt::SplitHCursor : Qt::SplitVCursor);
        if (!dragging_) setGeometry(sp.rect.x0, sp.rect.y0 + top, sp.rect.dx(), sp.rect.dy());
    }

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) return QWidget::mousePressEvent(e);
        dragging_ = true;
        raise();
        update();
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        int edge = edgeAt(e);
        if (sp_.vert)
            move(std::clamp(edge, sp_.box.x0, std::max(sp_.box.x0, sp_.box.x1 - width())), y());
        else
            move(x(), std::clamp(edge, sp_.box.y0, std::max(sp_.box.y0, sp_.box.y1 - height())) + top_);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (!dragging_ || e->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(e);
        dragging_ = false;
        update();
        int bar = sp_.vert ? sp_.rect.dx() : sp_.rect.dy();
        int avail = (sp_.vert ? sp_.box.dx() : sp_.box.dy()) - bar;
        if (avail <= 0) return;
        int from = sp_.vert ? sp_.box.x0 : sp_.box.y0;
        double pct = std::clamp(double(edgeAt(e) - from) / avail * 100, 0.0, 100.0);
        place(sp_, top_);              // back to rest; the relayout below moves it if the value was taken
        if (drop_) drop_(path_, pct);  // the relayout re-places this widget
    }
    void paintEvent(QPaintEvent*) override {
        if (!dragging_) return;  // at rest the window background shows through the 4 px gap
        QPainter(this).fillRect(rect(), palette().color(QPalette::Dark));
    }

private:
    // The pointer position on the split axis, in client coordinates.
    int edgeAt(QMouseEvent* e) const {
        QPoint p = mapToParent(e->position().toPoint());
        return sp_.vert ? p.x() : p.y() - top_;
    }
    std::string path_;
    Drop drop_;
    skin::Splitter sp_;
    int top_ = 0;
    bool dragging_ = false;
};

// A floating skin window. The close box hides it, as stock does; the owner is told so it can record is-visible=false.
class FloatWindow : public QWidget {
public:
    FloatWindow(QWidget* parent, Qt::WindowFlags f, std::function<void()> closed)
        : QWidget(parent, f), closed_(std::move(closed)) {}

protected:
    void closeEvent(QCloseEvent* e) override {
        e->ignore();
        hide();
        if (closed_) closed_();
    }

private:
    std::function<void()> closed_;
};
}  // namespace

// The status strip sits below the client area. Stock DreamSeeker shows it unless the skin sets statusbar = false; it is
// the system's status bar, 19 px at 96 dpi and 25 px at 144 dpi (516.1689, tests/data/winset-size-world): it follows
// the system font rather than scaling in proportion. This client's is 19 logical pixels at every scale, so it equals
// stock's at 96 dpi and is 28.5 device pixels to stock's 25 at 144 dpi. Without the strip the input bar would sit
// flush on the window's bottom edge.
constexpr int kStatusBarH = 19;

SkinWindow::SkinWindow(QWidget* parent) : QWidget(parent), live_(std::make_shared<skin::LiveSkin>()) {
    setObjectName("skinwindow");
    setAutoFillBackground(true);
    setWindowIcon(fugueIcon());  // until the world names an icon (setSkinIcon)
    clientMenu_ = new QMenu(this);
    clientMenu_->setObjectName("clientMenu");
    clientMenu_->setFont(skinMenuFont());
    addSystemMenuItems(
        clientMenu_, [this](const QString& c) { emit menuCommand(c); },
        [this](bool visible) { setMapMetricsVisible(visible); }, mapMetricsVisible_);
    host_ = new BrowseHost(this);
    host_->setLiveSkin(live_);
    host_->setDeclaredPredicate([this](const QString& n) { return declaresBrowser(n); });
    host_->setHiddenWindowPredicate([this](const QString& n) { return hiddenWindowBrowser(n); });
    host_->setChatBacklog([this] { return chat_; });
    connect(host_, &BrowseHost::byondLink, this, &SkinWindow::byondLink);
    connect(host_, &BrowseHost::worldLink, this, &SkinWindow::worldLink);
    connect(host_, &BrowseHost::commandRequested, this, &SkinWindow::commandEntered);
    connect(host_, &BrowseHost::onCloseCommand, this,
            [this](const QString&, const QString& cmd) { emit worldCommand(cmd); });
    // The live skin is mutated from the session thread, so this window syncs on the GUI thread, coalescing a burst
    // of changes into one sync.
    live_->setOnChange([this] {
        QMetaObject::invokeMethod(
            this,
            [this] {
                syncQueued_ = false;
                syncFromLive();
            },
            Qt::QueuedConnection);
    });
    // winget(null, "focus") follows the real keyboard focus, on a focus move and on a window activation. A fresh popup
    // has no focus widget yet, and stock answers "<popup>.browser" for it shortly after browse().
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (now) publishFocus(now);
    });
    connect(qApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow* w) {
        if (!w) return;  // focus left the application: keep the last answer
        QWidget* fw = QApplication::focusWidget();
        publishFocus(fw && fw->window() == QApplication::activeWindow() ? fw : QApplication::activeWindow());
    });
}

// The system-menu corner button. QMenuBar::sizeHint uses a corner widget's sizeHint, and a plain QToolButton's hint
// is a few pixels taller than a menu item, which would grow the bar. This one reports the size it is given.
struct BarCornerButton : QToolButton {
    using QToolButton::QToolButton;
    QSize hint;
    QSize sizeHint() const override { return hint.isValid() ? hint : QToolButton::sizeHint(); }
};

void SkinWindow::publishFocus(QWidget* now) {
    if (!now) return;
    // Check popups first: a popup is a top-level child of this widget, so the parent walk would reach `this` through it.
    QString id = host_->focusIdFor(now);  // a popup's page, "<popup>.browser"
    if (!id.isEmpty()) {
        live_->setGuiFocus(id.toStdString());
        return;
    }
    for (QWidget* w = now; w; w = w->parentWidget()) {
        for (const auto& [key, cw] : widgets_)
            if (cw == w) {
                live_->setGuiFocus(key);
                return;
            }
        if (w == this) {
            live_->setGuiFocus("");
            return;
        }  // the main window itself
        if (w->isWindow()) return;  // another top-level, such as a dialog: not a skin control
    }
}
SkinWindow::~SkinWindow() {
    live_->setOnChange({});
    clearWidgets();  // the panes first: the host's profile must outlive every page
    delete host_;
}

void SkinWindow::clearWidgets() {
    if (mapMenu_) mapMenu_->close();
    if (host_)
        for (auto* bp : browsers_) {
            if (bp->property("runtimeIdentity").toULongLong()) host_->close(bp->name);
            host_->releaseControl(bp);
        }
    const auto retired = std::exchange(widgets_, {});
    for (auto& [k, w] : retired) {
        w->hide();
        delete w;
    }
    for (auto& [k, h] : handles_) delete h;
    handles_.clear();
    widgets_.clear();
    types_.clear();
    outputs_.clear();
    infos_.clear();
    browsers_.clear();
    map_ = nullptr;
    for (auto& [k, f] : floats_) delete f;
    floats_.clear();
    floatPos_.clear();
}

int SkinWindow::menuBarHeight() const { return bar_ && !bar_->isHidden() ? bar_->height() : 0; }
void SkinWindow::setHoverName(const QString& name) {
    hoverName_ = name;
    if (status_) status_->setText(name);
}
int SkinWindow::statusBarHeight() const { return status_ && !status_->isHidden() ? kStatusBarH : 0; }

skin::Rect SkinWindow::clientRect() const { return {0, 0, width(), std::max(0, height() - chromeHeight())}; }

namespace {
// The main window's `statusbar` setting comes from the skin file, whose default is true. Nothing at runtime reads a winset of it.
bool wantsStatusBar(const skin::SkinSnapshot& s) {
    if (s.file)
        if (const skin::Block* mw = skin::findMainWindow(*s.file))
            if (const skin::Elem* m = mw->main()) return m->boolv("statusbar", true);
    return true;
}
}  // namespace

void SkinWindow::syncStatusBar(const skin::SkinSnapshot& s) {
    if (!wantsStatusBar(s)) {
        if (status_) status_->hide();
        live_->setStatusStrip(0);
        return;
    }
    if (!status_) {
        status_ = new QLabel(this);
        status_->setObjectName("statusbar");
        status_->setFrameShape(QFrame::Panel);
        status_->setFrameShadow(QFrame::Sunken);
        status_->setAutoFillBackground(true);
        status_->setBackgroundRole(QPalette::Button);  // OS chrome, not the skin's background colour
        status_->setTextFormat(Qt::PlainText);         // an atom's name is data, not markup
        status_->setFont(skinMenuFont());
        status_->setText(hoverName_);
    }
    status_->setGeometry(0, height() - kStatusBarH, width(), kStatusBarH);
    status_->show();
    live_->setStatusStrip(kStatusBarH);  // a zero `size` fills over it (liveskin.cpp applyGeometryLocked)
}

QSize SkinWindow::designSize() const {
    int bar = 0;
    if (!snap_.menu.empty()) {
        if (bar_)
            bar = bar_->sizeHint().height();
        else
            bar = 22;
    }
    int status = wantsStatusBar(snap_) ? kStatusBarH : 0;
    const QSize client = requestedClient_.isValid() ? requestedClient_ : design_;
    return QSize(client.width(), client.height() + bar + status);
}

QString SkinWindow::loadSkin(const std::string& dmf) {
    std::string err = live_->load(dmf);
    if (!err.empty()) return QString::fromStdString(err);
    // Size the client from the window (or the design size before the first resize), then build from one snapshot.
    skin::SkinSnapshot s = live_->snapshot();
    int w = width() > 1 ? width() : 0, h = height() > 1 ? height() : 0;
    if (!w || !h) {
        w = 640;
        h = 480;
        if (s.file)
            if (const skin::Block* mw = skin::findMainWindow(*s.file))
                if (const skin::Elem* m = mw->main()) {
                    int64_t dw, dh;
                    if (m->size("size", dw, dh) && dw > 0 && dh > 0) {
                        w = int(dw);
                        h = int(dh);
                    }
                }
    }
    live_->setClient(w, std::max(0, h - chromeHeight()));
    syncFromLive();
    return {};
}

void SkinWindow::showMapMenu(const MapMenu& menu) {
    if (mapMenu_) mapMenu_->close();
    if (menu.items.isEmpty()) return;
    auto* popup = new QMenu(this);
    popup->setObjectName("mapContextMenu");
    mapMenu_ = popup;
    // The system's menu font, for the atoms' entries and the verbs alike: stock's verbs are plain Windows menu items,
    // and it draws the atoms' names in the same font (stock 516.1689, tests/data/menu-world). Not the menu bar's.
    auto* ignoreMiddle = new MenuIgnoresMiddle(popup);
    popup->installEventFilter(ignoreMiddle);
    connect(popup, &QMenu::aboutToHide, popup, &QObject::deleteLater);
    connect(popup, &QMenu::aboutToHide, this, [this] {
        for (const auto& [key, widget] : widgets_)
            if (auto* map = dynamic_cast<MapWidget*>(widget)) map->popupClosing();
    });
    // Queued: the popup is still the active one while it hides.
    connect(
        popup, &QMenu::aboutToHide, this,
        [this] {
            for (const auto& [key, widget] : widgets_)
                if (auto* map = dynamic_cast<MapWidget*>(widget)) map->popupClosed();
        },
        Qt::QueuedConnection);
    QMenu* group = nullptr;
    int groupId = -1;
    for (qsizetype i = 0; i < menu.items.size(); ++i) {
        const auto& item = menu.items[i];
        if (!group || groupId != item.group) {
            groupId = item.group;
            group = popup->addMenu(mapMenuEntryIcon(item.icon, devicePixelRatioF()), QString(item.atom).replace("&", "&&"));
            group->installEventFilter(ignoreMiddle);
        }
        auto* action = group->addAction(QString(item.label).replace("&", "&&"));
        connect(action, &QAction::triggered, this,
                [this, token = menu.token, index = int(i)] { emit mapMenuPicked(token, index); });
    }
    popup->popup(menu.global);
}
void SkinWindow::mapFrameAvailable() {
    hasMapFrame_ = true;
    markMapShown();
    mapMailbox_->acknowledge();
    for (const auto& route : mapRoutes_->snapshot()) route.mailbox->acknowledge();
    for (const auto& [key, widget] : widgets_)
        if (auto* map = dynamic_cast<MapWidget*>(widget)) map->requestMapFrame();
}
void SkinWindow::toggleClientMenu() {
    if (clientMenu_->isVisible())
        clientMenu_->close();
    else
        clientMenu_->popup(mapToGlobal(QPoint(0, menuBarHeight())));
}

void SkinWindow::setMapMetricsVisible(bool visible) {
    mapMetricsVisible_ = visible;
    for (const auto& [key, widget] : widgets_)
        if (auto* map = dynamic_cast<MapWidget*>(widget)) map->setMetricsVisible(visible);
    if (auto* action = findChild<QAction*>("mapPerformanceAction")) action->setChecked(visible);
}

void SkinWindow::setMapViewSize(int columns, int rows) {
    mapViewSize_ = QSize(columns, rows);
    for (const auto& [key, widget] : widgets_)
        if (auto* map = dynamic_cast<MapWidget*>(widget); map && key == live_->defaultMapId())
            map->setViewSize(mapViewSize_);
    publishMapSizes();
}
void SkinWindow::publishMapSizes() {
    for (const auto& [key, widget] : widgets_)
        if (auto* map = dynamic_cast<MapWidget*>(widget)) {
            const auto size = map->presentedViewSize();
            live_->setMapViewSize(key, size.width(), size.height());
        }
}

QWidget* SkinWindow::makeWidget(const skin::Control& c) {
    QWidget* w = nullptr;
    if (c.type == "MAP") {
        // Reparent only after QRhiWidget has initialized its RHI configuration.
        // A MAP loaded into an already-visible raster window otherwise misses
        // Qt's switch to RHI composition ("QRhiWidget: No QRhi").
        auto* grid = new MapWidget;
        grid->setParent(this);
        const bool defaultMap = c.window + "." + c.name == live_->defaultMapId();
        auto mailbox = defaultMap ? mapMailbox_ : mapRoutes_->subscribe(QString::fromStdString(c.name));
        if (mailbox) grid->setMetrics(mailbox->metrics());
        grid->setMailbox(std::move(mailbox));
        grid->setInputHandler(QString::fromStdString(c.window + "." + c.name),
                              c.window + "." + c.name == live_->defaultMapId(),
                              [this](const MapPointer& p) { emit mapPointer(p); });
        grid->setMetricsVisible(mapMetricsVisible_);
        grid->setViewSize(defaultMap ? mapViewSize_ : QSize(1, 1));
        grid->presentationChanged = [this] { publishMapSizes(); };
        w = grid;
        w->setProperty("fugueZone", "map");
        w->setFocusPolicy(Qt::StrongFocus);
        if (!map_ && c.window + "." + c.name == live_->defaultMapId()) map_ = w;
    } else if (c.type == "INFO") {
        auto* sp = new StatPanelWidget(this);
        sp->setControlId(QString::fromStdString(c.window + "." + c.name));
        infos_.append(sp);
        sp->setView(statView_);
        connect(sp, &StatPanelWidget::tabRequested, this, &SkinWindow::statTabRequested);
        connect(sp, &StatPanelWidget::rowClicked, this, &SkinWindow::statRowClicked);
        connect(sp, &StatPanelWidget::verbClicked, this, &SkinWindow::statVerbClicked);
        connect(sp, &StatPanelWidget::rowHovered, this, &SkinWindow::statRowHovered);
        connect(sp, &StatPanelWidget::hoverLeft, this, &SkinWindow::statHoverLeft);
        w = sp;
    } else if (c.type == "OUTPUT") {
        auto* tb = new QTextBrowser(this);
        tb->setOpenLinks(false);
        tb->setProperty("fugueZone", "output");
        outputs_.append(tb);
        tb->setHtml(chat_.join("<br>"));
        tb->verticalScrollBar()->setValue(tb->verticalScrollBar()->maximum());
        w = tb;
    } else if (c.type == "INPUT") {
        auto* le = wirePaste(new QLineEdit(this));
        // The INPUT's `command` (kept in "fugueInputCommand" by applyProps). A `!text` command is a sticky default
        // that the bar returns to after every send. A bare `text` is put in front of what the player typed. With a
        // `!say "` default in force, Enter on `say "hello` sends say("hello") and the bar goes back to `say "`.
        connect(le, &QLineEdit::returnPressed, this, [this, le] {
            QString cmd = le->property("fugueInputCommand").toString();
            QString line = le->text();
            if (!cmd.isEmpty() && !cmd.startsWith('!') && !line.isEmpty()) line = cmd + line;
            emit commandEntered(line);
            le->setText(cmd.startsWith('!') ? cmd.mid(1) : QString());
        });
        le->installEventFilter(new SpaceCompleterFilter(le, &spaceCompleter_));
        connect(le, &QLineEdit::textChanged, this, [this, path = c.window + "." + c.name](const QString& t) {
            live_->setInputText(path, t.toStdString());
        });
        w = le;
    } else if (c.type == "BROWSER") {
        BrowserPane* bp = host_->takeHidden(QString::fromStdString(c.window), QString::fromStdString(c.name));
        if (!bp) {
            bp = new BrowserPane(this, host_->profile());
            // A browse() or output() naming the window reaches a .dmf control, never one a winset made in it.
            host_->adoptControl(QString::fromStdString(c.window), QString::fromStdString(c.name), bp,
                                !c.runtimeIdentity);  // shows a page sent before the control existed
        }
        browsers_.insert(QString::fromStdString(c.name), bp);
        w = bp;
    } else if (c.type == "BUTTON") {
        auto* b = new SkinButton(QString::fromStdString(c.name), this);
        QString cmd = QString::fromStdString(c.elem ? c.elem->str("command", "") : "");
        // A `button-type = pushbox` or `checkbox` button sets its own is-checked before its command runs (a grouped one
        // is a radio member and only ever goes true). The command must see the new state, since vg's commands test it.
        std::string bt = c.elem ? skin::lowerAscii(c.elem->str("button-type", "")) : "";
        bool pushbox = bt == "pushbox" || bt == "checkbox";
        std::string path = c.window + "." + c.name, group = c.elem ? c.elem->str("group", "") : "";
        if (!cmd.isEmpty() || pushbox)
            connect(b, &QPushButton::clicked, this, [this, cmd, pushbox, path, group] {
                if (pushbox) {
                    std::string cur;
                    bool now =
                        group.empty() ? !(live_->winget(path, "is-checked", cur) && skin::winsetBool(cur)) : true;
                    live_->winset("", path + ".is-checked=" + (now ? "true" : "false"));
                    syncFromLive();
                }
                if (!cmd.isEmpty()) emit buttonCommand(cmd);
            });
        w = b;
    } else if (c.type == "LABEL") {
        w = new SkinLabel(this);
    } else {
        auto* f = new QFrame(this);  // placeholder for a control type this client does not model
        f->setFrameShape(QFrame::StyledPanel);
        w = f;
    }
    return w;
}

void SkinWindow::rebuild(const skin::SkinSnapshot& s) {
    clearWidgets();
    mapPaneShown_ = infoPaneShown_ = false;  // a new skin shows its panes afresh (setPaneShown)
    builtFileId_ = s.fileId;
    requestedClient_ = QSize();
    lastGen_ = ~0ull;
    design_ = QSize(640, 480);
    if (s.file)
        if (const skin::Block* mw = skin::findMainWindow(*s.file))
            if (const skin::Elem* m = mw->main()) {
                int64_t w, h;
                if (m->size("size", w, h) && w > 0 && h > 0) design_ = QSize(int(w), int(h));
                skinTitle_ = QString::fromStdString(m->str("title", ""));
                uint8_t c[4];
                if (m->color("background-color", c)) {
                    QPalette pal = palette();
                    pal.setColor(QPalette::Window, QColor(c[0], c[1], c[2]));
                    setPalette(pal);
                }
            }
    updateTitle();
    menuSigValid_ = false;  // an empty menu is a valid signature, not an invalid cache
}

// Per-control live properties: the overlay on top of the skin file.
void SkinWindow::applyProps(const skin::Control& c, QWidget* w, const skin::SkinSnapshot& s) {
    auto prop = [&](const char* key, std::string& out) {
        if (s.props.controlProp(c.window, c.name, key, out)) return true;
        if (c.elem)
            if (const std::string* v = c.elem->get(key)) {
                out = *v;
                return true;
            }
        return false;
    };
    std::string v;
    if (auto* map = dynamic_cast<MapWidget*>(w)) {
        MapPresentationSettings settings;
        std::string zoom, icon;
        const bool zoomOverride = s.props.controlProp(c.window, c.name, "zoom", zoom);
        const bool iconOverride = s.props.controlProp(c.window, c.name, "icon-size", icon);
        // A runtime writer of either size property supersedes BOTH skin defaults.
        if (!zoomOverride && !iconOverride) {
            prop("zoom", zoom);
            // The skin file's icon-size counts for a map in the main window only: in a pane stock fits the map to it
            // whatever the file says, until a winset sets icon-size (stock 516.1689, scratch skins: pane16,
            // pane32, main640-16, and a winset of icon-size or zoom on the pane's map).
            if (c.window == s.mainWindow) prop("icon-size", icon);
        }
        auto positive = [](const std::string& text) {
            const double value = QString::fromStdString(text).toDouble();
            return std::isfinite(value) && value > 0 ? value : 0.0;
        };
        settings.zoom = positive(zoom);
        settings.iconSize = positive(icon);
        if (prop("zoom-mode", v)) {
            const auto mode = skin::lowerAscii(v);
            if (mode == "distort")
                settings.mode = MapPresentationSettings::Distort;
            else if (mode == "blur")
                settings.mode = MapPresentationSettings::Blur;
        }
        if (prop("letterbox", v)) settings.letterbox = skin::winsetBool(v);
        map->setPresentationSettings(settings);
    }
    if (auto* browser = qobject_cast<BrowserPane*>(w)) {
        // CSS transparency exposes the page's backing color, not the MAP, unless
        // the WebEngine surface itself has alpha. Keep ordinary browsers opaque.
        QColor background(Qt::white);
        if (prop("inner-background-color", v)) {
            if (skin::lowerAscii(skin::trimSpace(v)) == "transparent") {
                background = Qt::transparent;
            } else {
                skin::Elem color;
                color.rows.push_back({"inner-background-color", v, 0});
                color.idx["inner-background-color"] = 0;
                uint8_t rgba[4];
                // Chromium accepts only opaque or fully transparent backing
                // colors. Partial-alpha backing colors remain unsupported.
                if (color.color("inner-background-color", rgba) && rgba[3] == 255)
                    background = QColor(rgba[0], rgba[1], rgba[2]);
            }
        }
        if (browser->page()->backgroundColor() != background) browser->page()->setBackgroundColor(background);
    }
    if (c.type == "BROWSER") {
        // Disabled embedded browsers still run scripts and paint. They do not
        // participate in hit testing or take focus from the MAP beneath them.
        w->setAttribute(Qt::WA_TransparentForMouseEvents, c.disabled);
        w->setFocusPolicy(c.disabled ? Qt::NoFocus : Qt::StrongFocus);
    }
    w->setEnabled(!c.disabled || c.type == "BROWSER");
    if (auto* b = qobject_cast<QPushButton*>(w)) {
        if (prop("text", v)) b->setText(QString::fromStdString(v));
        std::string chk, bt = c.elem ? skin::lowerAscii(c.elem->str("button-type", "")) : "";
        bool pushbox = bt == "pushbox" || bt == "checkbox";
        if (s.props.controlProp(c.window, c.name, "is-checked", chk) || (c.elem && c.elem->get("is-checked")) ||
            pushbox) {
            if (!(s.props.controlProp(c.window, c.name, "is-checked", chk))) chk = c.elem->str("is-checked", "false");
            b->setCheckable(true);
            b->setChecked(skin::winsetBool(chk));
        }
    } else if (auto* le = qobject_cast<QLineEdit*>(w); le && c.type == "INPUT") {
        // `command` goes to the bar. A new `!` default replaces the text only while the bar still shows the old default
        // unedited. Otherwise the player's text is left alone.
        std::string cmd, text;
        if (s.props.controlProp(c.window, c.name, "text", text) && le->text().toStdString() != text)
            le->setText(QString::fromStdString(text));
        prop("command", cmd);
        QString now = QString::fromStdString(cmd), was = le->property("fugueInputCommand").toString();
        if (now != was || !le->property("fugueInputCommand").isValid()) {
            auto shown = [](const QString& c) { return c.startsWith('!') ? c.mid(1) : QString(); };
            if (le->text() == shown(was)) {
                le->setText(shown(now));
                le->setCursorPosition(le->text().size());
            }
            le->setProperty("fugueInputCommand", now);
        }
    } else if (auto* l = qobject_cast<QLabel*>(w)) {
        if (prop("text", v)) l->setText(QString::fromStdString(v));
    }
    // A live winset wins over the file's colour; "none" in either is the default look.
    auto col = [&](const char* key) {
        std::string x;
        return prop(key, x) ? skinColor(x) : QColor();
    };
    QString css, rules;
    QColor bg = col("background-color");
    std::string bgv;
    // An INPUT, OUTPUT or INFO set to "none" takes the control face colour; with no colour at all it is white.
    if (!bg.isValid() && (c.type == "INPUT" || c.type == "OUTPUT" || c.type == "INFO") &&
        prop("background-color", bgv) && skin::lowerAscii(skin::trimSpace(bgv)) == "none")
        bg = QColor(0xf4, 0xf4, 0xf4);
    if (bg.isValid()) css += QString("background-color:%1;").arg(bg.name());
    if (QColor fg = col("text-color"); fg.isValid()) css += QString("color:%1;").arg(fg.name());
    if (auto* sp = qobject_cast<StatPanelWidget*>(w)) {
        QColor tb = col("tab-background-color"), tt = col("tab-text-color"), pc = col("prefix-color"),
               sc = col("suffix-color");
        sp->setTextColors(pc, sc);
        sp->tabBar()->setDrawBase(
            !(tb.isValid() || tt.isValid()));  // the base line is the style's, not the tab text colour
        if (tb.isValid() || tt.isValid()) {
            QString tab = "padding:2px 6px;";
            if (tb.isValid()) tab += QString("background-color:%1;").arg(tb.name());
            if (tt.isValid()) tab += QString("color:%1;border:1px solid %1;").arg(tt.name());
            rules += "QTabBar::tab{" + tab + "}";
            if (tb.isValid()) rules += QString("QTabBar{background-color:%1;}").arg(tb.name());
            if (QColor edge = pc.isValid() ? pc : tt; edge.isValid())
                rules += QString("QTableWidget{border:1px solid %1;}").arg(edge.name());
        }
    }
    if (!rules.isEmpty())
        css = (css.isEmpty() ? QString() : "*{" + css + "}") +
              rules;  // declarations ahead of rules are not parsed as the widget's own
    if (css != w->property("fugueCss").toString()) {
        w->setProperty("fugueCss", css);
        w->setStyleSheet(css);
        w->setProperty("fugueFont",
                       QVariant());  // a style sheet change drops the font set before it: set it again below
    }
    if (c.type == "BUTTON" || c.type == "LABEL" || c.type == "INPUT" || c.type == "INFO") {
        std::string fam, size, style;
        prop("font-family", fam);
        prop("font-size", size);
        prop("font-style", style);
        QString key = QString::fromStdString(fam + '\n' + size + '\n' + style);
        if (!w->property("fugueFont").isValid() || key != w->property("fugueFont").toString()) {
            w->setProperty("fugueFont", key);
            const SkinFont sf = skinControlFont(fam, size, style);
            if (auto* b = dynamic_cast<SkinButton*>(w))
                b->setSkinFont(sf);
            else if (auto* l = dynamic_cast<SkinLabel*>(w))
                l->setSkinFont(sf);
            else if (auto* sp = qobject_cast<StatPanelWidget*>(w))
                sp->setPanelFont(sf.font);
            else
                w->setFont(sf.font);
        }
    }
    if (auto* l = dynamic_cast<SkinLabel*>(w)) {
        std::string al;
        prop("align", al);
        l->setAlignment(skinLabelAlign(al));
    }
}

void SkinWindow::paintEvent(QPaintEvent* e) {
    QWidget::paintEvent(e);
    QPainter p(this);
    for (const auto& [r, c] : fills_) p.fillRect(r, c);
}

void SkinWindow::apply(const skin::SkinSnapshot& s) {
    std::string mainKey;
    // Create what is now visible and placed; show or hide and place everything.
    std::set<std::string> placed;
    fills_.clear();
    std::set<std::string> filled;
    auto addFill = [&](const skin::Rect& r, const std::string& color) {
        if (QColor col = skinColor(color); col.isValid())
            fills_.emplace_back(QRect(r.x0, r.y0 + menuBarHeight(), r.dx(), r.dy()), col);
    };
    // Removal is not hiding: retire the document and its bridge before deleting
    // the widget. A recreated name has a new identity, even within one GUI tick.
    for (auto it = widgets_.begin(); it != widgets_.end();) {
        const auto identity = it->second->property("runtimeIdentity").toULongLong();
        const auto* c = s.tree.byName(it->first);
        if (identity && (!c || c->runtimeIdentity != identity)) {
            auto* bp = qobject_cast<BrowserPane*>(it->second);
            if (bp) {
                host_->close(bp->name);
                host_->releaseControl(bp);
                browsers_.remove(QString::fromStdString(it->first.substr(it->first.find('.') + 1)));
            }
            auto* retired = it->second;
            types_.erase(it->first);
            it = widgets_.erase(it);
            delete retired;
        } else
            ++it;
    }
    for (const skin::Control& c : s.tree.controls) {
        std::string bg;
        if (c.top.empty() && c.visible && !c.offPane && filled.insert(c.window).second &&
            s.props.get(c.window, "background-color", bg))
            addFill(c.box, bg);  // the window's own background, over the box it was laid out in
        if (c.type == "CHILD" || c.type == "MAIN") {
            if (c.visible && !c.offPane && s.props.controlProp(c.window, c.name, "background-color", bg))
                addFill(c.rect, bg);
            continue;
        }
        std::string key = c.window + "." + c.name;
        const skin::Floating* fl = c.top.empty() ? nullptr : s.tree.floating(c.top);
        bool show = c.visible && !c.offPane && (!fl || fl->visible);
        QWidget* popupParent = nullptr;
        if (c.runtimeIdentity && c.type == "MAP" && !s.file->window(c.window)) {
            popupParent = host_->paneFor(QString::fromStdString(c.window));
            if (!popupParent) show = false;
        }
        auto it = widgets_.find(key);
        if (it == widgets_.end()) {
            if (!show && !(c.runtimeIdentity && c.type == "BROWSER")) continue;
            QWidget* w = makeWidget(c);
            if (!w) continue;
            w->setProperty("runtimeIdentity", QVariant::fromValue<qulonglong>(c.runtimeIdentity));
            w->setObjectName(QString::fromStdString(key));
            w->setProperty("fugueControl", QString::fromStdString(key));
            widgets_[key] = w;
            types_[key] = c.type;
            if (c.type == "MAP" && c.runtimeIdentity) {
                // A popup can destroy its child MAP before a skin notification.
                // Remove only this widget, not a newer control with the same name.
                connect(w, &QObject::destroyed, this, [this, key, w] {
                    const auto found = widgets_.find(key);
                    if (found != widgets_.end() && found->second == w) {
                        widgets_.erase(found);
                        types_.erase(key);
                    }
                });
            }
            it = widgets_.find(key);
        }
        QWidget* w = it->second;
        placed.insert(key);
        applyProps(c, w, s);
        QWidget* parent = popupParent ? popupParent : this;
        if (!c.top.empty()) parent = floatWindow(c.top, s);
        if (w->parentWidget() != parent) w->setParent(parent);
        int top = parent == this ? menuBarHeight() : 0;
        const auto& geometry = c.type == "BUTTON" ? c.full : c.rect;
        const QRect next(geometry.x0, geometry.y0 + top, geometry.dx(), geometry.dy());
        w->setGeometry(next);
        if (c.type == "BUTTON") {
            // Widgets are flattened under SkinWindow rather than native pane parents.
            // Preserve the button's full size, but clip paint and hit testing to its pane.
            const QRect pane(c.rect.x0, c.rect.y0 + top, c.rect.dx(), c.rect.dy());
            if (pane.contains(next))
                w->clearMask();
            else if (show)
                w->setMask(QRegion(pane.intersected(next).translated(-next.topLeft())));
        }
        w->setVisible(show);
        if (c.runtimeIdentity && (c.type == "BROWSER" || c.type == "MAP") && show) w->raise();
    }
    syncFloats(s);
    update();
    publishMapSizes();
    // A control that left the layout is hidden, not destroyed, so its content survives.
    for (auto& [key, w] : widgets_)
        if (!placed.count(key)) w->hide();

    // The CHILD dividers the layout placed (none for show-splitter=false or a cleared side).
    std::set<std::string> dividers;
    for (const skin::Splitter& sp : s.tree.splitters) {
        if (sp.rect.empty() || !sp.top.empty()) continue;
        QWidget*& h = handles_[sp.path];
        if (!h)
            h = new SplitterHandle(this, sp.path, [this](const std::string& path, double pct) {
                live_->setSplitter(path, pct);
                syncFromLive();
            });
        static_cast<SplitterHandle*>(h)->place(sp, menuBarHeight());
        h->show();
        h->raise();
        dividers.insert(sp.path);
    }
    for (auto& [path, h] : handles_)
        if (!dividers.count(path)) h->hide();

    // Window title, from a winset of the main window's `title`.
    std::string t;
    if (!s.mainWindow.empty() && (s.props.get(s.mainWindow, "title", t) || s.props.get("mainwindow", "title", t)))
        skinTitle_ = QString::fromStdString(t);
    updateTitle();
    // Popup visibility/title writes follow the same immutable property snapshot.
    if (host_) host_->syncPopupProps(s.props);
}

void SkinWindow::setSkinIcon(const QString& name, const QByteArray& bytes) {
    QImage img = bytes.isEmpty() ? QImage() : skinIconImage(bytes);
    if (!bytes.isEmpty() && img.isNull())
        std::fprintf(stderr, "[icon] window icon \"%s\": %d bytes that do not decode - Fugue's icon instead\n",
                     name.toUtf8().constData(), int(bytes.size()));
    skinIcon_ = !img.isNull();
    setWindowIcon(skinIcon_ ? QIcon(QPixmap::fromImage(img)) : fugueIcon());
    // A browse() popup is a child WINDOW: Qt reads the parent's icon when it is created but sends an open one no change,
    // so bring the open ones along. A dialog with an icon of its own (prompts, options, macros: Fugue's) keeps it.
    for (QWidget* c : findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        if (c->isWindow() && !c->testAttribute(Qt::WA_SetWindowIcon) && c->windowHandle())
            c->windowHandle()->setIcon(windowIcon());
}

void SkinWindow::updateTitle() {
    QString t = skinTitle_;
    // "Dream Seeker" is the default skin's title, in a world's own copy of that skin too: stock shows the world's name
    // (stock 516.1689).
    if (t == "Dream Seeker") t.clear();
    if (t.isEmpty()) t = worldName_;
    setWindowTitle(t.isEmpty() ? QString("Fugue") : t);
}

void SkinWindow::rebuildMenu(const skin::Menu& m) {
    QString sig;
    for (const skin::MenuCat& c : m.cats) {
        sig += "|" + QString::fromStdString(c.name + ":" + c.label) + (c.disabled ? "!" : "");
        for (const skin::MenuItem& it : c.items)
            sig += "," + QString::fromStdString(it.name + ":" + it.label + ":" + it.command) +
                   (it.disabled ? "!" : "") + (it.separator ? "-" : "");
    }
    if (menuSigValid_ && sig == menuSig_) return;
    menuSig_ = sig;
    menuSigValid_ = true;
    menuActs_.clear();
    menuChk_.clear();
    if (bar_) {
        // clear() removes actions, but does not destroy the QMenus created by addMenu().
        const auto actions = bar_->actions();
        for (QAction* action : actions) delete action->menu();
        bar_->clear();
    }
    if (m.empty()) {
        if (bar_) bar_->hide();
        return;
    }
    if (!bar_) {
        bar_ = new QMenuBar(this);
        bar_->setFont(skinMenuFont());
        // Stock DreamSeeker's system-menu entries (shell/macrodialog.h) sit under the caption icon. The window manager
        // owns that icon here, so the same entries sit behind the window icon in the menu bar's corner.
        sysBtn_ = new BarCornerButton(bar_);
        sysBtn_->setObjectName("systemmenu");
        sysBtn_->setAutoRaise(true);
        sysBtn_->setPopupMode(QToolButton::InstantPopup);
        sysBtn_->setToolTip("Options & Messages, Macros...");
        sysBtn_->setIcon(windowIcon());
        sysBtn_->setMenu(clientMenu_);
        connect(this, &QWidget::windowIconChanged, sysBtn_, &QToolButton::setIcon);
        // The button must not make the bar taller, or every skin's client area would shrink. QMenuBar pads a corner
        // widget by its panel width and vertical margin, so the button is sized to the bar's item height.
        const int pad = bar_->style()->pixelMetric(QStyle::PM_MenuBarPanelWidth, nullptr, bar_) +
                        bar_->style()->pixelMetric(QStyle::PM_MenuBarVMargin, nullptr, bar_);
        QMenuBar probe;
        probe.setStyle(bar_->style());
        probe.setFont(bar_->font());
        probe.addMenu("X");
        const int itemH = std::max(8, probe.sizeHint().height() - 2 * pad);
        sysBtn_->setIconSize(QSize(itemH - 4, itemH - 4));
        sysBtn_->setFixedSize(itemH, itemH);
        static_cast<BarCornerButton*>(sysBtn_)->hint = QSize(itemH, itemH);  // QMenuBar sizes itself from the HINT
        bar_->setCornerWidget(sysBtn_, Qt::TopLeftCorner);
    }
    auto withAccel = [](const std::string& label, int accel) {
        QString t = QString::fromStdString(label);
        if (accel >= 0 && accel < t.size()) t.insert(accel, '&');
        return t;
    };
    for (const skin::MenuCat& c : m.cats) {
        QMenu* menu = bar_->addMenu(withAccel(c.label, c.accel));
        menu->setFont(bar_->font());  // a popup does not inherit its parent's font
        menu->setEnabled(!c.disabled);
        QHash<QString, QActionGroup*> groups;
        for (const skin::MenuItem& it : c.items) {
            if (it.separator) {
                menu->addSeparator();
                continue;
            }
            QString text = withAccel(it.label, it.accel);
            if (!it.shortcut.empty()) text += "\t" + QString::fromStdString(it.shortcut);
            QAction* a = menu->addAction(text);
            a->setEnabled(!it.disabled);
            if (it.canCheck) {
                a->setCheckable(true);
                a->setChecked(it.checked);
                if (!it.name.empty()) {
                    menuActs_.emplace(it.name, a);
                    menuChk_[it.name] = it.checked;
                }
                if (!it.group.empty()) {
                    QString g = QString::fromStdString(it.group);
                    if (!groups.contains(g)) {
                        groups[g] = new QActionGroup(menu);
                        groups[g]->setExclusive(true);
                    }
                    groups[g]->addAction(a);
                }
            }
            QString cmd = QString::fromStdString(it.command);
            // A can-check item's is-checked flips before its command runs, as a pushbox button's does (applyProps).
            // Worlds test the new state in the command, and the change also lands in the overlay.
            std::string name = it.canCheck ? it.name : std::string();
            if (!cmd.isEmpty() || !name.empty())
                connect(a, &QAction::triggered, this, [this, cmd, name, a] {
                    if (!name.empty()) {
                        menuChk_[name] = a->isChecked();
                        live_->winset("", name + ".is-checked=" + (a->isChecked() ? "true" : "false"));
                    }
                    if (!cmd.isEmpty()) emit menuCommand(cmd);
                });
        }
    }
    bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    bar_->show();
}

// A winset of a menu item's is-checked moves the check mark, and a radio sibling unchecked by a pick follows.
// Only a changed value is pushed.
void SkinWindow::syncMenuChecks(const skin::Menu& m) {
    for (const skin::MenuCat& c : m.cats)
        for (const skin::MenuItem& it : c.items) {
            if (it.name.empty() || !it.canCheck) continue;
            auto was = menuChk_.find(it.name);
            if (was == menuChk_.end() || was->second == it.checked) continue;
            was->second = it.checked;
            auto [b, e] = menuActs_.equal_range(it.name);
            for (auto i = b; i != e; ++i) i->second->setChecked(it.checked);
        }
}

void SkinWindow::syncFromLive() {
    if (syncing_) return;
    syncing_ = true;
    skin::SkinSnapshot s = live_->snapshot();
    if (!s.file) {
        syncing_ = false;
        return;
    }
    if (s.fileId != builtFileId_) rebuild(s);
    // The menu bar and the status bar come first: their heights come off the client area the layout is measured in.
    int before = chromeHeight();
    rebuildMenu(s.menu);
    syncMenuChecks(s.menu);
    if (bar_ && !bar_->isHidden()) bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    syncStatusBar(s);
    if (chromeHeight() != before) {
        live_->setClient(width(), std::max(0, height() - chromeHeight()));
        s = live_->snapshot();
    }
    // A winset of the main window's size names its client area; the menu bar and the status strip come on top, as they
    // do in stock DreamSeeker 516 (a 224x224 window's frame grows by 64 both ways for main.size=288x288). The live
    // layout has the new size already. A maximized or full-screen window stays so and gets the size when it is
    // restored (516.1689); its real client area stays the layout's.
    if (s.windowSizeSeq != lastWindowSizeSeq_) {
        lastWindowSizeSeq_ = s.windowSizeSeq;
        if (isWindow() && s.windowWidth > 0 && s.windowHeight > 0) {
            requestedClient_ = QSize(s.windowWidth, s.windowHeight);
            if (isMaximized() || isFullScreen()) {
                restoreClient_ = requestedClient_;
                publishWindowState();
                live_->setClient(width(), std::max(0, height() - chromeHeight()));
                s = live_->snapshot();
            } else {
                resize(s.windowWidth, s.windowHeight + chromeHeight());
            }
        }
    }
    // A winset of its pos moves the frame's top-left there (516.1689); a maximized window gets it when restored.
    if (s.windowPosSeq != lastWindowPosSeq_) {
        lastWindowPosSeq_ = s.windowPosSeq;
        if (isWindow()) {
            if (isMaximized() || isFullScreen())
                restorePos_ = QPoint(s.windowPosX, s.windowPosY);
            else
                move(s.windowPosX, s.windowPosY);
        }
    }
    if (s.maximizeSeq != lastMaximizeSeq_) {
        lastMaximizeSeq_ = s.maximizeSeq;
        if (isWindow())
            setWindowState(s.maximize ? windowState() | Qt::WindowMaximized : windowState() & ~Qt::WindowMaximized);
    }
    if (s.generation != lastGen_ || s.fileId != snap_.fileId) {
        apply(s);
        lastGen_ = s.generation;
    }
    snap_ = s;
    markMapShown();
    markInfoShown();
    if (QSize cl(s.client.dx(), s.client.dy()); cl != lastClient_ && cl.width() > 0 && cl.height() > 0) {
        lastClient_ = cl;
        emit clientResized(cl);
    }
    if (s.macro != lastMacro_ || s.macroGen != lastMacroGen_) {
        bool first = lastMacro_.empty();
        lastMacro_ = s.macro;
        lastMacroGen_ = s.macroGen;
        if (!first || !s.macro.empty()) emit macroChanged(QString::fromStdString(s.macro));
    }
    if (s.focusSeq != lastFocusSeq_) {
        lastFocusSeq_ = s.focusSeq;
        if (const skin::Control* c = s.tree.byName(s.focus)) {
            auto it = widgets_.find(c->window + "." + c->name);
            if (it != widgets_.end() && it->second->isVisible()) {
                if (!c->top.empty()) it->second->window()->activateWindow();
                it->second->setFocus(Qt::OtherFocusReason);
            }
        }
    }
    syncing_ = false;
}

QWidget* SkinWindow::floatWindow(const std::string& window, const skin::SkinSnapshot& s) {
    const std::string key = skin::lowerAscii(window);
    if (auto it = floats_.find(key); it != floats_.end()) return it->second;
    const skin::Block* b = s.file ? s.file->window(window) : nullptr;
    const skin::Elem* m = b ? b->main() : nullptr;
    Qt::WindowFlags fl = Qt::Dialog | Qt::WindowCloseButtonHint;
    if (m && !m->boolv("titlebar", true)) fl |= Qt::FramelessWindowHint;
    if (m && m->boolv("can-minimize", true)) fl |= Qt::WindowMinimizeButtonHint;
    auto* fw = new FloatWindow(this, fl, [this, window] {
        live_->winset(window, "is-visible=false");
        std::string cmd;
        if (live_->winget(window, "on-close", cmd) && !skin::trimSpace(cmd).empty())
            emit worldCommand(QString::fromStdString(cmd));
        syncFromLive();
    });
    fw->setObjectName(QString::fromStdString(window));
    fw->setProperty("fugueControl", QString::fromStdString(window));
    fw->setAutoFillBackground(true);
    uint8_t c[4];
    if (m && m->color("background-color", c)) {
        QPalette pal = fw->palette();
        pal.setColor(QPalette::Window, QColor(c[0], c[1], c[2]));
        fw->setPalette(pal);
    }
    fw->hide();
    floats_[key] = fw;
    return fw;
}

// Size, place, title and show or hide each floating window. Showing one activates it and gives the keyboard to the
// control a winset last focused there, else its first visible control.
void SkinWindow::syncFloats(const skin::SkinSnapshot& s) {
    for (const skin::Floating& f : s.tree.floats) {
        const std::string key = skin::lowerAscii(f.window);
        auto it = floats_.find(key);
        if (it == floats_.end() && !f.visible) continue;
        QWidget* fw = it == floats_.end() ? floatWindow(f.window, s) : it->second;
        const skin::Block* b = s.file->window(f.window);
        const skin::Elem* m = b ? b->main() : nullptr;
        QSize sz(std::max(1, f.box.dx()), std::max(1, f.box.dy()));
        if (m && !m->boolv("can-resize", true))
            fw->setFixedSize(sz);
        else if (fw->size() != sz)
            fw->resize(sz);
        std::string pos, title;
        if (!s.props.get(f.window, "pos", pos) && m) pos = m->str("pos", "");
        if (pos != floatPos_[key]) {
            long long x, y;
            if (skin::parsePair(pos, ",", x, y)) fw->move(int(x), int(y));
            floatPos_[key] = pos;
        }
        if (!s.props.get(f.window, "title", title) && m) title = m->str("title", "");
        fw->setWindowTitle(title.empty() ? QStringLiteral(" ") : QString::fromStdString(title));
        if (!f.visible) {
            fw->hide();
            continue;
        }
        if (fw->isVisible()) continue;
        fw->show();
        fw->raise();
        fw->activateWindow();
        QWidget* target = nullptr;
        if (const skin::Control* c = s.tree.byName(s.focus); c && skin::lowerAscii(c->top) == key) {
            auto w = widgets_.find(c->window + "." + c->name);
            if (w != widgets_.end()) target = w->second;
        }
        for (const skin::Control& c : s.tree.controls) {
            if (target) break;
            if (skin::lowerAscii(c.top) != key) continue;
            auto w = widgets_.find(c.window + "." + c.name);
            if (w != widgets_.end() && w->second->isVisible()) target = w->second;
        }
        if (target) target->setFocus(Qt::OtherFocusReason);
    }
}

void SkinWindow::closeEvent(QCloseEvent* e) {
    host_->setQuitting();
    QWidget::closeEvent(e);
}

// What winget(client, null, ...) reports for the OS: dpi, screen-pos and screen-size.
//
// dpi is the logical DPI scale, not the device pixel ratio. Stock DreamSeeker sizes its windows in physical pixels
// even when it reports a dpi of 2, and vg multiplies its browse() sizes by that answer. This client sizes in Qt
// logical pixels, which already carry the device pixel ratio, so the answer is logicalDotsPerInch / 96. Answering
// with the device pixel ratio would double vg's windows on any HiDPI screen.
void SkinWindow::publishClientEnv() {
    char dpi[32];
    QScreen* scr = screen();
    std::snprintf(dpi, sizeof dpi, "%g", scr ? scr->logicalDotsPerInch() / 96.0 : 1.0);
    live_->setClientEnv("dpi", dpi);
    if (QScreen* sc = screen()) {
        QRect a = sc->availableGeometry();
        live_->setClientEnv("screen-pos", std::to_string(a.x()) + "," + std::to_string(a.y()));
        live_->setClientEnv("screen-size", std::to_string(a.width()) + "x" + std::to_string(a.height()));
    }
}

// The main window's place on the screen, as its frame's top-left. Stock answers this for mainwindow.pos and outer-pos.
// While maximized, stock answers the place the window is restored to (516.1689: main.pos stayed 100,120 with the frame
// at -8,-8), so a maximized or full-screen window's moves are not published.
void SkinWindow::moveEvent(QMoveEvent* e) {
    QWidget::moveEvent(e);
    if (isWindow() && !isMaximized() && !isFullScreen())
        live_->setWindowPos("", frameGeometry().x(), frameGeometry().y());
    publishFrame();
}

void SkinWindow::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (isWindow() && !isMaximized() && !isFullScreen())
        live_->setWindowPos("", frameGeometry().x(), frameGeometry().y());
    publishFrame();
    publishWindowState();
}

// The frame's size: winget `outer-size`. Stock answers its window rectangle, which on Windows includes the invisible
// resize borders (516.1689 at 96 dpi: 256x298 for a 240x240 client, where Qt's frame geometry is 240x290), so this
// reads the same rectangle of this window, in logical pixels. Elsewhere it is Qt's frame geometry. The menu bar and
// the status strip are this client's, so the answer equals stock's only where they are the same height.
void SkinWindow::publishFrame() {
    if (!isWindow()) return;
    QSize frame = frameGeometry().size();
#ifdef Q_OS_WIN
    RECT r;
    if (testAttribute(Qt::WA_WState_Created) && QGuiApplication::platformName() == QLatin1String("windows") &&
        GetWindowRect(reinterpret_cast<HWND>(winId()), &r)) {
        const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1;
        frame = QSize(int(std::lround((r.right - r.left) / dpr)), int(std::lround((r.bottom - r.top) / dpr)));
    }
#endif
    live_->setWindowFrame(frame.width(), frame.height());
}

void SkinWindow::publishWindowState() {
    if (!isWindow()) return;
    const bool zoomed = isMaximized() || isFullScreen();
    if (zoomed == zoomed_ && !zoomed) return;
    zoomed_ = zoomed;
    QSize restore = restoreClient_;
    if (!restore.isValid() && normalGeometry().isValid())
        restore = QSize(normalGeometry().width(), std::max(0, normalGeometry().height() - chromeHeight()));
    if (!restore.isValid()) restore = normalClient_;
    live_->setWindowState(zoomed, restore.width(), restore.height());
}

// Maximizing publishes the size the window is restored to; restoring gives it a size or pos winset that arrived
// meanwhile (516.1689: main.size=300x300 sent to a maximized window left it maximized and made it 300x300 when
// restored).
void SkinWindow::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() != QEvent::WindowStateChange || !isWindow()) return;
    const bool wasZoomed = zoomed_;
    publishWindowState();
    if (wasZoomed && !zoomed_ && (restoreClient_.isValid() || restorePos_)) {
        const QSize size = std::exchange(restoreClient_, QSize());
        const std::optional<QPoint> pos = std::exchange(restorePos_, std::nullopt);
        // After the platform has put back its own restore geometry.
        QMetaObject::invokeMethod(
            this,
            [this, size, pos] {
                if (isMaximized() || isFullScreen()) return;
                if (size.isValid()) resize(size.width(), size.height() + chromeHeight());
                if (pos) move(*pos);
            },
            Qt::QueuedConnection);
    }
}

void SkinWindow::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    publishClientEnv();
    publishFrame();
    if (isWindow() && !isMaximized() && !isFullScreen() && !isMinimized())
        normalClient_ = QSize(width(), std::max(0, height() - chromeHeight()));
    if (bar_ && !bar_->isHidden()) bar_->setGeometry(0, 0, width(), bar_->sizeHint().height());
    if (status_ && !status_->isHidden()) status_->setGeometry(0, height() - kStatusBarH, width(), kStatusBarH);
    if (!live_->loaded()) return;
    live_->setClient(width(), std::max(0, height() - chromeHeight()));
    syncFromLive();
}

// The map pane is shown once the map has a picture, since stock runs the pane's on-show only from then on: a skin whose
// split side is empty until the map's on-show fills it (a world's copy of the default skin) gets its map then. The pane
// is the window of the skin's default MAP, or its first.
void SkinWindow::markMapShown() {
    if (mapPaneShown_ || !hasMapFrame_ || !snap_.file) return;
    std::string first, def;
    for (const skin::Block& b : snap_.file->blocks)
        if (b.kind == "window")
            for (const skin::Elem* e : b.controls())
                if (e->type() == "MAP") {
                    if (first.empty()) first = b.name;
                    if (def.empty() && e->boolv("is-default", false)) def = b.name;
                }
    mapPaneShown_ = true;
    if (!first.empty()) live_->setPaneShown(def.empty() ? first : def);
}

// The statpanel pane is shown the same way, once it has a tab to show.
void SkinWindow::markInfoShown() {
    if (infoPaneShown_ || statView_.tabs.isEmpty() || !snap_.file) return;
    infoPaneShown_ = true;
    for (const skin::Block& b : snap_.file->blocks)
        if (b.kind == "window")
            for (const skin::Elem* e : b.controls())
                if (e->type() == "INFO") live_->setPaneShown(b.name);
}

void SkinWindow::appendChat(const QStringList& html) {
    chat_ += html;
    while (chat_.size() > 500) chat_.removeFirst();
    if (host_->chatHosted()) {  // the world's own chat page replaces the log: lines go to its output(), once READY
        for (const QString& h : html) host_->chatLine(h);
        return;
    }
    for (auto* o : outputs_) {
        for (const QString& h : html) o->append(h);
        o->verticalScrollBar()->setValue(o->verticalScrollBar()->maximum());
    }
}

void SkinWindow::setStat(const QString& html) { stat_ = html; }  // the INFO control renders setStatView()

void SkinWindow::setStatView(const StatView& v) {
    statView_ = v;
    markInfoShown();
    for (auto* i : infos_) i->setView(v);
}

void SkinWindow::showBrowse(const QString& name, const QString& params, const QString& html, quint64 skinRevision) {
    syncFromLive();  // a preceding dynamic winset may still have its GUI notification queued
    host_->open(name, params, html, skinRevision);
}
void SkinWindow::closeBrowse(const QString& name) { host_->close(name); }
// output(file, control) loads the file into that browser control, never into a window of its own.
void SkinWindow::showOutputFile(const QString& control, const QString& html) {
    if (declaresBrowser(control))
        host_->open(control, QString(), html);
    else
        std::fprintf(stderr, "[browse] output(file) to \"%s\": not a browser control of this skin - dropped\n",
                     control.toUtf8().constData());
}
void SkinWindow::pageCall(const QString& window, const QString& fn, const QStringList& args) {
    host_->pageCall(window, fn, args);
}

// Also a BROWSER a winset created at runtime in a skin window (syncRuntimeBrowsers): a browse() that arrives before
// the GUI has built it is held for it, not opened as a window of its own.
bool SkinWindow::declaresBrowser(const QString& name) const {
    return live_->wingetType(name.toStdString()) == "BROWSER";
}

// The skin file declares it: vg's interface/skin.dmf declares rpane.hosttracker and rpane.hosttracker2 at 1x1 with
// `is-visible = false`.
// Also a control the layout does not show now (its pane is not placed, as tg's browseroutput until its page swaps
// output_browser in, or its floating window is hidden): stock creates every control at load, so its page runs.
bool SkinWindow::declaresHiddenBrowser(const QString& name) const {
    std::string v;
    return declaresBrowser(name) && live_->winget(name.toStdString(), "is-visible", v) && !skin::winsetBool(v);
}

// browse(html, "window=W") where W names a skin window rather than a control: tg's tgui_say is a hidden 231x30
// window holding one BROWSER. Its page runs in that window's browser, so while the window is hidden the page runs
// windowless instead of in a popup of its own.
bool SkinWindow::hiddenWindowBrowser(const QString& name) const {
    const skin::File* f = snap_.file.get();
    if (!f || name.contains('.')) return false;
    for (const skin::Block& b : f->blocks) {
        if (b.kind != "window" || QString::fromStdString(b.name) != name) continue;
        if (skin::findMainWindow(*f) == &b) return false;
        bool browser = false;
        for (const skin::Elem& e : b.elems) browser = browser || e.type() == "BROWSER";
        if (!browser) return false;
        // A floating window's browser runs windowless until its control is built.
        if (skin::isFloatingWindow(*f, b)) {
            for (const skin::Elem& e : b.elems)
                if (e.type() == "BROWSER" && widgets_.count(b.name + "." + e.name)) return false;
            return true;
        }
        std::string v;
        if (!live_->winget(b.name, "is-visible", v)) return false;
        v = skin::lowerAscii(skin::trimSpace(v));
        return v == "false" || v == "0" || v == "no" || v == "off";
    }
    return false;
}

bool SkinWindow::clientOutput(const QString& control, const QString& encodedValue) {
    model::PageCall call;
    if (!model::parsePageCall(control.toStdString(), encodedValue.toStdString(), call, true)) return false;
    QStringList args;
    for (const auto& value : call.args) args << QString::fromStdString(value);
    pageCall(QString::fromStdString(call.target), QString::fromStdString(call.fn), args);
    return true;
}

BrowserPane* SkinWindow::runtimeBrowser(const QString& name) const {
    skin::SkinSnapshot::RuntimeControl rc;
    if (!live_->runtimeControl(name.toStdString(), rc) || rc.type != "BROWSER") return nullptr;
    return qobject_cast<BrowserPane*>(controlWidget(QString::fromStdString(rc.parent + "." + rc.name)));
}

QWidget* SkinWindow::controlWidget(const QString& windowDotName) const {
    auto it = widgets_.find(windowDotName.toStdString());
    return it == widgets_.end() ? nullptr : it->second;
}

std::string SkinWindow::expandMapCommandParams(const std::string& command) const {
    // These are control coordinates, not icon/world coordinates: stock includes letterbox margins
    // and allows negative/outside positions. Do not consult the model or inverse-project a MAP frame.
    const QPoint cursor = QCursor::pos();
    std::string out;
    bool quoted = false;
    for (size_t i = 0; i < command.size();) {
        if (command[i] == '\\' && i + 1 < command.size()) {
            out.append(command, i, 2);
            i += 2;
            continue;
        }
        if (command[i] == '"') quoted = !quoted;
        if (command.compare(i, 2, "[[") == 0) {
            const size_t end = command.find("]]", i + 2);
            if (end != std::string::npos) {
                const std::string token = command.substr(i + 2, end - i - 2);
                const size_t dot = token.rfind('.');
                if (dot != std::string::npos) {
                    const QString id = QString::fromStdString(token.substr(0, dot));
                    const std::string prop = skin::lowerAscii(token.substr(dot + 1));
                    if (prop == "mouse-pos" || prop == "size") {
                        for (const auto& c : snap_.tree.controls) {
                            if (c.type != "MAP") continue;
                            const QString path = QString::fromStdString(c.window + "." + c.name);
                            if (id.compare(path, Qt::CaseInsensitive) &&
                                id.compare(QString::fromStdString(c.name), Qt::CaseInsensitive))
                                continue;
                            if (QWidget* widget = controlWidget(path)) {
                                const QPoint pair = prop == "mouse-pos" ? widget->mapFromGlobal(cursor)
                                                                        : QPoint(widget->width(), widget->height());
                                out += std::to_string(pair.x()) + (quoted ? "," : " ") + std::to_string(pair.y());
                                i = end + 2;
                                break;
                            }
                        }
                        if (i == end + 2) continue;
                    }
                }
                // Skip unsupported tokens as a unit, including [[*]] (expanded later by the router).
                out.append(command, i, end + 2 - i);
                i = end + 2;
                continue;
            }
        }
        out += command[i++];
    }
    return out;
}

QWidget* SkinWindow::splitterHandle(const QString& windowDotName) const {
    auto it = handles_.find(windowDotName.toStdString());
    return it == handles_.end() ? nullptr : it->second;
}

QString SkinWindow::describe() const {
    QString s;
    for (const skin::Control& c : snap_.tree.controls) {
        auto it = widgets_.find(c.window + "." + c.name);
        s += QString("%1 %2.%3 (%4,%5 %6x%7)%8\n")
                 .arg(QString::fromStdString(c.type), QString::fromStdString(c.window), QString::fromStdString(c.name))
                 .arg(c.rect.x0)
                 .arg(c.rect.y0)
                 .arg(c.rect.dx())
                 .arg(c.rect.dy())
                 .arg(it == widgets_.end() ? " [no widget]" : "");
    }
    return s;
}

}  // namespace fugue::shell

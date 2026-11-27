#include "shell/browsehost.h"
#include <QCloseEvent>
#include <QMoveEvent>
#include <QShowEvent>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWidget>
#include <cstdio>

namespace fugue::shell {

namespace {
constexpr int kPopupW = 480, kPopupH = 600;  // stock DreamSeeker's default size for a browse window

class PopupWidget : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;
signals:
    void closedByPlayer();
    void placed(int x, int y);  // the frame's top-left after a show or move
protected:
    void closeEvent(QCloseEvent* e) override {
        emit closedByPlayer();
        e->accept();
    }
    void moveEvent(QMoveEvent* e) override {
        QWidget::moveEvent(e);
        if (isVisible()) emit placed(frameGeometry().x(), frameGeometry().y());
    }
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        emit placed(frameGeometry().x(), frameGeometry().y());
    }
};

#define LOGB(...) std::fprintf(stderr, "[browse] " __VA_ARGS__)
}  // namespace

namespace {
QString gStorageDir;
}

void BrowseHost::setStorageDir(const QString& dir) { gStorageDir = dir; }

BrowseHost::BrowseHost(QWidget* anchor, QObject* parent) : QObject(parent), anchor_(anchor) {
    if (!gStorageDir.isEmpty() && QDir().mkpath(gStorageDir)) {
        // One client at a time per storage directory: Chromium's stores do not take two processes.
        auto* lock = new QLockFile(gStorageDir + "/.lock");
        lock->setStaleLockTime(0);
        if (lock->tryLock(0)) {
            storageLock_ = lock;
            profile_ = new QWebEngineProfile(QStringLiteral("fugue"), this);
            profile_->setPersistentStoragePath(gStorageDir);
            profile_->setCachePath(gStorageDir + "/cache");
            profile_->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
        } else {
            delete lock;
            LOGB("web storage %s is held by another client: this run keeps none\n", gStorageDir.toUtf8().constData());
        }
    }
    if (!profile_) profile_ = new QWebEngineProfile(this);
    // No HTTP cache: a fixed-name browse_rsc asset can be re-minted and must never be served stale.
    profile_->setHttpCacheType(QWebEngineProfile::NoCache);
    // Stock DreamSeeker's embedded browser has no autoplay gate. vg's jukebox calls play() without a user gesture,
    // which Chromium refuses silently, so every page may start media without one.
    profile_->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    origin_ = new LocalOrigin(store_, this);
    if (QCoreApplication* app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, [this] { quitting_ = true; });
    origin_->setBridge([this](int pane, const QByteArray& body) {
        BrowserPane* p = panes_.value(pane);
        if (!p) return QString();
        QString call = QString::fromUtf8(body);
        auto u = model::parseByondUrl(call.toStdString());
        const auto document = u.get("__fugueDocument");
        if (!document.empty()) {
            if (QString::fromStdString(document).toULongLong() != p->document()) return QString();
            const auto at = call.lastIndexOf("__fugueDocument=");
            call.truncate(at - 1);
        }
        return handleCall(p, model::parseByondUrl(call.toStdString()));
    });
    connect(origin_, &LocalOrigin::topic, this, [this](const QString& href) { topicFrom(href); });
    LOGB("local origin %s (listening=%d)\n", origin_->base().left(40).toUtf8().constData(), origin_->listening());
}

BrowseHost::~BrowseHost() {
    // Every popup this host made, including one close() only hid, must be gone before the profile.
    for (auto it = slots_.begin(); it != slots_.end(); ++it)
        if (it->popup) it->popup->disconnect(this);
    for (PopupWidget* w : anchor_->findChildren<PopupWidget*>(QString(), Qt::FindDirectChildrenOnly)) delete w;
    for (auto it = slots_.begin(); it != slots_.end(); ++it)
        if (it->hidden) delete it->pane;
    slots_.clear();
    delete storageLock_;
}

void BrowseHost::setHubStorePath(const QString& p) {
    hubPath_ = p;
    QFile f(p);
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) hub_[it.key()] = it.value().toString();
    }
}

BrowserPane* BrowseHost::makePane(QWidget* parent) {
    auto* p = new BrowserPane(parent, profile_);
    wire(p);
    return p;
}

void BrowseHost::wire(BrowserPane* p) {
    int id = nextPane_++;
    p->attachOrigin(origin_, id);
    panes_[id] = p;
    // Ctrl+F opens the find bar only when the world sets browser-options `find` (bit 2), as in DreamSeeker 516.
    QPointer<BrowseHost> self(this);
    p->findAllowed = [self] { return self && self->live_ && (self->live_->browserOptions() & 2u); };
    connect(p, &BrowserPane::byondLink, this, [this, p](const QUrl& u) {
        // The JS from handleCall (a winget answer) goes back to the asking page: a navigation has no response body.
        QString js = handleCall(p, model::parseByondUrl(u.toString(QUrl::FullyEncoded).toStdString()));
        if (!js.isEmpty()) p->page()->runJavaScript(js);
    });
    connect(p, &BrowserPane::topicQuery, this, [this](const QString& href) { topicFrom(href); });
    connect(p, &BrowserPane::becameReady, this, [this, p] {
        LOGB("page READY: %s (%d queued calls run)\n", p->name.toUtf8().constData(), p->callsRun());
        if (p == chatPane_) {
            LOGB("hosted chat page READY (via chatready)\n");
            emit chatPageReady();
        }
    });
}

void BrowseHost::topicFrom(const QString& raw) {
    LOGB("TOPIC %s\n", raw.left(200).toUtf8().constData());
    emit byondLink(QUrl::fromEncoded((raw.startsWith("byond:") ? raw : "byond://" + raw).toUtf8(), QUrl::TolerantMode));
}

// ------------------------------------------------------------------------------------------------ world -> client

static QString pageFileFor(const QString& name, const model::BrowseParams& p) {
    QString f = QString::fromStdString(p.file);
    if (f.isEmpty()) f = name + ".html";
    f.replace('\\', '/');
    while (f.startsWith('/')) f.remove(0, 1);
    f.replace("..", "_");
    return f;
}

// The popup title bar follows the page's document title, except that stock DreamSeeker blanks a title that is just
// the page's own address (compared with and without the scheme, and with a trailing '/' ignored). Chromium reports
// the loopback address of the local origin as the title of a page with no <title>, so such a page shows no title.
static QString popupTitle(const QString& t, const QUrl& url) {
    auto slashed = [](QString u) {  // the part before '?' gets one trailing '/'
        int q = u.indexOf('?');
        QString base = q < 0 ? u : u.left(q), query = q < 0 ? QString() : u.mid(q);
        if (!base.isEmpty() && !base.endsWith('/')) base += '/';
        return base + query;
    };
    const QString full = url.toString();
    const int sep = full.indexOf("://");
    const QString bare = sep >= 0 ? full.mid(sep + 3) : full, st = slashed(t);
    if (st == slashed(full) || st == slashed(bare)) return {};
    return t.trimmed();
}

static bool looksLikeChat(const QString& name, const QString& html) {
    QString n = name.section('.', -1).toLower();
    return n == "browseroutput" || html.contains("browserOutput.js") || html.contains("ehjaxCallback");
}

// A blank popup title is one space, not "": Qt publishes an empty title as the application name in _NET_WM_NAME,
// where stock DreamSeeker's untitled popup shows nothing at all.
static void setPopupTitle(QWidget* w, const QString& t) { w->setWindowTitle(t.isEmpty() ? QStringLiteral(" ") : t); }

void BrowseHost::open(const QString& name0, const QString& params, const QString& html, quint64 skinRevision) {
    if (skinRevision == ~quint64(0)) skinRevision = live_ ? live_->propertyRevision() : 0;
    model::BrowseParams p = model::parseBrowseParams(params.toStdString());
    QString name = name0;
    if (p.display == 0) {  // saved, never shown
        if (!p.file.empty()) origin_->setPage(pageFileFor(name, p), html);
        LOGB("browse(\"%s\") display=0: saved as \"%s\", not shown\n", name.toUtf8().constData(), p.file.c_str());
        return;
    }
    // An empty window= is the embedded default panel: the first browser control the skin has.
    if (name.isEmpty()) {
        for (auto it = controls_.begin(); it != controls_.end(); ++it) {
            name = it->ctl;
            break;
        }
        if (name.isEmpty()) {
            LOGB("browse with no window= and no browser control: dropped\n");
            return;
        }
    }
    // vg's asset_cache_browser is a pure handshake: one script that navigates to a literal byond:// URL. Answer it
    // directly (model/headlessacks.h) and never host it, since the page never runs on a 1x1 control.
    if (model::hostlessBrowser(name.section('.', -1).toStdString())) {
        if (auto href = navAcks_.navAck(name.section('.', -1).toStdString(), html.toStdString())) {
            LOGB("machine page \"%s\": answered natively -> %s\n", name.toUtf8().constData(),
                 QString::fromStdString(*href).left(120).toUtf8().constData());
            topicFrom(QString::fromStdString(*href));
            return;
        }
        // A name is not evidence of a machine-only document. Real pages with
        // external scripts must load, including hidden dynamic browser controls.
    }
    QString file = pageFileFor(name, p);
    Slot s;
    s.name = name;
    s.file = file;
    s.p = p;
    s.worldTitleWrite = s.worldVisibilityWrite = skinRevision;
    // Lookup order: 1. a created skin control of that name, 2. a control the skin declares but has not created,
    // 3. a top-level window.
    BrowserPane* docked = nullptr;
    QString dockWin;
    for (const Ctl& c : controls_)
        if (name == c.ctl || name == c.win + "." + c.ctl || name.section('.', -1) == c.ctl ||
            (c.byWindow && name == c.win)) {
            docked = c.pane;
            if (c.byWindow) dockWin = c.win;  // Slot::win answers paneFor(window)
            break;
        }
    if (docked) {
        dropHidden(name);
        s.docked = true;
        s.pane = docked;
        s.win = dockWin;
        s.chat = looksLikeChat(name, html);
        pendingDocked_[name] = {params, html};
        slots_[name] = s;
        origin_->setPage(file, html);
        docked->name = name;
        LOGB("browse(\"%s\") -> skin control (docked), %d bytes%s\n", name.toUtf8().constData(),
             int(html.toUtf8().size()), s.chat ? ", CHAT page" : "");
        if (s.chat && chatPane_ != docked) {
            chatPane_ = docked;
            if (backlog_) {
                QStringList l = backlog_();
                for (const QString& x : l) docked->callPage("output", {x});
                LOGB("chat backlog: %d lines queued for the page\n", int(l.size()));
            }
        }
        docked->openFile(file);
        return;
    }
    if (declared_ && declared_(name)) {
        pendingDocked_[name] = {params, html};
        // A declared browser must run even if its pane is not attached yet. Its own is-visible can be true;
        // the startup script may be the handshake that makes the server attach it. Adopt the same page later.
        openHidden(name, file, p, html);
        return;
    }
    if (hiddenWindow_ && hiddenWindow_(name)) {
        LOGB("browse(\"%s\"): a hidden skin window with a browser - its page runs windowless\n",
             name.toUtf8().constData());
        openHidden(name, file, p, html);
        return;
    }
    auto it = slots_.find(name);
    if (it != slots_.end() && it->popup) {
        Slot& e = *it;
        e.p = p;
        e.file = file;
        e.worldTitleWrite = e.worldVisibilityWrite = skinRevision;
        origin_->setPage(file, html);
        if (p.w > 0 && p.h > 0) applyPopupProps(e, "size", QString("%1x%2").arg(p.w).arg(p.h));
        e.pane->openFile(file);
        e.popup->show();
        e.popup->raise();
        if (live_) {
            live_->setPopupShown(name.toStdString(), true, skinRevision);
            syncPopupProps(live_->snapshot().props);
        }
        LOGB("browse(\"%s\") -> existing popup reloaded\n", name.toUtf8().constData());
        return;
    }
    Qt::WindowFlags fl = Qt::Dialog | Qt::WindowCloseButtonHint;
    if (!p.hasTitlebar()) fl |= Qt::FramelessWindowHint;
    if (p.minimizable()) fl |= Qt::WindowMinimizeButtonHint;
    auto* w = new PopupWidget(anchor_, fl);
    w->setProperty("fugueControl",
                   name);  // a drag over the page names the popup's window id (shell/mapwidget.h targetAt)
    // Until its page says otherwise a popup has no title, not its window id: stock DreamSeeker answers "" for
    // winget(p1, "title"). A browse() `title=` option is applied as the window's own parameter.
    setPopupTitle(w, QString::fromStdString(p.extra.count("title") ? p.extra.at("title") : std::string()).trimmed());
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    s.pane = makePane(w);
    s.pane->name = name;
    lay->addWidget(s.pane);
    s.popup = w;
    int W = p.w > 0 ? p.w : kPopupW, H = p.h > 0 ? p.h : kPopupH;
    w->resize(W, H);
    if (!p.resizable()) w->setFixedSize(W, H);  // can_resize=0 pins min and max
    if (p.posSet) w->move(p.x, p.y);
    // The page title and a world winset of `title` both set the title bar; the last writer wins.
    connect(s.pane->page(), &QWebEnginePage::titleChanged, w, [this, w, name, pg = s.pane->page()](const QString& t) {
        auto slot = slots_.constFind(name);
        if (slot == slots_.constEnd() || slot->popup != w) return;
        const QString title = popupTitle(t, pg->url());
        setPopupTitle(w, title);
        if (live_) live_->setPopupTitle(name.toStdString(), title.toStdString());
    });
    // winget(popup, "pos") reports the popup's real place, not the requested one.
    connect(w, &PopupWidget::placed, this, [this, name](int x, int y) {
        if (live_) live_->setWindowPos(name.toStdString(), x, y);
    });
    connect(w, &PopupWidget::closedByPlayer, this, [this, name] {
        // Run the world's on-close. Worlds register it by winset (vg's nanoui, /datum/browser and tgui all do), and
        // the world keeps the ui marked open until that command runs. A popup the world closed itself never gets
        // here, because close() disconnects first. Stock DreamSeeker hides a closed popup rather than destroying it,
        // so its on-close stays registered for the next browse() of that id. Not run on quit.
        if (quitting_ || QCoreApplication::closingDown()) {
            LOGB("popup \"%s\" closed by app quit: on-close not run\n", name.toUtf8().constData());
            return;
        }
        QString onClose;
        if (live_) {
            std::string v;
            if (live_->winget(name.toStdString(), "on-close", v)) onClose = QString::fromStdString(v).trimmed();
            live_->setPopupShown(name.toStdString(), false);
            live_->setPopupTitle(name.toStdString(), "");
        }
        LOGB("popup \"%s\" closed by the player%s%s\n", name.toUtf8().constData(),
             onClose.isEmpty() ? " (no on-close registered)" : ": on-close ", onClose.left(120).toUtf8().constData());
        auto i = slots_.find(name);
        if (i != slots_.end()) {
            origin_->removePage(i->file);
            i->popup->deleteLater();
            slots_.erase(i);
        }
        emit popupClosedByPlayer(name);
        // A byond://?src=... on-close is a Topic, not a command line; stock DreamSeeker sends it as one.
        if (onClose.startsWith("byond:", Qt::CaseInsensitive))
            topicFrom(onClose);
        else if (!onClose.isEmpty())
            emit onCloseCommand(name, onClose);
    });
    if (live_) live_->setPopupTitle(name.toStdString(), w->windowTitle().trimmed().toStdString(), skinRevision);
    slots_[name] = s;
    origin_->setPage(file, html);
    s.pane->openFile(file);
    w->show();
    if (live_) {
        live_->setPopupShown(name.toStdString(), true, skinRevision);
        syncPopupProps(live_->snapshot().props);
    }
    LOGB("browse(\"%s\") -> top-level window %dx%d resize=%d titlebar=%d (%d bytes)\n", name.toUtf8().constData(), W, H,
         p.resizable(), p.hasTitlebar(), int(html.toUtf8().size()));
}

// An unplaced BROWSER (hidden control or unattached pane) runs without a window, as in stock DreamSeeker.
// It is recorded in slots_ like any page, so output() and its startup scripts work before placement.
void BrowseHost::openHidden(const QString& name, const QString& file, const model::BrowseParams& p,
                            const QString& html) {
    auto it = slots_.find(name);
    if (it != slots_.end() && it->hidden) {
        it->p = p;
        it->file = file;
        origin_->setPage(file, html);
        it->pane->openFile(file);
        LOGB("browse(\"%s\") -> HIDDEN page reloaded\n", name.toUtf8().constData());
        return;
    }
    Slot s;
    s.name = name;
    s.file = file;
    s.p = p;
    s.hidden = true;
    // Parented to the main window so it shares its screen, and explicitly hidden: a later show() of the anchor
    // does not show it.
    s.pane = makePane(anchor_);
    s.pane->name = name;
    s.pane->setGeometry(0, 0, 1, 1);
    s.pane->hide();
    slots_[name] = s;
    origin_->setPage(file, html);
    s.pane->openFile(file);
    LOGB("browse(\"%s\") -> HIDDEN page at %s (unplaced skin browser; document runs before placement)\n",
         name.toUtf8().constData(), origin_->urlFor(file).toUtf8().constData());
}

void BrowseHost::dropHidden(const QString& name) {
    auto it = slots_.find(name);
    if (it == slots_.end() || !it->hidden) return;
    BrowserPane* p = it->pane;
    origin_->removePage(it->file);
    slots_.erase(it);
    for (auto i = panes_.begin(); i != panes_.end();) {
        if (i->data() == p)
            i = panes_.erase(i);
        else
            ++i;
    }
    p->deleteLater();
}

bool BrowseHost::isHiddenPage(const QString& window) const {
    auto it = slots_.constFind(window);
    return it != slots_.constEnd() && it->hidden;
}

void BrowseHost::close(const QString& name) {
    pendingDocked_.remove(name);
    auto it = slots_.find(name);
    if (it == slots_.end()) {
        LOGB("close(\"%s\"): nothing open under that name\n", name.toUtf8().constData());
        return;
    }
    if (it->hidden) {
        dropHidden(name);
        LOGB("close(\"%s\"): hidden page closed\n", name.toUtf8().constData());
        return;
    }
    origin_->removePage(it->file);
    if (it->popup) {
        if (live_) {
            live_->setPopupShown(name.toStdString(), false);
            live_->setPopupTitle(name.toStdString(), "");
        }
        it->popup->disconnect(this);
        it->popup->hide();
        it->popup->deleteLater();
        LOGB("close(\"%s\"): popup closed\n", name.toUtf8().constData());
    } else if (it->pane) {
        it->pane->name.clear();
        it->pane->setUrl(QUrl("about:blank"));
        if (it->pane == chatPane_) chatPane_ = nullptr;
        LOGB("close(\"%s\"): docked control cleared\n", name.toUtf8().constData());
    }
    slots_.erase(it);
}

BrowserPane* BrowseHost::paneFor(const QString& window) const {
    auto it = slots_.constFind(window);
    if (it != slots_.constEnd()) return it->pane;
    for (const Slot& s : slots_)
        if ((s.docked && (s.name.section('.', -1) == window || s.win == window)) ||
            (s.hidden && s.name.section('.', -1) == window))
            return s.pane;
    for (const Ctl& c : controls_)
        if (c.ctl == window || c.win + "." + c.ctl == window || (c.byWindow && c.win == window))
            return c.pane->name.isEmpty() ? nullptr : c.pane;
    return nullptr;
}

QWidget* BrowseHost::popupFor(const QString& window) const {
    auto it = slots_.constFind(window);
    return it == slots_.constEnd() ? nullptr : it->popup;
}

// The DreamSeeker name of a focused widget inside one of this host's panes: a popup's page is "<popup>.browser"
// (stock winget(null, "focus") answers that). A docked pane is named by its skin control, which SkinWindow resolves.
// Returns "" for widgets this host does not own.
QString BrowseHost::focusIdFor(QWidget* w) const {
    for (; w; w = w->parentWidget())
        for (const Slot& s : slots_)
            if (s.popup && (w == s.popup || w == s.pane)) return s.name + ".browser";
    return {};
}

QStringList BrowseHost::openWindows() const {
    QStringList l;
    for (auto it = slots_.constBegin(); it != slots_.constEnd(); ++it) l << it.key();
    l.sort();
    return l;
}

BrowserPane* BrowseHost::paneForTarget(const QString& target) const {
    // output()'s target is `[window.]control`. A page open under the whole name wins. A hidden page is also matched
    // by its last part alone, so "rpane.hosttracker2:SetMusic" finds the page browse()d as "rpane.hosttracker2".
    if (BrowserPane* p = paneFor(target)) return p;
    for (const Slot& s : slots_)
        if (s.hidden && s.name.section('.', -1) == target.section('.', -1)) return s.pane;
    // Otherwise the last part is a browse window's browser control: "stat.stat", "tgui-window-3.browser".
    return target.contains('.') ? paneFor(target.section('.', 0, -2)) : nullptr;
}

void BrowseHost::pageCall(const QString& window, const QString& fn, const QStringList& args) {
    BrowserPane* p = paneForTarget(window);
    if (!p) {
        QString sig = window + ":" + fn;
        if (!dropLogged_.contains(sig)) {
            dropLogged_.insert(sig);
            LOGB("page call %s DROPPED - no page is open for \"%s\"\n", sig.toUtf8().constData(),
                 window.toUtf8().constData());
        }
        return;
    }
    LOGB("page call %s.%s(%d arg%s) -> %s%s\n", window.toUtf8().constData(), fn.toUtf8().constData(), int(args.size()),
         args.size() == 1 ? "" : "s", p->name.toUtf8().constData(), p->ready() ? "" : " (queued until READY)");
    p->callPage(fn, args);
}

void BrowseHost::adoptControl(const QString& win, const QString& ctl, BrowserPane* pane, bool byWindow) {
    controls_.append({win, ctl, pane, byWindow});
    wire(pane);
    // A page the world sent before the control existed.
    for (auto it = pendingDocked_.begin(); it != pendingDocked_.end(); ++it) {
        const QString& n = it.key();
        if (n == ctl || n == win + "." + ctl || n.section('.', -1) == ctl) {
            Pending pd = it.value();
            QString n2 = n;
            LOGB("control \"%s\" created: showing the page held for it\n", ctl.toUtf8().constData());
            dropHidden(n2);  // it ran windowless until now; the control is its home from here on
            open(n2, pd.params, pd.html);
            break;
        }
    }
}

// A BROWSER control is one browser in stock DreamSeeker: showing a hidden one does not reload its page. vg's credits
// rely on it: the page is set up while hidden and makes its own control visible.
BrowserPane* BrowseHost::takeHidden(const QString& win, const QString& ctl) {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
        const QString& n = it.key();
        if (!it->hidden || !(n == ctl || n == win + "." + ctl || n.section('.', -1) == ctl || n == win)) continue;
        it->hidden = false;
        it->docked = true;
        it->win = win;
        pendingDocked_.remove(n);
        controls_.append({win, ctl, it->pane});
        LOGB("control \"%s\" created: it takes over the page that ran hidden\n", ctl.toUtf8().constData());
        return it->pane;
    }
    return nullptr;
}

void BrowseHost::releaseControl(BrowserPane* pane) {
    for (int i = 0; i < controls_.size(); i++)
        if (controls_[i].pane == pane) {
            controls_.removeAt(i);
            break;
        }
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->pane == pane && it->docked)
            it = slots_.erase(it);
        else
            ++it;
    }
    if (chatPane_ == pane) chatPane_ = nullptr;
    for (auto it = panes_.begin(); it != panes_.end();) {
        if (it->data() == pane)
            it = panes_.erase(it);
        else
            ++it;
    }
}

void BrowseHost::chatLine(const QString& html) {
    if (chatPane_) chatPane_->callPage("output", {html});
    if (++chatLines_ <= 5)
        LOGB("chat line %d -> page output() (%s)\n", chatLines_, chatReady() ? "READY" : "queued until READY");
}

// ------------------------------------------------------------------------------------------------ page -> client

bool BrowseHost::validCallback(const QString& cb) {
    // Accepts `Byond.__callbacks__[7]` or a plain function name such as wingetMacros. The name is spliced into a
    // script, so only dotted identifiers and numeric subscripts pass: never a call or a string.
    static const QRegularExpression re(R"(^[A-Za-z_$][\w$]*(\.[A-Za-z_$][\w$]*|\[\d{1,9}\])*$)");
    return re.match(cb).hasMatch() && cb.size() < 128;
}

QString BrowseHost::jsCallback(const QString& cb, const QJsonValue& v) const {
    if (!validCallback(cb)) return {};
    QJsonDocument d = v.isObject() ? QJsonDocument(v.toObject()) : QJsonDocument(QJsonArray{v});
    QString js = QString::fromUtf8(d.toJson(QJsonDocument::Compact));
    if (!v.isObject()) js = js.mid(1, js.size() - 2);
    return QString("try{(%1)(%2)}catch(e){}").arg(cb, js);
}

static QJsonValue convProp(const QString& prop, const QString& v, qreal scale = 1) {
    static const QRegularExpression pt(R"(^(-?\d+),(-?\d+)$)");
    if (prop == "pos" || prop == "anchor1" || prop == "anchor2") {
        auto m = pt.match(v);
        const qreal factor = prop == "pos" ? scale : 1;
        if (m.hasMatch())
            return QJsonObject{{"x", qRound(m.captured(1).toInt() * factor)},
                               {"y", qRound(m.captured(2).toInt() * factor)}};
    }
    if (prop == "size" || prop == "view-size" || prop == "inner-size" || prop == "outer-size") {
        static const QRegularExpression size(R"(^(-?\d+)x(-?\d+)$)");
        auto m = size.match(v);
        if (m.hasMatch())
            return QJsonObject{{"x", qRound(m.captured(1).toInt() * scale)},
                               {"y", qRound(m.captured(2).toInt() * scale)}};
    }
    if (prop == "type") return v.toUpper();
    if (v == "true") return true;
    if (v == "false") return false;
    return v;
}

QJsonObject BrowseHost::macroSet(const QString& set) const {
    QJsonObject out;
    if (!live_) return out;
    auto snap = live_->snapshot();
    if (!snap.file) return out;
    const skin::Block* b = snap.file->macro(set.toStdString());
    if (!b) return out;
    int i = 0;
    for (const skin::Elem& e : b->elems) {
        i++;
        QString name = e.name.empty() ? QString("macro%1").arg(i) : QString::fromStdString(e.name);
        for (const skin::KV& kv : e.rows)
            out[set + "." + name + "." + QString::fromStdString(kv.key)] = QString::fromStdString(kv.value);
    }
    return out;
}

QJsonObject BrowseHost::wingetObject(const QString& id, const QString& propSpec) const {
    QString spec = propSpec.isEmpty() ? "*" : propSpec;
    QStringList props = spec == "*" ? QStringList{"*"} : spec.split(QRegularExpression("[;,]"), Qt::SkipEmptyParts);
    if (id.endsWith(".*")) return macroSet(id.left(id.size() - 2));
    QJsonObject out;
    auto it = slots_.constFind(id);
    if (it != slots_.constEnd() && it->popup) {
        QWidget* w = it->popup;
        QJsonObject all{{"pos", QJsonObject{{"x", qRound(w->pos().x() * w->devicePixelRatioF())},
                                            {"y", qRound(w->pos().y() * w->devicePixelRatioF())}}},
                        {"size", QJsonObject{{"x", qRound(w->width() * w->devicePixelRatioF())},
                                             {"y", qRound(w->height() * w->devicePixelRatioF())}}},
                        {"is-visible", w->isVisible()},
                        {"titlebar", it->p.hasTitlebar()},
                        {"can-resize", it->p.resizable()},
                        {"type", "MAIN"}};  // a popup is a window
        if (props == QStringList{"*"}) return all;
        for (const QString& p : props) out[p] = all.value(p);
        return out;
    }
    if (live_) {
        static const QStringList wild = {"type", "pos",   "size",  "is-visible", "is-disabled",      "is-default",
                                         "text", "title", "macro", "is-checked", "background-color", "text-color"};
        for (const QString& p : props == QStringList{"*"} ? wild : props) {
            std::string v;
            if (live_->winget(id.toStdString(), p.toStdString(), v))
                out[p] = convProp(p, QString::fromStdString(v), anchor_->devicePixelRatioF());
            else if (props != QStringList{"*"})
                out[p] = QJsonValue::Null;  // a named property is always returned as a key
        }
    } else if (props != QStringList{"*"}) {
        for (const QString& p : props) out[p] = QJsonValue::Null;
    }
    return out;
}

// Property write stamps distinguish an unchanged snapshot from a repeated world write after a page changed
// the window. One stamp per supported property, no queued command history. Browse resets consume only writes
// preceding that browse's worker-side stamp, not later winsets already present in a coalesced GUI snapshot.
void BrowseHost::syncPopupProps(const skin::PropStore& props) {
    for (Slot& s : slots_) {
        if (!s.popup) continue;
        for (const char* key : {"title", "is-visible"}) {
            const auto revision = props.revision(s.name.toStdString(), key);
            quint64& consumed = QString::fromLatin1(key) == "title" ? s.worldTitleWrite : s.worldVisibilityWrite;
            if (revision <= consumed) continue;
            std::string value;
            if (!props.get(s.name.toStdString(), key, value)) continue;
            consumed = revision;
            applyPopupProps(s, QString::fromLatin1(key), QString::fromStdString(value), revision);
        }
    }
}

void BrowseHost::applyPopupProps(Slot& s, const QString& prop, const QString& val, quint64 throughRevision) {
    QWidget* w = s.popup;
    if (!w) return;
    if (prop == "size") {
        model::BrowseParams q = model::parseBrowseParams("size=" + val.toStdString());
        if (q.w > 0 && q.h > 0) {
            if (!s.p.resizable())
                w->setFixedSize(q.w, q.h);
            else
                w->resize(q.w, q.h);
        }
    } else if (prop == "pos") {
        model::BrowseParams q = model::parseBrowseParams("pos=" + val.toStdString());
        if (q.posSet) w->move(q.x, q.y);
    } else if (prop == "is-visible") {
        const bool shown = skin::winsetBool(val.toStdString());
        w->setVisible(shown);
        if (live_) live_->setPopupShown(s.name.toStdString(), shown, throughRevision);
    } else if (prop == "can-resize") {
        bool on = !(val == "false" || val == "0");
        s.p.canResize = on;
        if (on) {
            w->setMinimumSize(0, 0);
            w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        } else
            w->setFixedSize(w->size());
    } else if (prop == "focus") {
        if (val == "true") {
            w->raise();
            w->activateWindow();
        }
    } else if (prop == "title") {
        setPopupTitle(w, val.trimmed());
        if (live_) live_->setPopupTitle(s.name.toStdString(), val.trimmed().toStdString(), throughRevision);
    } else {
        LOGB("popup winset %s=%s: not applied\n", prop.toUtf8().constData(), val.left(40).toUtf8().constData());
    }
}

QString BrowseHost::applyWinset(BrowserPane*, const model::ByondUrl& u) {
    QString id = QString::fromStdString(u.get("id"));
    for (const auto& kv : u.params) {
        QString k = QString::fromStdString(kv.first), v = QString::fromStdString(kv.second);
        if (k == "id") continue;
        QString ctl = id, prop = k;
        if (id.isEmpty()) {
            int dot = k.lastIndexOf('.');
            if (dot < 0) {
                ctl.clear();
                prop = k;
            } else {
                ctl = k.left(dot);
                prop = k.mid(dot + 1);
            }
        }
        if (prop == "command") {
            LOGB("winset command=%s\n", v.left(120).toUtf8().constData());
            emit commandRequested(v);
            continue;
        }
        auto it = slots_.find(ctl);
        const bool popup = it != slots_.end() && it->popup;
        // Browser geometry is in device pixels (getBoundingClientRect * DPR, window.screenLeft * DPR).
        // The live skin and Qt widgets use logical pixels. Round only here. A popup is no exception: winget answers
        // its place and size in device pixels, and tgui adds the difference between the two to every position it
        // sets, so a popup moved by unscaled numbers drifts further off with each opening on a scaled display.
        if (prop == "size" || prop == "pos") {
            static const QRegularExpression geometry(R"(^(-?\d+(?:\.\d+)?)[x,](-?\d+(?:\.\d+)?)$)");
            const qreal dpr = popup ? it->popup->devicePixelRatioF() : anchor_->devicePixelRatioF();
            auto m = geometry.match(v);
            if (m.hasMatch())
                v = QString("%1%2%3")
                        .arg(qRound(m.captured(1).toDouble() / dpr))
                        .arg(prop == "size" ? 'x' : ',')
                        .arg(qRound(m.captured(2).toDouble() / dpr));
        }
        if (popup) {
            applyPopupProps(*it, prop, v);
            continue;
        }
        if (!live_) continue;
        // URL-encode the value before passing it to the DM winset parser: a
        // semicolon or quote inside a JS value must never become another pair.
        const auto encoded = QUrl::toPercentEncoding(v);
        skin::WinsetResult r = live_->winset(ctl.toStdString(), prop.toStdString() + "=" + encoded.toStdString(), true);
        LOGB("winset %s%s%s=%s -> applied %zu unhandled %zu\n", ctl.toUtf8().constData(), ctl.isEmpty() ? "" : ".",
             prop.toUtf8().constData(), v.left(40).toUtf8().constData(), r.applied.size(), r.unhandled.size());
    }
    return {};
}

QString BrowseHost::handleCall(BrowserPane* from, const model::ByondUrl& u) {
    QString js;
    if (from && live_) {
        const auto identity = from->property("runtimeIdentity").toULongLong();
        if (identity) {
            auto snap = live_->snapshot();
            const auto* c = snap.tree.byName(from->name.toStdString());
            if (!c || c->runtimeIdentity != identity) return {};
        }
    }
    if (u.path.empty()) {
        // A Topic. A bare "?" href arrives as "byond://?..".
        QString q;
        for (size_t i = 0; i < u.raw.size(); i++)
            if (u.raw[i] == '?') {
                q = QString::fromStdString(u.raw.substr(i));
                break;
            }
        topicFrom(q);
        return {};
    }
    if (u.path == "__ready") {
        if (from) from->markReady();
        return {};
    } else if (u.path == "winget") {
        QString cb = QString::fromStdString(u.get("callback"));
        if (cb.isEmpty()) {
            LOGB("winget with no callback: nothing waits for it\n");
            return {};
        }
        QJsonObject o = wingetObject(QString::fromStdString(u.get("id")), QString::fromStdString(u.get("property")));
        js = jsCallback(cb, o);
        LOGB("winget id=%s property=%s -> %s\n", u.get("id").c_str(), u.get("property").c_str(),
             QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).left(160).toUtf8().constData());
    } else if (u.path == "winset") {
        // tgui's focusMap sends winset?focus=true&id=mapwindow.map, the same as mapwindow.map.focus=true
        applyWinset(from, u);
    } else if (u.path == "hubStorage") {
        QString op = QString::fromStdString(u.get("op")), key = QString::fromStdString(u.get("key")),
                cb = QString::fromStdString(u.get("callback"));
        QJsonValue ret = QJsonValue::Null;
        if (op == "get") {
            if (hub_.contains(key)) ret = hub_.value(key);
        } else if (op == "set")
            hub_[key] = QString::fromStdString(u.get("value"));
        else if (op == "remove")
            hub_.remove(key);
        else if (op == "clear")
            hub_.clear();
        if (op != "get" && !hubPath_.isEmpty()) {
            QJsonObject o;
            for (auto it = hub_.begin(); it != hub_.end(); ++it) o[it.key()] = it.value();
            QFile f(hubPath_);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(o).toJson());
        }
        js = jsCallback(cb, ret);
    } else if (u.path.find_first_of(".:") != std::string::npos) {
        LOGB("page link to another world: %s\n", u.raw.substr(0, 200).c_str());
        emit worldLink(QString::fromStdString(u.raw));
    } else {
        QString sig = QString::fromStdString(u.path);
        if (!dropLogged_.contains("call:" + sig)) {
            dropLogged_.insert("call:" + sig);
            LOGB("page called byond://%s - a client-side call this client does not answer\n", u.path.c_str());
        }
    }
    // A navigation has no response body to carry the JS, so the caller runs it in the asking page. The XHR bridge does return it.
    return js;
}

}  // namespace fugue::shell

#include "browsehost.moc"

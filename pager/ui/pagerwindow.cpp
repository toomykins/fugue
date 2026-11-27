#include "pager/ui/pagerwindow.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QFontDatabase>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include "account/records.h"
#include "fugue_version.h"
#include "pager/cache.h"
#include "pager/launch.h"
#include "pager/target.h"
#include "pager/ui/icons.h"

namespace fugue::pager {
namespace {
QString Q(const std::string& s) { return QString::fromStdString(s); }
const char* kOk = "#5fd07f";
const char* kBad = "#ff6b5e";
const char* kDim = "#9a9a9a";
const char* kLink = "#6ea8ff";

// The look follows the stock 516 pager: black panes, dark-grey bars, white bold text, blue pill buttons with a
// top-light gradient, thin grey row separators.
const char* kStyle = R"(
#pager { background:#2b2b2b; }
#pager QWidget { color:white; }
#pager QLabel { background:transparent; }
QWidget#bar { background:#2b2b2b; }
QWidget#homepane, QWidget#gamepane { background:#000000; }
QFrame#sep { background:#3a3a3a; min-height:1px; max-height:1px; border:none; }
QPushButton[pill="true"], QToolButton[iconbtn="true"] {
  color:white; font-weight:bold; border:1px solid #1d3f8a; border-radius:7px; padding:2px 9px;
  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6f9cf0, stop:0.5 #3d6fd8, stop:1 #2c58b8);
}
QPushButton[pill="true"]:hover, QToolButton[iconbtn="true"]:hover {
  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #86b0ff, stop:0.5 #4f80ea, stop:1 #3466cc);
}
QPushButton[pill="true"]:pressed, QToolButton[iconbtn="true"]:pressed, QToolButton[iconbtn="true"]:checked { background:#244a9c; }
QPushButton[pill="true"]:disabled { color:#8c8c8c; border-color:#444; background:#464646; }
QPushButton[pill="true"]::menu-indicator, QToolButton[iconbtn="true"]::menu-indicator { image:none; width:0; }
QToolButton[iconbtn="true"] { border-radius:3px; padding:1px; }
#pager QPushButton[link="true"], QDialog QPushButton[link="true"] { color:#6ea8ff; background:transparent; border:none; padding:0; font-weight:normal; }
QPushButton[link="true"]:hover { text-decoration:underline; }
#pager QPushButton[link="true"]:disabled { color:#777; }
QLineEdit { background:#3a3a3a; color:white; border:1px solid #777; padding:2px 4px; selection-background-color:#3d6fd8; }
QLineEdit:disabled { color:#888; }
QComboBox { background:#3a3a3a; color:white; border:1px solid #777; padding:1px 4px; selection-background-color:#3d6fd8; }
QComboBox:disabled { color:#888; }
QComboBox QLineEdit { border:none; padding:0; background:transparent; }
QComboBox QAbstractItemView { background:#2b2b2b; color:white; border:1px solid #777; selection-background-color:#3d6fd8; }
QListWidget { background:#000000; border:none; outline:none; }
QTabWidget::pane { border:1px solid #4a4a4a; background:#000000; top:-1px; }
QTabBar::tab { background:#0c0c0c; color:white; font-weight:bold; border:1px solid #5a5a5a; border-bottom:none;
  border-top-left-radius:4px; border-top-right-radius:4px; padding:3px 9px; margin-right:2px; }
QTabBar::tab:selected { background:#4a4a4a; }
QTabBar::tab:hover:!selected { background:#262626; }
QTabBar::tab:disabled { color:#7a7a7a; }
QPlainTextEdit, QTableWidget { background:#101010; color:#dddddd; border:1px solid #444; }
QTableWidget { alternate-background-color:#1c1c1c; gridline-color:#333; }
QHeaderView::section { background:#2b2b2b; color:white; border:none; border-right:1px solid #444; padding:3px; font-weight:bold; }
QScrollBar:vertical { background:#1a1a1a; width:12px; margin:0; }
QScrollBar::handle:vertical { background:#5a5a5a; border-radius:5px; min-height:24px; margin:1px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:#1a1a1a; height:12px; margin:0; }
QScrollBar::handle:horizontal { background:#5a5a5a; border-radius:5px; min-width:24px; margin:1px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
QCheckBox { color:white; }
QDialog { background:#2b2b2b; }
QMenu { background:#f2f2f2; color:black; border:1px solid #9a9a9a; padding:3px 0; }
QMenu::item { padding:4px 30px 4px 26px; color:black; }
QMenu::item:selected { background:#cde6f7; }
QMenu::item:disabled { color:#a0a0a0; }
QMenu::separator { height:1px; background:#d0d0d0; margin:4px 8px; }
QMessageBox { background:#2b2b2b; }
QMessageBox QLabel { color:white; }
QMessageBox QPushButton { color:white; font-weight:bold; border:1px solid #1d3f8a; border-radius:7px; padding:3px 14px; min-width:60px;
  background:qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #6f9cf0, stop:0.5 #3d6fd8, stop:1 #2c58b8); }
QMessageBox QPushButton:focus { border-color:#bcd4ff; }
)";

// parseTarget's sentences name protocol details; the add-server field says it plainly.
QString plainTargetError(const std::string& err) {
    if (err.find("hub WORLD reference") != std::string::npos)
        return "That needs a port number, like example.com:1234. (BYOND hub links like Name.World cannot be opened "
               "here yet.)";
    if (err.find("no server address") != std::string::npos) return "Type the server's address first.";
    return "That doesn't look like a server address. Type it like example.com:1234 or byond://example.com:1234.";
}

QPushButton* pill(const QString& text, QWidget* parent, const char* name = nullptr) {
    auto* b = new QPushButton(text, parent);
    b->setProperty("pill", true);
    b->setCursor(Qt::PointingHandCursor);
    if (name) b->setObjectName(name);
    return b;
}
QPushButton* link(const QString& text, QWidget* parent, const char* name = nullptr) {
    auto* b = new QPushButton(text, parent);
    b->setProperty("link", true);
    b->setFlat(true);
    b->setCursor(Qt::PointingHandCursor);
    if (name) b->setObjectName(name);
    return b;
}
QFrame* sep(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName("sep");
    return f;
}
QString a(const QString& href, const QString& text) {
    return QString("<a href=\"%1\" style=\"color:%2; text-decoration:none;\">%3</a>")
        .arg(href.toHtmlEscaped(), kLink, text.toHtmlEscaped());
}

// List rows: data roles the delegate draws. Rows are PAINTED (no per-row widgets), like BYOND's list.
enum Role { kAddr = Qt::UserRole + 1, kPlayers, kBlocked, kPlaceholder };

class GameRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex&) const override {
        return QSize(opt.rect.width(), 22);
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& ix) const override {
        p->save();
        const QRect r = opt.rect;
        QColor bg = (ix.row() % 2) ? QColor("#1e1e1e") : QColor("#000000");
        if (opt.state & QStyle::State_Selected)
            bg = QColor("#2d4f95");
        else if (opt.state & QStyle::State_MouseOver)
            bg = QColor("#303030");
        p->fillRect(r, bg);
        QFont f = opt.font;
        if (ix.data(kPlaceholder).toBool()) {
            f.setItalic(true);
            p->setFont(f);
            p->setPen(QColor(kDim));
            p->drawText(r.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                        opt.fontMetrics.elidedText(ix.data().toString(), Qt::ElideRight, r.width() - 16));
            p->restore();
            return;
        }
        int x = r.left() + 4;
        const QPixmap ic = ix.data(Qt::DecorationRole).value<QPixmap>();
        if (!ic.isNull()) p->drawPixmap(x, r.top() + (r.height() - 16) / 2, 16, 16, ic);
        x += 22;
        int rx = r.right() - 4;
        if (ix.data(kBlocked).toBool()) {
            p->drawPixmap(rx - 13, r.top() + (r.height() - 14) / 2, icons::lock(14));
            rx -= 18;
        }
        const int players = ix.data(kPlayers).isValid() ? ix.data(kPlayers).toInt() : -1;
        f.setBold(true);
        p->setFont(f);
        QFontMetrics fm(f);
        if (players >= 0) {
            p->drawPixmap(rx - 13, r.top() + (r.height() - 14) / 2, icons::people(14));
            rx -= 17;
            QString n = QString::number(players);
            int w = fm.horizontalAdvance(n);
            p->setPen(Qt::white);
            p->drawText(QRect(rx - w, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignRight, n);
            rx -= w + 6;
        }
        p->setPen(Qt::white);
        p->drawText(QRect(x, r.top(), qMax(0, rx - x), r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                    fm.elidedText(ix.data().toString(), Qt::ElideRight, qMax(0, rx - x)));
        p->restore();
    }
};

// Remove a closable right-pane tab. QTabWidget would make a neighbour current, so go back to Home explicitly.
void closeTab(QTabWidget* tw, QWidget* page, QWidget* home) {
    const int ix = tw->indexOf(page);
    if (ix < 0) return;
    const bool wasCurrent = tw->currentWidget() == page;
    tw->removeTab(ix);
    if (wasCurrent) tw->setCurrentWidget(home);
}

QListWidget* gameList(QWidget* parent, const char* name) {
    auto* l = new QListWidget(parent);
    l->setObjectName(name);
    l->setItemDelegate(new GameRowDelegate(l));
    l->setMouseTracking(true);
    l->setUniformItemSizes(true);
    l->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);  // always show the scrollbar
    return l;
}
}  // namespace

PagerWindow::PagerWindow(QWidget* parent) : QWidget(parent) {
    setObjectName("pager");
    qApp->installEventFilter(this);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(kStyle);
    setWindowTitle("Fugue");
    resize(940, 540);
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(8);

    // ================= LEFT: Games =================
    auto* leftTabs = new QTabWidget(this);
    leftTabs->setObjectName("lefttabs");
    leftTabs->setFixedWidth(312);
    leftTabs->setDocumentMode(false);
    root->addWidget(leftTabs);
    auto* gp = new QWidget;
    auto* gl = new QVBoxLayout(gp);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(0);
    leftTabs->addTab(gp, icons::games(), "Games");

    auto* bar = new QWidget(gp);
    bar->setObjectName("bar");
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* bl = new QVBoxLayout(bar);
    bl->setContentsMargins(6, 6, 6, 6);
    bl->setSpacing(6);
    auto* r2 = new QHBoxLayout;
    r2->setSpacing(6);
    auto* fl = new QLabel("Filter:", bar);
    fl->setStyleSheet("font-weight:bold;");
    filter_ = new QLineEdit(bar);
    filter_->setObjectName("filter");
    filter_->setClearButtonEnabled(true);
    openBtn_ = pill("Open...", bar, "open");
    openBtn_->setToolTip("Add a server by its address");
    openBtn_->setMinimumWidth(openBtn_->sizeHint().width());  // never elide the pill
    r2->addWidget(fl);
    r2->addWidget(filter_, 1);
    r2->addWidget(openBtn_);
    bl->addLayout(r2);
    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });

    // Open... : add a server by address (BYOND's Open Location)
    addRow_ = new QWidget(bar);
    auto* ar = new QVBoxLayout(addRow_);
    ar->setContentsMargins(0, 0, 0, 0);
    ar->setSpacing(4);
    auto* arl = new QLabel("Server address:", addRow_);
    arl->setStyleSheet("font-weight:bold;");
    ar->addWidget(arl);
    addEdit_ = new QLineEdit(addRow_);
    addEdit_->setObjectName("addaddr");
    addEdit_->setPlaceholderText("example.com:1234 or byond://example.com:1234");
    ar->addWidget(addEdit_);
    auto* arh = new QHBoxLayout;
    addOk_ = pill("Add", addRow_, "addok");
    addCancel_ = pill("Cancel", addRow_, "addcancel");
    arh->addStretch(1);
    arh->addWidget(addOk_);
    arh->addWidget(addCancel_);
    ar->addLayout(arh);
    addMsg_ = new QLabel(addRow_);
    addMsg_->setWordWrap(true);
    addMsg_->setStyleSheet(QString("color:%1;").arg(kBad));
    addMsg_->hide();
    ar->addWidget(addMsg_);
    addRow_->hide();
    bl->addWidget(addRow_);
    gl->addWidget(bar);
    connect(openBtn_, &QPushButton::clicked, this, &PagerWindow::showAddServer);
    connect(addCancel_, &QPushButton::clicked, this, [this] {
        addEdit_->clear();
        addMsg_->hide();
        addRow_->hide();
    });
    connect(addOk_, &QPushButton::clicked, this, [this] { submitAddServer(addEdit_->text()); });
    connect(addEdit_, &QLineEdit::returnPressed, this, [this] { submitAddServer(addEdit_->text()); });

    marks_ = gameList(gp, "servers");
    marks_->setContextMenuPolicy(Qt::CustomContextMenu);
    gl->addWidget(marks_, 1);
    // The account status bar under the list, where BYOND's pager kept its status bar: the left pane is on screen whatever
    // tab the right one shows. Filled with pages_ below.
    auto* acctBar = new QWidget(gp);
    acctBar->setObjectName("bar");
    acctBar->setAttribute(Qt::WA_StyledBackground, true);
    auto* acctLay = new QHBoxLayout(acctBar);
    acctLay->setContentsMargins(6, 4, 6, 4);
    gl->addWidget(acctBar);
    connect(marks_, &QListWidget::currentRowChanged, this, [this] {
        updateJoin();
        updateGameTab();
    });
    connect(marks_, &QListWidget::itemClicked, this, [this] { openGameTab(); });
    connect(marks_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { joinRow(marks_->row(it)); });
    connect(marks_, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        int r = marks_->row(marks_->itemAt(pos));
        if (r < 0 || r >= int(sorted_.size())) return;
        QMenu m(this);
        QAction* jn = m.addAction("Join");
        jn->setEnabled(!running_ && !blocker(st_, sorted_[size_t(r)]) && page() == ServersPage);
        QAction* wm = m.addAction(warming_ ? "Cancel download" : "Download resources");
        wm->setEnabled(warming_ || jn->isEnabled());
        QAction* rm = m.addAction("Remove server");
        QAction* got = m.exec(marks_->viewport()->mapToGlobal(pos));
        if (got == rm)
            emit serverRemoved(Q(sorted_[size_t(r)].addr));
        else if (got == jn)
            joinRow(r);
        else if (got == wm)
            warmRow(r);
    });

    // ================= RIGHT: Home | <game> | Details =================
    right_ = new QTabWidget(this);
    right_->setObjectName("right");
    right_->setTabsClosable(true);
    right_->setMovable(false);
    root->addWidget(right_, 1);
    auto* corner = new QWidget(right_);
    auto* ch = new QHBoxLayout(corner);
    ch->setContentsMargins(0, 0, 2, 2);
    ch->setSpacing(3);
    auto tool = [corner](const QIcon& ic, const QString& tip, const char* name) {
        auto* t = new QToolButton(corner);
        t->setProperty("iconbtn", true);
        t->setObjectName(name);
        t->setIcon(ic);
        t->setIconSize(QSize(14, 14));
        t->setFixedSize(20, 20);
        t->setToolTip(tip);
        t->setCursor(Qt::PointingHandCursor);
        return t;
    };
    openTool_ = tool(icons::plus(), "Open Location... (add a server by its address)", "opentool");
    detailsTool_ = tool(icons::details(), "Details: setup checklist, client log", "details");
    detailsTool_->setCheckable(true);
    gearTool_ = tool(icons::gear(), "Menu", "gear");
    gearTool_->setPopupMode(QToolButton::InstantPopup);
    ch->addWidget(openTool_);
    ch->addWidget(detailsTool_);
    ch->addWidget(gearTool_);
    right_->setCornerWidget(corner, Qt::TopRightCorner);
    connect(openTool_, &QToolButton::clicked, this, &PagerWindow::showAddServer);
    connect(detailsTool_, &QToolButton::toggled, this, [this](bool on) { setDetailsVisible(on); });

    // ---- Home ----
    home_ = new QWidget;
    home_->setObjectName("homepane");
    home_->setAttribute(Qt::WA_StyledBackground, true);
    auto* hv = new QVBoxLayout(home_);
    hv->setContentsMargins(0, 0, 0, 0);
    hv->setSpacing(0);
    auto row = [this](const QPixmap& icon, QWidget* body) {
        auto* w = new QWidget(home_);
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(8, 8, 8, 8);
        h->setSpacing(10);
        auto* il = new QLabel(w);
        il->setPixmap(icon);
        il->setFixedSize(icon.size());
        h->addWidget(il, 0, Qt::AlignTop);
        h->addWidget(body, 1);
        return w;
    };
    // account status: "Not signed in | Sign in · Play as guest" while signed out, else "Name | Logout" / "Playing as a guest |
    // Sign in". Signing in itself happens in the small Sign in window (buildSignInDialog), as in stock BYOND.
    auto avatarLabel = [](QWidget* parent) {
        auto* av = new QLabel(parent);
        av->setObjectName("accountavatar");
        av->setPixmap(icons::avatar(16));
        av->setFixedSize(16, 16);
        return av;
    };
    pages_ = new QStackedWidget(acctBar);
    auto* sp = new QWidget;
    auto* sh = new QHBoxLayout(sp);
    sh->setContentsMargins(0, 0, 0, 0);
    sh->setSpacing(6);
    auto* nsl = new QLabel("<b>Not signed in</b>", sp);
    nsl->setObjectName("notsignedin");
    rowSignIn_ = link("Sign in", sp, "signinlink");
    rowGuest_ = link("Play as guest", sp, "guest");
    sh->addWidget(nsl);
    sh->addWidget(new QLabel("|", sp));
    sh->addWidget(rowSignIn_);
    sh->addWidget(new QLabel("\xc2\xb7", sp));
    sh->addWidget(rowGuest_);
    sh->insertWidget(0, avatarLabel(sp));
    sh->addStretch(1);
    pages_->addWidget(sp);
    connect(rowSignIn_, &QPushButton::clicked, this, [this] { showSignIn(); });
    connect(rowGuest_, &QPushButton::clicked, this, [this] {
        if (busy_) return;
        guest_ = true;
        held_ = false;
        emit guestRequested();
        choosePage();
    });
    auto* mp = new QWidget;
    auto* mh = new QHBoxLayout(mp);
    mh->setContentsMargins(0, 0, 0, 0);
    mh->setSpacing(6);
    who_ = new QLabel(mp);
    who_->setObjectName("who");
    auto* bar2 = new QLabel("|", mp);
    acctBtn_ = link("", mp, "account");
    mh->addWidget(who_);
    mh->addWidget(bar2);
    mh->addWidget(acctBtn_);
    mh->insertWidget(0, avatarLabel(mp));
    mh->addStretch(1);
    pages_->addWidget(mp);
    connect(acctBtn_, &QPushButton::clicked, this, [this] {
        if (cfg_.account.empty())
            showSignIn();  // a guest stays a guest unless the Sign in window succeeds
        else
            emit signOutRequested();
    });
    acctLay->addWidget(pages_);

    auto* findBody = new QLabel(home_);
    findBody->setTextFormat(Qt::RichText);
    findBody->setText("Find games to play!<br>" + a("saved", "Saved servers") + " | " +
                      a("open", "Open location\xe2\x80\xa6"));
    connect(findBody, &QLabel::linkActivated, this, [this](const QString& href) {
        if (href == "saved")
            marks_->setFocus();
        else if (href == "open")
            showAddServer();
    });
    hv->addWidget(row(icons::find(32), findBody));
    hv->addWidget(sep(home_));
    recent_ = new QLabel(home_);
    recent_->setObjectName("recent");
    recent_->setTextFormat(Qt::RichText);
    connect(recent_, &QLabel::linkActivated, this, [this](const QString& href) {
        const bool replay = href.startsWith("replay:");
        const QString addr = href.mid(href.indexOf(':') + 1);
        for (int i = 0; i < int(sorted_.size()); i++) {
            if (Q(sorted_[size_t(i)].addr) != addr) continue;
            marks_->setCurrentRow(i);
            if (replay)
                joinRow(i);
            else
                openGameTab();
            return;
        }
    });
    hv->addWidget(row(icons::recent(32), recent_));
    hv->addWidget(sep(home_));
    status_ = new QLabel(home_);
    status_->setObjectName("status");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setContentsMargins(10, 8, 10, 8);
    status_->hide();
    hv->addWidget(status_);
    hv->addStretch(1);
    right_->addTab(home_, icons::home(), "Home");
    for (auto side : {QTabBar::LeftSide, QTabBar::RightSide})
        if (QWidget* b = right_->tabBar()->tabButton(0, side)) {
            b->hide();
            right_->tabBar()->setTabButton(0, side, nullptr);
        }

    // ---- the selected game's tab (opened by a click; closable) ----
    game_ = new QWidget(this);
    game_->setObjectName("gamepane");
    game_->setAttribute(Qt::WA_StyledBackground, true);
    auto* gv = new QVBoxLayout(game_);
    gv->setContentsMargins(14, 14, 14, 14);
    gv->setSpacing(8);
    auto* gh = new QHBoxLayout;
    gameIcon_ = new QLabel(game_);
    gameIcon_->setFixedSize(48, 48);
    auto* gnv = new QVBoxLayout;
    gameName_ = new QLabel(game_);
    gameName_->setObjectName("gamename");
    gameName_->setTextFormat(Qt::PlainText);
    gameName_->setStyleSheet("font-size:18px; font-weight:bold;");
    gameAddr_ = new QLabel(game_);
    gameAddr_->setObjectName("gameaddr");
    gameAddr_->setTextFormat(Qt::PlainText);
    gameAddr_->setWordWrap(true);
    gameAddr_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    gameAddr_->setStyleSheet(QString("color:%1;").arg(kDim));
    gnv->addWidget(gameName_);
    gnv->addWidget(gameAddr_);
    gh->addWidget(gameIcon_, 0, Qt::AlignTop);
    gh->addLayout(gnv, 1);
    gv->addLayout(gh);
    gameWhy_ = new QLabel(game_);
    gameWhy_->setObjectName("gamewhy");
    gameWhy_->setWordWrap(true);
    gameWhy_->setTextFormat(Qt::PlainText);
    gameWhy_->setStyleSheet(QString("color:%1;").arg(kBad));
    gv->addWidget(gameWhy_);
    gameNote_ = new QLabel(game_);
    gameNote_->setWordWrap(true);
    gameNote_->setTextFormat(Qt::PlainText);
    gameNote_->setStyleSheet(QString("color:%1;").arg(kDim));
    gv->addWidget(gameNote_);
    auto* gb = new QHBoxLayout;
    gameJoin_ = pill("Join", game_, "join");
    gameJoin_->setMinimumWidth(100);
    // Download resources: fetch the whole resource list into the shared cache without joining (the client's --prefetch
    // pass, which never enters the world; see net/session.h holdEntry).
    gameWarm_ = pill("Download resources", game_, "warm");
    gameWarm_->setToolTip(
        "Download this game's files now, so the first join starts complete. You do not join the game.");
    gameRemove_ = link("Remove server", game_, "remove");
    gb->addWidget(gameJoin_);
    gb->addSpacing(8);
    gb->addWidget(gameWarm_);
    gb->addSpacing(12);
    gb->addWidget(gameRemove_);
    gb->addStretch(1);
    gv->addLayout(gb);
    gv->addStretch(1);
    game_->hide();
    connect(gameJoin_, &QPushButton::clicked, this, [this] { joinRow(marks_->currentRow()); });
    connect(gameWarm_, &QPushButton::clicked, this, [this] { warmRow(marks_->currentRow()); });
    connect(gameRemove_, &QPushButton::clicked, this, [this] {
        int r = marks_->currentRow();
        if (r >= 0 && r < int(sorted_.size())) emit serverRemoved(Q(sorted_[size_t(r)].addr));
    });
    connect(right_, &QTabWidget::tabCloseRequested, this, [this](int i) {
        QWidget* w = right_->widget(i);
        if (w == details_)
            setDetailsVisible(false);
        else if (w == game_)
            closeTab(right_, game_, home_);
    });

    // ---- the gear menu: the BYOND items Fugue does ----
    gear_ = new QMenu(this);
    gear_->setObjectName("gearmenu");
    auto act = [this](const QString& text, const char* name, const QKeySequence& ks = {}, bool enabled = true) {
        QAction* x = gear_->addAction(text);
        x->setObjectName(name);
        if (!ks.isEmpty()) {
            x->setShortcut(ks);
            x->setShortcutContext(Qt::WindowShortcut);
        }
        x->setEnabled(enabled);
        addAction(x);  // shortcuts work with the menu closed
        return x;
    };
    connect(act("About Fugue...", "about"), &QAction::triggered, this, [this] {
        QMessageBox::about(this, "About Fugue",
                           QString("Fugue %1 \xe2\x80\x94 a native client and pager for BYOND games.\n"
                                   "Not made by or affiliated with BYOND.")
                               .arg(fugue::kVersion));
    });
    gear_->addSeparator();
    connect(act("Open Location...", "openloc", QKeySequence("Ctrl+O")), &QAction::triggered, this,
            &PagerWindow::showAddServer);
    connect(act("Logout", "logout", QKeySequence("Ctrl+L")), &QAction::triggered, this, [this] {
        // A guest's item reads "Sign in...": it opens the Sign in window and changes nothing until that succeeds. Signed in, Logout
        // goes through the caller (it also forgets a remembered guest choice), which then shows the Sign in window.
        if (cfg_.account.empty()) {
            showSignIn();
            return;
        }
        guest_ = false;
        emit signOutRequested();
    });
    gear_->addSeparator();
    connect(act("Preferences...", "prefs", QKeySequence("Ctrl+P")), &QAction::triggered, this,
            &PagerWindow::showPreferences);
    connect(act("Show Resources", "resources"), &QAction::triggered, this, [this] { showTab(SetupTab); });
    QAction* top = act("Stay on Top", "ontop");
    top->setCheckable(true);
    connect(top, &QAction::toggled, this, [this](bool on) {
        const bool vis = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, on);
        if (vis) show();
    });
    gear_->addSeparator();
    connect(act("Exit", "exit", QKeySequence("Ctrl+Q")), &QAction::triggered, this, &PagerWindow::requestQuit);
    gearTool_->setMenu(gear_);

    // ================= Details (the technical half; a right-pane tab, closed by default) =================
    details_ = new QWidget(this);
    auto* dl = new QVBoxLayout(details_);
    dl->setContentsMargins(4, 4, 4, 4);
    tabs_ = new QTabWidget(details_);
    dl->addWidget(tabs_);
    details_->hide();

    // ---- Setup: chips, account name, config, cache, checklist, the selected server's technical blocker ----
    auto* sw = new QWidget;
    auto* sl = new QVBoxLayout(sw);
    auto* chips = new QHBoxLayout;
    keyChip_ = new QLabel(sw);
    identChip_ = new QLabel(sw);
    clientChip_ = new QLabel(sw);
    chips->addWidget(keyChip_);
    chips->addWidget(identChip_);
    chips->addWidget(clientChip_);
    chips->addStretch(1);
    sl->addLayout(chips);
    auto* sf = new QFormLayout;
    acctEdit_ = new QLineEdit(sw);
    acctEdit_->setPlaceholderText("BYOND key (empty = play as a guest)");
    apply_ = pill("Apply", sw);
    auto* acr = new QHBoxLayout;
    acr->addWidget(acctEdit_, 1);
    acr->addWidget(apply_);
    sf->addRow("Account", acr);
    cfgPathLabel_ = new QLabel(sw);
    cfgPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cfgPathLabel_->setWordWrap(true);
    sf->addRow("Config", cfgPathLabel_);
    cacheLabel_ = new QLabel(sw);
    cacheLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cacheLabel_->setWordWrap(true);
    sf->addRow("Resources", cacheLabel_);
    sl->addLayout(sf);
    joinNote_ = new QLabel(sw);
    joinNote_->setWordWrap(true);
    joinNote_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sl->addWidget(joinNote_);
    sl->addWidget(new QLabel("Readiness checklist", sw));
    checklist_ = new QPlainTextEdit(sw);
    checklist_->setObjectName("checklist");
    checklist_->setReadOnly(true);
    checklist_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    checklist_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    sl->addWidget(checklist_, 1);
    tabs_->addTab(sw, "Setup");
    connect(apply_, &QPushButton::clicked, this, [this] { emit accountApplied(acctEdit_->text().trimmed()); });

    // ---- Log ----
    log_ = new QPlainTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    tabs_->addTab(log_, "Log");

    buildSignInDialog();
    rebuildRecent();
    choosePage();
}

// ---------------------------------------------------------------- state

void PagerWindow::setState(const account::Config& cfg, const Status& st) {
    cfg_ = cfg;
    st_ = st;
    haveState_ = true;
    sorted_ = cfg.sorted();
    if (!cfg.account.empty()) guest_ = false;
    if (cfg.account.empty()) {
        who_->setText("Playing as a guest");
        acctBtn_->setText("Sign in");
    } else {
        who_->setText("<b>" + Q(cfg.account).toHtmlEscaped() + "</b>");
        acctBtn_->setText("Logout");
    }
    auto chip = [](QLabel* l, const QString& text, const char* color) {
        l->setText(text);
        l->setStyleSheet(QString("color:%1; font-weight:600;").arg(color));
    };
    if (st.keyTxtErr)
        chip(keyChip_, "key.txt: missing", kBad);
    else if (cfg.account.empty())
        chip(keyChip_, "key.txt: not needed", kDim);
    else if (st.hasKey())
        chip(keyChip_, "key.txt: ok", kOk);
    else
        chip(keyChip_, "key.txt: no block", kBad);
    if (cfg.account.empty())
        chip(identChip_, "identity: not needed", kDim);
    else if (st.hasIdentity)
        chip(identChip_, "identity: ok", kOk);
    else
        chip(identChip_, "identity: none", kBad);
    if (st.clientErr)
        chip(clientChip_, "client: missing", kBad);
    else
        chip(clientChip_, "client: ok", kOk);
    acctEdit_->setText(Q(cfg.account));
    std::string text;
    for (auto& l : windowChecklist(st)) text += l + "\n";
    checklist_->setPlainText(Q(text));
    std::string e;
    cfgPathLabel_->setText(Q(account::configPath(&e)));
    CacheStats cs = scanRscCache(rscDir());
    cacheLabel_->setText(
        QString("%1  (%2 files, %3 MiB)").arg(Q(rscDir())).arg(cs.files).arg(double(cs.bytes) / (1 << 20), 0, 'f', 1));
    rebuildServers();
    rebuildRecent();
    choosePage();
    updatePrefs();
}

void PagerWindow::choosePage() {
    const bool signedIn = haveState_ && !cfg_.account.empty() && st_.hasIdentity && !held_;
    const bool guest = haveState_ && cfg_.account.empty() && guest_;
    const Page p = !busy_ && (signedIn || guest) ? ServersPage : SignInPage;
    // a QStackedWidget sizes to its LARGEST page: let only the shown one count, so the account row stays one line tall
    for (int i = 0; i < pages_->count(); i++)
        pages_->widget(i)->setSizePolicy(QSizePolicy::Preferred,
                                         i == int(p) ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    pages_->setCurrentIndex(int(p));
    pages_->adjustSize();
    updateMenu();
    updateJoin();
    updateGameTab();
    updatePrefs();
}

PagerWindow::Page PagerWindow::page() const { return Page(pages_->currentIndex()); }

// ---------------------------------------------------------------- the Sign in window

void PagerWindow::buildSignInDialog() {
    signIn_ = new QDialog(this);
    signIn_->setObjectName("signindialog");
    signIn_->setWindowTitle("Sign in");
    signIn_->setWindowModality(Qt::WindowModal);
    auto* v = new QVBoxLayout(signIn_);
    v->setSpacing(8);
    auto* top = new QHBoxLayout;
    auto* mark = new QLabel(signIn_);
    // Fugue's icon, set on the QApplication by app/launcher.cpp.
    mark->setPixmap(QApplication::windowIcon().pixmap(40, 40));
    mark->setFixedSize(40, 40);
    top->addWidget(mark, 0, Qt::AlignTop);
    auto* intro = new QLabel(
        "<b>Sign in with your BYOND account</b><br>"
        "<span style=\"color:#9a9a9a;\">You only sign in once on each computer.</span>",
        signIn_);
    top->addWidget(intro, 1, Qt::AlignVCenter);
    v->addLayout(top);
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(6);
    userEdit_ = new QComboBox(signIn_);
    userEdit_->setObjectName("username");
    userEdit_->setEditable(true);
    userEdit_->setInsertPolicy(QComboBox::NoInsert);
    userEdit_->lineEdit()->setPlaceholderText("BYOND username");
    userEdit_->setMinimumWidth(200);
    passEdit_ = new QLineEdit(signIn_);
    passEdit_->setObjectName("password");
    passEdit_->setPlaceholderText("Password");
    passEdit_->setEchoMode(QLineEdit::Password);
    // The stock dialog's two links. They open the player's browser; Fugue itself sends nothing.
    auto ext = [this](const char* name, const QString& href, const QString& text) {
        auto* l = new QLabel(a(href, text), signIn_);
        l->setObjectName(name);
        l->setTextFormat(Qt::RichText);
        l->setOpenExternalLinks(true);
        return l;
    };
    g->addWidget(new QLabel("Account:", signIn_), 0, 0);
    g->addWidget(userEdit_, 0, 1);
    g->addWidget(ext("getaccount", "https://secure.byond.com/Join", "Get an account"), 0, 2);
    g->addWidget(new QLabel("Password:", signIn_), 1, 0);
    g->addWidget(passEdit_, 1, 1);
    g->addWidget(ext("forgotpassword", "https://www.byond.com/members/?command=forgot_password", "Forgot password?"), 1,
                 2);
    g->setColumnStretch(1, 1);
    v->addLayout(g);
    signInNote_ = new QLabel(signIn_);
    signInNote_->setObjectName("signinnote");
    signInNote_->setWordWrap(true);
    signInNote_->setTextFormat(Qt::PlainText);
    signInNote_->setStyleSheet(QString("color:%1;").arg(kDim));
    v->addWidget(signInNote_);
    autoBox_ = new QCheckBox("Sign in automatically when Fugue starts", signIn_);
    autoBox_->setObjectName("autosignin");
    autoBox_->setChecked(autoSignIn_);
    v->addWidget(autoBox_);
    connect(autoBox_, &QCheckBox::toggled, this, [this](bool on) {
        if (on == autoSignIn_) return;
        setAutoSignIn(on);
        emit autoSignInChanged(on);
    });
    signInMsg_ = new QLabel(signIn_);
    signInMsg_->setObjectName("signinmsg");
    signInMsg_->setWordWrap(true);
    signInMsg_->setTextFormat(Qt::PlainText);
    signInMsg_->hide();
    v->addWidget(signInMsg_);
    auto* bh = new QHBoxLayout;
    guestBtn_ = link("Play as guest", signIn_, "dlgguest");
    signInBtn_ = pill("Sign in", signIn_, "signin");
    signInBtn_->setDefault(true);
    signInBtn_->setAutoDefault(true);
    cancelBtn_ = pill("Cancel", signIn_, "signincancel");
    cancelBtn_->setAutoDefault(false);
    bh->addWidget(guestBtn_);
    bh->addStretch(1);
    bh->addWidget(signInBtn_);
    bh->addWidget(cancelBtn_);
    v->addLayout(bh);
    v->setSizeConstraint(QLayout::SetMinimumSize);  // a wrapped note must never be clipped
    signIn_->setMinimumWidth(440);
    connect(signInBtn_, &QPushButton::clicked, this, &PagerWindow::submitDialog);
    connect(passEdit_, &QLineEdit::returnPressed, this, &PagerWindow::submitDialog);
    connect(userEdit_->lineEdit(), &QLineEdit::returnPressed, this, [this] {
        if (passEdit_->text().isEmpty() && !hasSavedSignIn(userEdit_->currentText().trimmed()))
            passEdit_->setFocus();
        else
            submitDialog();
    });
    connect(userEdit_, &QComboBox::currentTextChanged, this, [this] { updateSignInNote(); });
    connect(cancelBtn_, &QPushButton::clicked, signIn_, &QDialog::reject);
    connect(signIn_, &QDialog::rejected, this, [this] { passEdit_->clear(); });  // never kept past the window
    connect(guestBtn_, &QPushButton::clicked, this, [this] {
        if (busy_) return;
        passEdit_->clear();
        guest_ = true;
        held_ = false;
        signIn_->accept();
        emit guestRequested();
        choosePage();
    });
}

// Does this computer already hold a working sign-in for `account` (its identity file, and the key.txt block the join check
// wants)? Then the Sign in window needs no password: the same rule model.cpp check() applies to the saved account.
bool PagerWindow::hasSavedSignIn(const QString& account) const {
    if (account.trimmed().isEmpty()) return false;
    // Only a real ~/.fugue/identity-<ckey>: an explicit $FUGUE_IDENTITY answers for ANY name, and must not sign in as one.
    const std::string ck = account::ckey(account.trimmed().toStdString());
    const auto names = savedSignInNames();
    if (std::find(names.begin(), names.end(), ck) == names.end()) return false;
    account::Config c = cfg_;
    c.account = account.trimmed().toStdString();
    const Status s = check(c);
    return s.hasIdentity && !s.keyTxtErr;
}

void PagerWindow::updateSignInNote() {
    const QString u = userEdit_->currentText().trimmed();
    if (hasSavedSignIn(u))
        signInNote_->setText("This computer already has a sign-in for " + u +
                             ": no password needed. Type it only to sign in again.");
    else
        signInNote_->setText("Your password goes to BYOND once, to fetch this computer's sign-in, and is never saved.");
}

void PagerWindow::submitDialog() {
    if (busy_) return;
    const QString u = userEdit_->currentText().trimmed(), p = passEdit_->text();
    if (u.isEmpty()) {
        setSignInError("Enter your BYOND username and password.");
        return;
    }
    if (p.isEmpty()) {
        if (hasSavedSignIn(u)) {  // sign in once: this computer's identity for u is the sign-in
            held_ = false;
            guest_ = false;
            signInMsg_->hide();
            signIn_->accept();
            emit savedSignInChosen(u);
            choosePage();
            return;
        }
        setSignInError("Enter your BYOND username and password.");
        return;
    }
    emit signInRequested(u, p);
}

void PagerWindow::showSignIn(const QString& message) {
    right_->setCurrentWidget(home_);
    // The account list: this computer's saved sign-ins (pager/cache.h), the saved account first with its own spelling.
    const QString keep = userEdit_->currentText().trimmed();
    QStringList names;
    if (!cfg_.account.empty()) names << Q(cfg_.account);
    for (auto& n : savedSignInNames()) {
        bool dup = false;
        for (auto& have : names) dup |= Q(account::ckey(have.toStdString())) == Q(n);
        if (!dup) names << Q(n);
    }
    {
        const QSignalBlocker hold(userEdit_);
        userEdit_->clear();
        userEdit_->addItems(names);
        userEdit_->setEditText(!keep.isEmpty() ? keep : names.isEmpty() ? QString() : names.first());
    }
    updateSignInNote();
    if (!message.isEmpty())
        setSignInError(message);
    else if (!cfg_.account.empty() && haveState_ && !st_.hasIdentity &&
             userEdit_->currentText().trimmed() == Q(cfg_.account)) {
        // An account is saved but this box has no identity for it (e.g. a fresh machine, or after Clear cache): its password.
        signInMsg_->setText("Sign in again to play as " + Q(cfg_.account) + ".");
        signInMsg_->setStyleSheet(QString("color:%1;").arg(kDim));
        signInMsg_->show();
    } else
        signInMsg_->hide();
    signIn_->show();
    signIn_->raise();
    (userEdit_->currentText().isEmpty() || hasSavedSignIn(userEdit_->currentText()) ? static_cast<QWidget*>(userEdit_)
                                                                                    : passEdit_)
        ->setFocus();
}
QDialog* PagerWindow::signInDialog() const { return signIn_; }
bool PagerWindow::signInVisible() const { return signIn_ && signIn_->isVisible(); }

void PagerWindow::setGuestChosen(bool guest) {
    guest_ = guest;
    choosePage();
}

void PagerWindow::setAutoSignIn(bool on) {
    autoSignIn_ = on;
    for (QCheckBox* b : {autoBox_, prefsAuto_}) {
        if (!b) continue;
        const QSignalBlocker hold(b);
        b->setChecked(on);
    }
}
bool PagerWindow::autoSignInChecked() const { return autoSignIn_; }

void PagerWindow::holdSignIn() {
    held_ = true;
    choosePage();
    showSignIn();
}

void PagerWindow::setSigningIn(bool busy) {
    busy_ = busy;
    for (QWidget* w : std::initializer_list<QWidget*>{userEdit_, passEdit_, signInBtn_, guestBtn_, cancelBtn_,
                                                      rowGuest_, rowSignIn_})
        w->setEnabled(!busy);
    signInBtn_->setText(busy ? "Signing in\xe2\x80\xa6" : "Sign in");
    if (busy)
        signInMsg_->hide();
    else
        passEdit_->clear();  // the password is never kept past one attempt
    choosePage();
}

void PagerWindow::closeSignIn() {
    held_ = false;
    signInMsg_->hide();
    signIn_->accept();
    choosePage();
}

void PagerWindow::setSignInError(const QString& plain) {
    signInMsg_->setText(plain);
    signInMsg_->setStyleSheet(QString("color:%1;").arg(kBad));
    signInMsg_->setVisible(!plain.isEmpty());
    // The window was closed (Esc / the title bar) while the attempt ran: bring it back so the error is seen.
    if (!plain.isEmpty() && !signInVisible()) {
        signIn_->show();
        signIn_->raise();
    }
}

void PagerWindow::submitSignIn(const QString& user, const QString& password) {
    if (!signInVisible()) showSignIn();
    userEdit_->setEditText(user);
    passEdit_->setText(password);
    signInBtn_->click();
}

// ---------------------------------------------------------------- the game list

void PagerWindow::rebuildServers() {
    int keep = marks_->currentRow();
    const QSignalBlocker hold(marks_);
    marks_->clear();
    for (int i = 0; i < int(sorted_.size()); i++) {
        const auto& s = sorted_[size_t(i)];
        auto* it = new QListWidgetItem(Q(s.name),
                                       marks_);  // ONE label per row: the name (an added server's name is its address)
        it->setData(kAddr, Q(s.addr));
        it->setData(Qt::DecorationRole, icons::gameIcon(Q(s.name), 16));
        auto b = plainBlocker(st_, s);
        it->setData(kBlocked, bool(b));
        it->setToolTip(Q(s.addr) + (b ? "\n" + Q(*b) : QString()));
    }
    if (!sorted_.empty()) marks_->setCurrentRow(keep >= 0 && keep < int(sorted_.size()) ? keep : 0);
    applyFilter();
    updateJoin();
    updateGameTab();
}

void PagerWindow::rebuildRecent() {
    std::vector<const account::Server*> played;
    for (auto& s : sorted_)
        if (!s.lastPlayed.isZero()) played.push_back(&s);
    std::stable_sort(played.begin(), played.end(),
                     [](auto* x, auto* y) { return x->lastPlayed.sec > y->lastPlayed.sec; });
    if (played.size() > 3) played.resize(3);
    QString t;
    for (auto* s : played) {
        if (!t.isEmpty()) t += "<br>";
        t += a("open:" + Q(s->addr), Q(s->name)) + " | " + a("replay:" + Q(s->addr), "Replay");
    }
    if (t.isEmpty()) t = QString("<span style=\"color:%1;\">Games you play show up here.</span>").arg(kDim);
    recent_->setText(t);
}

void PagerWindow::setFilter(const QString& text) {
    filter_->setText(text);
    applyFilter();
}
void PagerWindow::applyFilter() {
    const QString f = filter_->text().trimmed();
    for (int i = 0; i < marks_->count(); i++) {
        QListWidgetItem* it = marks_->item(i);
        const bool match = f.isEmpty() || it->text().contains(f, Qt::CaseInsensitive) ||
                           it->data(kAddr).toString().contains(f, Qt::CaseInsensitive);
        it->setHidden(!match);
    }
}
int PagerWindow::visibleGameCount() const {
    int n = 0;
    for (int i = 0; i < marks_->count(); i++) n += !marks_->item(i)->isHidden();
    return n;
}

void PagerWindow::warmRow(int r) {
    if (warming_) {
        emit warmCancelRequested();
        return;
    }  // the button reads "Cancel download" while one runs
    if (r < 0 || r >= int(sorted_.size()) || running_ || blocker(st_, sorted_[size_t(r)])) return;
    if (page() != ServersPage) {
        setStatus("Sign in, or choose Play as guest, to download a game's resources.", true);
        return;
    }
    marks_->setCurrentRow(r);
    emit warmRequested(Q(sorted_[size_t(r)].addr), sorted_[size_t(r)].hub, Q(cfg_.account));
}
bool PagerWindow::warmEnabled() const { return gameWarm_->isEnabled(); }
QString PagerWindow::gameNoteText() const { return gameNote_->isHidden() ? QString() : gameNote_->text(); }

void PagerWindow::setWarming(bool on, const QString& progress, bool error) {
    warming_ = on;
    warmNote_ = progress;
    setStatus(progress, error);
    updateJoin();
    updateGameTab();
    updatePrefs();
}

// Join stays usable during a download: the caller stops the download first (what arrived is kept), and the game's own join
// fetches the rest, what it draws first.
void PagerWindow::joinRow(int r) {
    if (r < 0 || r >= int(sorted_.size()) || running_ || blocker(st_, sorted_[size_t(r)])) return;
    if (page() != ServersPage) {  // the account row is still asking: sign in, or say "guest", first
        setStatus("Sign in, or choose Play as guest, to join a game.", true);
        return;
    }
    marks_->setCurrentRow(r);
    emit joinRequested(Q(sorted_[size_t(r)].addr), sorted_[size_t(r)].hub, Q(cfg_.account));
}

void PagerWindow::showAddServer() {
    addRow_->show();
    addEdit_->setFocus();
}

QString PagerWindow::submitAddServer(const QString& text) {
    std::string err;
    auto ad = parseTarget(text.toStdString(), &err);
    if (!ad) {
        addRow_->show();
        addMsg_->setText(plainTargetError(err));
        addMsg_->show();
        return addMsg_->text();
    }
    addEdit_->clear();
    addMsg_->hide();
    addRow_->hide();
    emit serverAdded(Q(*ad));
    for (int i = 0; i < int(sorted_.size()); i++)
        if (QString::fromStdString(sorted_[size_t(i)].addr).compare(Q(*ad), Qt::CaseInsensitive) == 0)
            marks_->setCurrentRow(i);
    return {};
}

void PagerWindow::updateJoin() {
    const int r = marks_->currentRow();
    const bool valid = r >= 0 && r < int(sorted_.size());
    gameJoin_->setEnabled(valid && !running_ && !blocker(st_, sorted_[size_t(r)]) && page() == ServersPage);
    gameWarm_->setText(warming_ ? "Cancel download" : "Download resources");
    gameWarm_->setToolTip(
        warming_ ? "Stop the download. What arrived is kept, and Join still works."
                 : "Download this game's files now, so the first join starts complete. You do not join the game.");
    gameWarm_->setEnabled(warming_ || gameJoin_->isEnabled());
    if (!valid) {
        joinNote_->setText(sorted_.empty() ? "No saved servers." : "Select a server.");
        joinNote_->setStyleSheet(QString("color:%1;").arg(kDim));
        if (sorted_.empty() && !running_) setStatus("Add a server to play: press Open... and type its address.");
        return;
    }
    if (auto b = blocker(st_, sorted_[size_t(r)])) {
        joinNote_->setText("\xe2\x9a\xa0 " + Q(windowWording(*b)));
        joinNote_->setStyleSheet(QString("color:%1;").arg(kBad));
    } else {
        joinNote_->setText("Ready to join " + Q(sorted_[size_t(r)].addr) +
                           (cfg_.account.empty() ? " as a guest." : " as " + Q(cfg_.account) + "."));
        joinNote_->setStyleSheet(QString("color:%1;").arg(kOk));
    }
    if (status_->text().startsWith("Add a server to play")) setStatus({});
}

void PagerWindow::updateGameTab() {
    const int r = marks_->currentRow();
    if (r < 0 || r >= int(sorted_.size())) {
        closeTab(right_, game_, home_);
        return;
    }
    const auto& s = sorted_[size_t(r)];
    const QString title = Q(s.name);
    gameIcon_->setPixmap(icons::gameIcon(title, 48));
    gameName_->setText(title);
    gameAddr_->setText(Q(s.addr));
    auto b = plainBlocker(st_, s);
    gameWhy_->setText(b ? Q(*b) : QString());
    gameWhy_->setVisible(bool(b));
    QString note;
    if (running_)
        note = runNote_.isEmpty() ? QString("The game is running.") : runNote_;
    else if (warming_)
        note = warmNote_;
    else if (!b && page() != ServersPage)
        note = "Sign in, or choose Play as guest, to join.";
    else if (!b)
        note = Q(plainWarmState(s));
    gameNote_->setText(note);
    gameNote_->setVisible(!note.isEmpty());
    const int ix = right_->indexOf(game_);
    if (ix >= 0) {
        right_->setTabText(ix, title.size() > 28 ? title.left(27) + "\xe2\x80\xa6" : title);
        right_->setTabIcon(ix, QIcon(icons::gameIcon(title, 16)));
    }
}

void PagerWindow::openGameTab() {
    int ix = right_->indexOf(game_);
    if (ix < 0) ix = right_->insertTab(1, game_, QString());
    updateGameTab();
    if (right_->indexOf(game_) >= 0) right_->setCurrentWidget(game_);
}
QString PagerWindow::gameTabTitle() const {
    const int ix = right_->indexOf(game_);
    return ix < 0 ? QString() : right_->tabText(ix);
}

void PagerWindow::setStatus(const QString& plain, bool error) {
    status_->setText(plain);
    status_->setStyleSheet(QString("color:%1;").arg(error ? kBad : kDim));
    status_->setVisible(!plain.isEmpty());
}

void PagerWindow::reload(const CheckOptions& opt) {
    std::string e;
    auto cfg = account::loadConfig(&e);
    if (!cfg) {
        cfg = account::Config{};
        cfg->servers = account::defaultServers();
    }
    setState(*cfg, check(*cfg, opt));
}

// ---------------------------------------------------------------- menu / preferences / clear cache

void PagerWindow::updateMenu() {
    QAction* lo = menuAction("logout");
    if (!lo) return;
    lo->setText(cfg_.account.empty() ? "Sign in..." : "Logout");
}
QMenu* PagerWindow::gearMenu() const { return gear_; }
QAction* PagerWindow::menuAction(const QString& name) const {
    for (QAction* x : gear_->actions())
        if (x->objectName() == name) return x;
    return nullptr;
}

void PagerWindow::showPreferences() {
    if (!prefs_) {
        prefs_ = new QDialog(this);
        prefs_->setObjectName("preferences");
        prefs_->setWindowTitle("Preferences");
        auto* v = new QVBoxLayout(prefs_);
        v->setSpacing(10);
        auto* h0 = new QLabel("Account", prefs_);
        h0->setStyleSheet("font-weight:bold;");
        v->addWidget(h0);
        prefsAccount_ = new QLabel(prefs_);
        prefsAccount_->setObjectName("prefsaccount");
        prefsAccount_->setWordWrap(true);
        prefsAccount_->setTextFormat(Qt::PlainText);
        v->addWidget(prefsAccount_);
        prefsAuto_ = new QCheckBox("Sign in automatically when Fugue starts", prefs_);
        prefsAuto_->setObjectName("prefsautosignin");
        prefsAuto_->setChecked(autoSignIn_);
        v->addWidget(prefsAuto_);
        connect(prefsAuto_, &QCheckBox::toggled, this, [this](bool on) {
            if (on == autoSignIn_) return;
            setAutoSignIn(on);
            emit autoSignInChanged(on);
        });
        v->addWidget(sep(prefs_));
        auto* h1 = new QLabel("Window", prefs_);
        h1->setStyleSheet("font-weight:bold;");
        v->addWidget(h1);
        prefsDetails_ = new QCheckBox("Show technical details (setup checklist, client log)", prefs_);
        prefsDetails_->setObjectName("prefsdetails");
        v->addWidget(prefsDetails_);
        connect(prefsDetails_, &QCheckBox::toggled, this, [this](bool on) { setDetailsVisible(on); });
        v->addWidget(sep(prefs_));
        auto* h2 = new QLabel("Cache", prefs_);
        h2->setStyleSheet("font-weight:bold;");
        v->addWidget(h2);
        prefsCache_ = new QLabel(prefs_);
        prefsCache_->setObjectName("prefscache");
        prefsCache_->setWordWrap(true);
        v->addWidget(prefsCache_);
        auto* ch = new QHBoxLayout;
        prefsClear_ = pill("Clear cache...", prefs_, "clearcache");
        ch->addWidget(prefsClear_);
        ch->addStretch(1);
        v->addLayout(ch);
        connect(prefsClear_, &QPushButton::clicked, this, &PagerWindow::requestClearCache);
        v->addStretch(1);
        auto* bh = new QHBoxLayout;
        bh->addStretch(1);
        auto* close = pill("Close", prefs_, "prefsclose");
        bh->addWidget(close);
        v->addLayout(bh);
        connect(close, &QPushButton::clicked, prefs_, &QDialog::close);
        prefs_->resize(480, 380);
    }
    updatePrefs();
    prefs_->show();
    prefs_->raise();
}
QDialog* PagerWindow::preferencesDialog() const { return prefs_; }

void PagerWindow::updatePrefs() {
    if (!prefs_) return;
    {
        const QSignalBlocker hold(prefsDetails_);
        prefsDetails_->setChecked(detailsVisible());
    }
    // Fugue never keeps the password: the identity is the sign-in, and it does not expire on its own.
    QString acct;
    if (!cfg_.account.empty() && st_.hasIdentity)
        acct = "Signed in as " + Q(cfg_.account) + ". This computer keeps a sign-in for " + Q(cfg_.account) +
               ", so you sign in only once here. Fugue never saves your password. Logout keeps the sign-in; Clear "
               "cache removes it.";
    else if (!cfg_.account.empty())
        acct = "Not signed in on this computer as " + Q(cfg_.account) +
               ". Sign in once and Fugue keeps a sign-in for this computer; it never saves your password.";
    else if (guest_)
        acct =
            "Playing as a guest. Sign in once and Fugue keeps a sign-in for this computer; it never saves your "
            "password.";
    else
        acct = "Not signed in. Sign in once and Fugue keeps a sign-in for this computer; it never saves your password.";
    prefsAccount_->setText(acct);
    CachePlan p = planClearCache();
    int signIns = 0;
    uint64_t rsc = 0, rscFiles = 0;
    for (auto& i : p.items) {
        if (i.path == rscDir()) {
            rsc = i.bytes;
            rscFiles = i.files;
        } else if (i.what.rfind("Saved sign-in", 0) == 0)
            signIns++;
    }
    QString t =
        p.empty()
            ? QString("Nothing is cached.")
            : QString(
                  "Cached: %1 saved sign-in%2, %3 of game resources (%4 file%5). Your servers and settings are kept.")
                  .arg(signIns)
                  .arg(signIns == 1 ? "" : "s")
                  .arg(Q(humanBytes(rsc)))
                  .arg(rscFiles)
                  .arg(rscFiles == 1 ? "" : "s");
    prefsCache_->setText(t);
    prefsClear_->setEnabled(!running_ && !warming_ && !p.empty());
    prefsClear_->setToolTip(running_   ? "Close the game first."
                            : warming_ ? "Wait for the download to finish."
                                       : QString());
}

void PagerWindow::setConfirmHook(std::function<bool(const QString&, const QString&)> hook) {
    confirm_ = std::move(hook);
}

void PagerWindow::requestClearCache() {
    if (running_) {
        setStatus("Close the game before clearing the cache.", true);
        return;
    }
    if (warming_) {
        setStatus("Wait for the resource download to finish before clearing the cache.", true);
        return;
    }
    CachePlan p = planClearCache();
    const QString title = "Clear cache";
    const QString text = "This removes:\n\n" + Q(describeClearCache(p));
    bool yes;
    if (confirm_)
        yes = confirm_(title, text);
    else {
        QMessageBox box(QMessageBox::Warning, title,
                        p.empty() ? "There is nothing cached to remove." : "Remove these cached files?",
                        p.empty() ? QMessageBox::Ok : (QMessageBox::Yes | QMessageBox::No), this);
        box.setInformativeText(text);
        if (!p.empty()) box.setDefaultButton(QMessageBox::No);
        yes = box.exec() == QMessageBox::Yes;
    }
    if (yes && !p.empty()) emit clearCacheRequested();
    updatePrefs();
}

// ---------------------------------------------------------------- log / details

void PagerWindow::appendLog(const QString& text) {
    QString t = text;
    if (t.endsWith('\n')) t.chop(1);
    log_->appendPlainText(t);
}
QString PagerWindow::logText() const { return log_->toPlainText(); }
void PagerWindow::setClientRunning(bool running, const QString& note) {
    running_ = running;
    runNote_ = note;
    if (running) setStatus(note.isEmpty() ? QString("The game is running.") : note);
    updateJoin();
    updateGameTab();
    updatePrefs();
    if (!running && parked_) showPager();
    updateTray();
}

bool PagerWindow::trayAvailable() const { return trayHook_ ? trayHook_() : QSystemTrayIcon::isSystemTrayAvailable(); }
void PagerWindow::setTrayHook(std::function<bool()> available) { trayHook_ = std::move(available); }
void PagerWindow::setNoticeHook(std::function<void(const QString&, const QString&)> hook) { notice_ = std::move(hook); }
QSystemTrayIcon* PagerWindow::trayIcon() const { return tray_; }
bool PagerWindow::hiddenToTray() const { return parked_ && isHidden(); }

void PagerWindow::updateTray() {
    if (!running_ || !trayAvailable()) {
        if (tray_) tray_->hide();
        return;
    }
    if (!tray_) {
        tray_ = new QSystemTrayIcon(windowIcon(), this);
        auto* m = new QMenu(this);
        m->setObjectName("traymenu");
        QAction* open = m->addAction("Open Fugue");
        open->setObjectName("trayopen");
        connect(open, &QAction::triggered, this, &PagerWindow::showPager);
        m->addSeparator();
        QAction* exit = m->addAction("Exit");
        exit->setObjectName("trayexit");
        connect(exit, &QAction::triggered, this, &PagerWindow::requestQuit);
        tray_->setContextMenu(m);
        connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
            if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) showPager();
        });
        trayWatch_ = new QTimer(this);
        trayWatch_->setInterval(2000);
        connect(trayWatch_, &QTimer::timeout, this, [this] {
            if (hiddenToTray() && !trayAvailable())
                showPager();  // the panel went away: nothing would bring the window back
        });
    }
    tray_->setToolTip("Fugue: a game is running");
    tray_->show();
}

void PagerWindow::showPager() {
    // A Wayland client cannot un-minimize its window; mapping it again brings it back.
    if (parked_ && QGuiApplication::platformName().startsWith("wayland")) hide();
    parked_ = false;
    if (trayWatch_) trayWatch_->stop();
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
}

void PagerWindow::closeEvent(QCloseEvent* e) {
    if (quitting_ || qApp->isSavingSession()) {
        e->accept();
        return;
    }
    switch (closeAction(running_, trayAvailable())) {
        case CloseAction::Quit:
            e->accept();
            requestQuit();
            return;
        case CloseAction::HideToTray:
            e->ignore();
            parked_ = true;
            updateTray();
            hide();
            trayWatch_->start();
            if (!trayNoticed_) {
                trayNoticed_ = true;
                const QString title = "Fugue is still running";
                const QString text = "The game keeps running. Click the Fugue icon to open the pager again.";
                if (notice_)
                    notice_(title, text);
                else
                    tray_->showMessage(title, text);
            }
            return;
        case CloseAction::Minimize:
            e->ignore();
            parked_ = true;
            showMinimized();
            return;
    }
}

bool PagerWindow::confirmQuit() {
    if (!running_) return true;
    const QString title = "Exit Fugue";
    const QString text = "A game is still running. Exiting Fugue closes the game.";
    if (confirm_) return confirm_(title, text);
    QMessageBox box(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, this);
    box.setInformativeText("Exit anyway?");
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

void PagerWindow::requestQuit() {
    if (!confirmQuit()) return;
    quitting_ = true;
    emit quitRequested();
}

// Every quit reaches the application as QEvent::Quit, which closes the windows. One this window did not ask for comes from
// the platform: on macOS, Cmd+Q or the Dock's Quit, which ask like Exit; elsewhere the session ending, which nothing may veto.
bool PagerWindow::eventFilter(QObject* o, QEvent* e) {
    if (o != qApp || e->type() != QEvent::Quit) return QWidget::eventFilter(o, e);
#ifdef Q_OS_MACOS
    if (!quitting_ && !confirmQuit()) {
        e->ignore();
        return true;
    }
#endif
    quitting_ = true;
    QTimer::singleShot(0, this, [this] { quitting_ = false; });  // still running: another window refused the quit
    return false;
}

void PagerWindow::setDetailsVisible(bool on) {
    const int ix = right_->indexOf(details_);
    if (on && ix < 0) right_->addTab(details_, icons::details(), "Details");
    if (on) right_->setCurrentWidget(details_);
    if (!on && ix >= 0) closeTab(right_, details_, home_);
    {
        const QSignalBlocker hold(detailsTool_);
        detailsTool_->setChecked(on);
    }
    updatePrefs();
}
bool PagerWindow::detailsVisible() const { return right_->indexOf(details_) >= 0; }
void PagerWindow::showTab(Tab t) {
    setDetailsVisible(true);
    tabs_->setCurrentIndex(int(t));
}

QString PagerWindow::joinBlockerText() const {
    int r = marks_->currentRow();
    if (r < 0 || r >= int(sorted_.size())) return {};
    auto b = plainBlocker(st_, sorted_[size_t(r)]);
    return b ? Q(*b) : QString();
}
bool PagerWindow::joinEnabled() const { return gameJoin_->isEnabled(); }
int PagerWindow::bookmarkCount() const { return marks_->count(); }
void PagerWindow::selectBookmark(int row) { marks_->setCurrentRow(row); }
QString PagerWindow::checklistText() const { return checklist_->toPlainText(); }
QString PagerWindow::statusText() const { return status_->text(); }
QString PagerWindow::accountText() const { return who_->text(); }
QString PagerWindow::signInErrorText() const { return signInMsg_->isHidden() ? QString() : signInMsg_->text(); }

}  // namespace fugue::pager

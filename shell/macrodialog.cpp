// The layout is described in shell/macrodialog.h; the macro file format is in skin/usermacros.h.
#include "shell/macrodialog.h"
#include <algorithm>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "shell/focusfilter.h"
#include "shell/fugueicon.h"
#include "shell/paste.h"
#include "skin/usermacros.h"

namespace fugue::shell {

namespace {
QLabel* note(const QString& t, QWidget* parent) {
    auto* l = new QLabel(t, parent);
    l->setWordWrap(true);
    return l;
}

// Find Key: the next key combination pressed becomes the macro's key.
class FindKeyDialog : public QDialog {
public:
    explicit FindKeyDialog(QWidget* parent) : QDialog(parent) {
        setWindowTitle("Find Key");
        setProperty("fugueZone", "other");
        auto* v = new QVBoxLayout(this);
        v->addWidget(note("Type a key or ALT+CTRL+SHIFT combination to make into a macro.", this));
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        bb->button(QDialogButtonBox::Cancel)->setFocusPolicy(Qt::NoFocus);  // Space and Return must reach keyPressEvent
        connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
        v->addWidget(bb);
        setFocusPolicy(Qt::StrongFocus);
    }
    QString spec;

protected:
    bool event(QEvent* e) override {  // Tab is consumed by the focus chain before keyPressEvent
        if (e->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent*>(e));
            return true;
        }
        return QDialog::event(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        std::string s = macroSpecFor(e);
        if (s.empty() || s.back() == '+') return;  // a bare modifier: keep waiting for the key
        spec = QString::fromStdString(s);
        accept();
    }
};
}  // namespace

MacroEditDialog::MacroEditDialog(const QString& title, const skin::MacroEntry* e, QWidget* parent) : QDialog(parent) {
    setWindowTitle(title);
    setProperty("fugueZone", "other");
    auto* g = new QGridLayout(this);
    key_ = wirePaste(new QLineEdit(this));
    key_->setObjectName("key");
    auto* find = new QPushButton("Find Key...", this);
    g->addWidget(new QLabel("Key/action:", this), 0, 0);
    g->addWidget(key_, 0, 1);
    g->addWidget(find, 0, 2);
    auto* mods = new QHBoxLayout;
    alt_ = new QCheckBox("ALT", this);
    ctrl_ = new QCheckBox("CTRL", this);
    shift_ = new QCheckBox("SHIFT", this);
    mods->addWidget(alt_);
    mods->addWidget(ctrl_);
    mods->addWidget(shift_);
    mods->addStretch();
    g->addWidget(new QLabel("Use with:", this), 1, 0);
    g->addLayout(mods, 1, 1, 1, 2);
    auto* flags = new QHBoxLayout;
    rep_ = new QCheckBox("Repeat", this);
    up_ = new QCheckBox("When released", this);
    flags->addWidget(rep_);
    flags->addWidget(up_);
    flags->addStretch();
    g->addLayout(flags, 2, 1, 1, 2);
    cmd_ = wirePaste(new QLineEdit(this));
    cmd_->setObjectName("command");
    g->addWidget(new QLabel("Command:", this), 3, 0);
    g->addWidget(cmd_, 3, 1, 1, 2);
    g->addWidget(
        note(
            "The command will be executed when the corresponding key is pressed. To have it placed on the command line "
            "instead, append the continuation tag: \\...  For example: \"jump\" executes the command, while \"jump "
            "\\...\" "
            "places it on the command line.",
            this),
        4, 0, 1, 3);
    g->addWidget(note("To delete a macro, enter a blank key or command.", this), 5, 0, 1, 3);
    g->addWidget(
        note("An ID is helpful (but not required) if you wish to interact with this macro at runtime via winset.",
             this),
        6, 0, 1, 3);
    id_ = wirePaste(new QLineEdit(this));
    id_->setObjectName("id");
    g->addWidget(new QLabel("ID:", this), 7, 0);
    g->addWidget(id_, 7, 1, 1, 2);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    g->addWidget(bb, 8, 0, 1, 3);
    connect(find, &QPushButton::clicked, this, [this] {
        auto* fk = new FindKeyDialog(this);
        fk->setAttribute(Qt::WA_DeleteOnClose);
        connect(fk, &QDialog::accepted, this, [this, fk] { setSpec(fk->spec); });
        fk->open();
    });
    if (e) {
        setSpec(QString::fromStdString(e->spec));
        cmd_->setText(QString::fromStdString(e->command));
        id_->setText(QString::fromStdString(e->id));
    }
}

void MacroEditDialog::setSpec(const QString& spec) {
    skin::MacroKey k;
    bool rep = false;
    if (!skin::parseMacroName(spec.toStdString(), k, rep)) {
        key_->clear();
        return;
    }
    key_->setText(QString::fromStdString(k.key));
    alt_->setChecked(k.alt);
    ctrl_->setChecked(k.ctrl);
    shift_->setChecked(k.shift);
    rep_->setChecked(rep);
    up_->setChecked(k.up);
}

QString MacroEditDialog::spec() const {
    QString key = key_->text().trimmed();
    if (key.isEmpty()) return QString();
    QString s;
    if (ctrl_->isChecked()) s += "CTRL+";
    if (shift_->isChecked()) s += "SHIFT+";
    if (alt_->isChecked()) s += "ALT+";
    s += key.toUpper();
    if (rep_->isChecked()) s += "+REP";
    if (up_->isChecked()) s += "+UP";
    return s;
}
QString MacroEditDialog::command() const { return cmd_->text().trimmed(); }
QString MacroEditDialog::id() const { return id_->text().trimmed(); }

QCheckBox* MacroEditDialog::box(const char* name) const {
    QString n(name);
    return n == "alt"     ? alt_
           : n == "ctrl"  ? ctrl_
           : n == "shift" ? shift_
           : n == "rep"   ? rep_
           : n == "up"    ? up_
                          : nullptr;
}

MacroDialog::MacroDialog(const skin::MacroSet& global, const skin::MacroSet& game, bool gameAvailable, QWidget* parent)
    : QDialog(parent), global_(global), game_(game) {
    setWindowTitle("Macros");
    setWindowIcon(fugueIcon());  // the client's own icon, not the world's
    setProperty("fugueZone", "other");
    auto* v = new QVBoxLayout(this);
    v->addWidget(note("These macros will apply if the corresponding actions are not in use.", this));
    v->addWidget(
        note("Client menu: Ctrl+M by default. To replace it, bind .clientmenu to a key combination "
             "(without UP or REP). This client-only binding takes priority over game keys, even in browsers. "
             "Game-specific client-menu bindings replace global ones.",
             this));
    scope_ = new QCheckBox("Apply only to this game", this);
    scope_->setEnabled(gameAvailable);
    scope_->setChecked(gameAvailable);  // checked by default when the world has a per-game list
    v->addWidget(scope_);
    list_ = new QTreeWidget(this);
    list_->setColumnCount(2);
    list_->setHeaderLabels({"Action", "Command"});
    list_->setRootIsDecorated(false);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->header()->setStretchLastSection(true);
    list_->setColumnWidth(0, 100);
    list_->viewport()->installEventFilter(this);
    v->addWidget(list_, 1);
    v->addWidget(new QLabel("Double-click on the list to create or edit a macro.", this));
    auto* nb = new QPushButton("New Macro...", this);
    auto* row = new QHBoxLayout;
    row->addWidget(nb);
    row->addStretch();
    v->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(bb);
    connect(nb, &QPushButton::clicked, this, [this] { openEditor(-1); });
    connect(scope_, &QCheckBox::toggled, this, [this] { refill(); });
    connect(list_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* it) { openEditor(list_->indexOfTopLevelItem(it)); });
    resize(300, 340);
    refill();
}

bool MacroDialog::gameScope() const { return scope_->isEnabled() && scope_->isChecked(); }
skin::MacroSet& MacroDialog::current() { return gameScope() ? game_ : global_; }

void MacroDialog::refill() {
    list_->clear();
    std::vector<const skin::MacroEntry*> es;
    for (const skin::MacroEntry& e : current().entries) es.push_back(&e);
    std::stable_sort(es.begin(), es.end(), [](auto* a, auto* b) { return a->spec < b->spec; });  // by key
    for (const skin::MacroEntry* e : es)
        new QTreeWidgetItem(list_, {QString::fromStdString(e->spec), QString::fromStdString(e->command)});
}

bool MacroDialog::commit(const QString& oldSpec, const QString& spec, const QString& command, const QString& id) {
    bool ok = skin::editUserMacro(current(), oldSpec.toStdString(), spec.toStdString(), command.toStdString(),
                                  id.toStdString());
    refill();
    return ok;
}

MacroEditDialog* MacroDialog::openEditor(int row) {
    const skin::MacroEntry* e = nullptr;
    QString old;
    if (row >= 0 && row < list_->topLevelItemCount()) {
        old = list_->topLevelItem(row)->text(0);
        skin::MacroKey k;
        bool rep = false;
        if (skin::parseMacroName(old.toStdString(), k, rep))
            for (const skin::MacroEntry& x : current().entries)
                if (x.key == k && x.rep == rep) e = &x;
    }
    auto* ed = new MacroEditDialog(e ? "Edit Macro" : "New Macro", e, this);
    ed->setAttribute(Qt::WA_DeleteOnClose);
    connect(ed, &QDialog::accepted, this, [this, ed, old] { commit(old, ed->spec(), ed->command(), ed->id()); });
    ed->open();
    return ed;
}

bool MacroDialog::eventFilter(QObject* o, QEvent* e) {
    // Double-click edits the row under the cursor (itemDoubleClicked) or creates a macro on empty space.
    if (o == list_->viewport() && e->type() == QEvent::MouseButtonDblClick &&
        !list_->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint())) {
        openEditor(-1);
        return true;
    }
    return QDialog::eventFilter(o, e);
}

void addClientMenuItems(QMenu* client, const std::function<void(const QString&)>& run,
                        const std::function<void(bool)>& metrics, bool metricsVisible) {
    // Stock order and separators, with unimplemented items left out.
    auto item = [&](const char* label, const char* cmd) {
        QAction* a = client->addAction(label);
        a->setData(QString(cmd));
        QObject::connect(a, &QAction::triggered, client, [run, c = QString(cmd)] { run(c); });
    };
    item("&Screenshot...", ".screenshot");
    item("&Reconnect", ".reconnect");
    client->addSeparator();
    item("&Macros...", ".macros");
    if (metrics) {
        QAction* action = client->addAction("&Performance metrics");
        action->setObjectName("mapPerformanceAction");
        action->setCheckable(true);
        action->setChecked(metricsVisible);
        QObject::connect(action, &QAction::toggled, client, [metrics](bool visible) { metrics(visible); });
    }
    client->addSeparator();
    item("&Quit", ".quit");
}

void addSystemMenuItems(QMenu* m, const std::function<void(const QString&)>& run,
                        const std::function<void(bool)>& metrics, bool metricsVisible) {
    QAction* opt = m->addAction("Options && Messages");  // "&&" shows one '&', so there is no accelerator
    opt->setData(QString(".options"));
    QObject::connect(opt, &QAction::triggered, m, [run] { run(".options"); });
    addClientMenuItems(m->addMenu("&Client"), run, metrics, metricsVisible);
}

OptionsWindow::OptionsWindow(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Options and Messages");
    // Stock DreamSeeker keeps BYOND's own icon on this window while the main window shows the skin's. Fugue's own
    // icon is set explicitly, because the parent chain would otherwise hand it the world's.
    setWindowIcon(fugueIcon());
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    bar_ = new QMenuBar(this);
    addClientMenuItems(bar_->addMenu("&Client"), [this](const QString& c) { emit command(c); });
    v->setMenuBar(bar_);
    auto* hint = note("Client > Macros... edits your own key bindings for this game, or for every game.", this);
    hint->setContentsMargins(8, 8, 8, 8);
    v->addWidget(hint);
    v->addStretch();
    resize(400, 100);
}

}  // namespace fugue::shell

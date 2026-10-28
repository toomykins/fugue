#include "shell/statpanelwidget.h"
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fugue::shell {

namespace {
constexpr int kIconTextGap = 3;   // gap between the icon cell and the text
constexpr int kIconRole = Qt::UserRole + 1;
// The Stat() value column is [icon cell][gap][text], top-aligned, as stock DreamSeeker lays out an atom row. It is
// painted here rather than through Qt's DecorationRole, so the click can be counted in the cell exactly (iconAt).
class StatValueDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& ix) const override {
        QImage icon = ix.data(kIconRole).value<QImage>();
        if (icon.isNull()) { QStyledItemDelegate::paint(p, opt, ix); return; }
        p->save();
        p->drawImage(opt.rect.topLeft(), icon);
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, ix);
        o.rect.setLeft(opt.rect.left() + icon.width() + kIconTextGap);
        o.displayAlignment = Qt::AlignLeft | Qt::AlignTop;
        o.widget ? o.widget->style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget) : QStyledItemDelegate::paint(p, o, ix);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& ix) const override {
        QSize s = QStyledItemDelegate::sizeHint(opt, ix);
        QImage icon = ix.data(kIconRole).value<QImage>();
        if (icon.isNull()) return s;
        // Stock: an icon row is the cell plus one pixel (33 px pitch at 32), and a taller text line wins.
        return QSize(s.width() + icon.width() + kIconTextGap, std::max(s.height(), icon.height() + 1));
    }
};
}  // namespace

StatPanelWidget::StatPanelWidget(QWidget* parent) : QWidget(parent) {
    setProperty("fugueZone", "info");   // shell/focusfilter.h: keys over the stat panel go to macro routing
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    tabs_ = new QTabBar(this);
    tabs_->setObjectName("statTabs");
    tabs_->setExpanding(false);
    tabs_->setDrawBase(true);
    tabs_->setUsesScrollButtons(true);
    tabs_->setFocusPolicy(Qt::NoFocus);
    table_ = new QTableWidget(this);
    table_->setObjectName("statRows");
    table_->setProperty("fugueZone", "info");
    table_->horizontalHeader()->hide();
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setItemDelegateForColumn(1, new StatValueDelegate(table_));
    l->addWidget(tabs_);
    l->addWidget(table_, 1);
    // QTabBar moves its own selection on a click, but the model decides which tab is selected. A change the model did
    // not make becomes a request to the server, and is undone until the model's `selected` says otherwise.
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) {
        if (setting_ || i < 0 || i == view_.selected) return;
        setting_ = true;
        tabs_->setCurrentIndex(view_.selected);
        setting_ = false;
        emit tabRequested(i);
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        if (view_.verbTab) {
            if (auto* it = table_->item(r, c); it && !it->text().isEmpty()) emit verbClicked(it->text());
        }
    });
    // Atom rows are read from the raw mouse events: cellClicked is left-button only and has no double-click position.
    // Qt delivers a double click as press, release, double-click, release; one report per release plus one for the
    // double-click event gives stock's click, dblclick, click sequence.
    table_->viewport()->installEventFilter(this);
}

void StatPanelWidget::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange && table_) rebuildRows();   // clickable rows carry their own copy of the font
}

bool StatPanelWidget::eventFilter(QObject* o, QEvent* e) {
    if (o != table_->viewport() || view_.verbTab) return QWidget::eventFilter(o, e);
    auto t = e->type();
    if (t != QEvent::MouseButtonPress && t != QEvent::MouseButtonDblClick && t != QEvent::MouseButtonRelease) return QWidget::eventFilter(o, e);
    auto* me = static_cast<QMouseEvent*>(e);
    QModelIndex ix = table_->indexAt(me->position().toPoint());
    int row = (ix.isValid() && ix.column() == 1 && ix.row() < view_.rows.size() && view_.rows[ix.row()].clickable) ? ix.row() : -1;
    QString button = me->button() == Qt::LeftButton ? "left" : me->button() == Qt::MiddleButton ? "middle" : QString();
    if (t == QEvent::MouseButtonPress) { pressRow_ = row; return QWidget::eventFilter(o, e); }
    Qt::KeyboardModifiers km = me->modifiers();
    int mods = (km & Qt::ControlModifier ? ModCtrl : 0) | (km & Qt::ShiftModifier ? ModShift : 0) | (km & Qt::AltModifier ? ModAlt : 0);
    int iconX = 0, iconY = 0;
    bool icon = row >= 0 && iconAt(row, me->position().toPoint(), iconX, iconY);
    if (t == QEvent::MouseButtonDblClick) {
        pressRow_ = row;
        if (row >= 0 && !button.isEmpty()) emit rowClicked(row, controlId_, button, mods, true, icon, iconX, iconY);
    } else {
        if (row >= 0 && row == pressRow_ && !button.isEmpty()) emit rowClicked(row, controlId_, button, mods, false, icon, iconX, iconY);
        pressRow_ = -1;
    }
    return QWidget::eventFilter(o, e);
}

// icon-x = x - the cell's left + 1 and icon-y = the cell's bottom - y + 1, in cell units whatever the icon's size,
// unclamped. A row with nothing in its cell sends neither.
bool StatPanelWidget::iconAt(int row, const QPoint& p, int& ix, int& iy) const {
    if (row < 0 || row >= view_.rows.size() || view_.rows[row].icon.isNull()) return false;
    QRect cell = table_->visualRect(table_->model()->index(row, 1));
    ix = p.x() - cell.left() + 1;
    iy = cell.top() + view_.rows[row].icon.height() - p.y();
    return true;
}

void StatPanelWidget::setPanelFont(const QFont& f) {
    setFont(f);
    tabs_->setFont(f);
    table_->setFont(f);
    rebuildRows();
}

void StatPanelWidget::setTextColors(const QColor& prefix, const QColor& suffix) {
    if (prefix == prefix_ && suffix == suffix_) return;
    prefix_ = prefix;
    suffix_ = suffix;
    rebuildRows();
}

void StatPanelWidget::setView(const StatView& v) {
    if (v == view_ && tabs_->count() == v.tabs.size()) return;
    setting_ = true;
    bool tabsChanged = v.tabs != view_.tabs;
    bool rowsChanged = !(v.rows == view_.rows) || v.verbTab != view_.verbTab;
    view_ = v;
    if (tabsChanged) {
        while (tabs_->count()) tabs_->removeTab(0);
        for (const QString& t : v.tabs) tabs_->addTab(t);
    }
    if (v.selected >= 0 && v.selected < tabs_->count()) tabs_->setCurrentIndex(v.selected);
    if (rowsChanged) rebuildRows();
    setting_ = false;
}

void StatPanelWidget::rebuildRows() {
    table_->clearContents();
    if (view_.verbTab) {
        const int cols = 4;   // verbs flow row-major across a uniform four-column grid, as in stock
        table_->setColumnCount(cols);
        table_->setRowCount((view_.rows.size() + cols - 1) / cols);
        for (int i = 0; i < view_.rows.size(); i++) {
            auto* it = new QTableWidgetItem(view_.rows[i].name);
            it->setFlags(Qt::ItemIsEnabled);
            table_->setItem(i / cols, i % cols, it);
        }
        for (int c = 0; c < cols; c++) table_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    } else {
        table_->setColumnCount(2);
        table_->setRowCount(view_.rows.size());
        for (int i = 0; i < view_.rows.size(); i++) {
            auto* a = new QTableWidgetItem(view_.rows[i].name);
            auto* b = new QTableWidgetItem(view_.rows[i].text);
            Qt::ItemFlags f = Qt::ItemIsEnabled;
            a->setFlags(f); b->setFlags(f);
            a->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);   // stock: a label sits at the top of an icon row
            if (prefix_.isValid()) a->setForeground(prefix_);
            if (suffix_.isValid()) b->setForeground(suffix_);
            if (!view_.rows[i].icon.isNull()) b->setData(kIconRole, view_.rows[i].icon);
            if (view_.rows[i].clickable) {
                QFont ft = table_->font();   // the INFO control's font (an item's own font() is the application's)
                ft.setUnderline(true);
                b->setFont(ft);
                b->setToolTip("Click to click this atom");
            }
            table_->setItem(i, 0, a);
            table_->setItem(i, 1, b);
        }
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    table_->resizeRowsToContents();
}

}  // namespace fugue::shell

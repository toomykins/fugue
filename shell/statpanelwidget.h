// StatPanelWidget: the INFO control as Qt widgets. A tab strip (the world's Stat() tabs, then one tab per verb
// category) sits over a row list. The data is model/statpanel.h.
//   * A Stat() tab shows `name  value` rows. A row naming an atom (turf, obj or mob) is clickable on its value, and
//     the session thread builds the Click (model::statRowClickBody). Other rows are inert, as in stock DreamSeeker.
//     Left and middle clicks report. A double click reports click, double-click, click, as stock does.
//     A right click reports nothing.
//   * An atom row draws the atom's icon at the head of its value cell, with the name after it (render/scene.h
//     statRowIcon has the rules). A click on such a row reports icon-x and icon-y within that cell.
//   * A verb-category tab shows its verbs in a row-major grid of four columns; clicking one runs it.
//   * Clicking a tab asks the server for that tab (0x006b). The server sends only the selected tab's rows, so the
//     strip follows the model's `selected`, which selectStatTab moves when the request is made.
// Only value types (StatView) cross from the session thread; this widget never sees the model.
#pragma once
#include <QColor>
#include <QFont>
#include <QImage>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QWidget>

class QTabBar;
class QTableWidget;
class QTableWidgetItem;
class QEvent;

namespace fugue::shell {

struct StatRowView {
    QString name;        // the label (Stat rows) / the verb name (verb tabs)
    QString text;        // the value column (Stat rows)
    bool clickable = false;   // an atom row the server can be told about
    QImage icon;              // the atom's icon cell (render::statRowIcon), null = none drawn (no icon, or not loaded yet)
    bool operator==(const StatRowView& o) const { return name == o.name && text == o.text && clickable == o.clickable && icon == o.icon; }
};
struct StatView {
    QStringList tabs;    // the whole strip
    int selected = 0;
    bool verbTab = false;   // the selected tab is a verb category: rows are verbs
    QList<StatRowView> rows;
    bool operator==(const StatView& o) const { return tabs == o.tabs && selected == o.selected && verbTab == o.verbTab && rows == o.rows; }
};

class StatPanelWidget : public QWidget {
    Q_OBJECT
public:
    explicit StatPanelWidget(QWidget* parent = nullptr);
    void setView(const StatView& v);
    const StatView& view() const { return view_; }
    QTabBar* tabBar() const { return tabs_; }
    QTableWidget* table() const { return table_; }
    // The INFO control's prefix-color and suffix-color: a Stat() row's name and its value. Invalid = the control's text colour.
    // The INFO control's font, on the tab strip and the rows too (a style sheet change resets their inherited font).
    void setPanelFont(const QFont& f);
    void setTextColors(const QColor& prefix, const QColor& suffix);
    void setControlId(const QString& id) { controlId_ = id; }   // "<window>.<control>", named by a row click
    QString controlId() const { return controlId_; }
    enum RowMods { ModCtrl = 1, ModShift = 2, ModAlt = 4 };
signals:
    void tabRequested(int index);     // a click on a tab other than the selected one
    // An atom row's value: button "left" or "middle", mods = RowMods bits, dbl = the second press of a double click.
    // icon = the row draws an icon. iconX and iconY locate the click in its cell as stock counts it: x+1 from the cell's
    // left, y+1 from its bottom, unclamped.
    void rowClicked(int row, const QString& control, const QString& button, int mods, bool dbl, bool icon, int iconX, int iconY);
    void verbClicked(const QString& name);
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void changeEvent(QEvent* e) override;   // a new font (the INFO control's) rebuilds the rows
private:
    void rebuildRows();
    bool iconAt(int row, const QPoint& viewportPos, int& ix, int& iy) const;
    QString controlId_;
    int pressRow_ = -1;
    QTabBar* tabs_ = nullptr;
    QTableWidget* table_ = nullptr;
    StatView view_;
    QColor prefix_, suffix_;
    bool setting_ = false;
};

}  // namespace fugue::shell
Q_DECLARE_METATYPE(fugue::shell::StatView)

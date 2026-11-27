// StatPanelWidget: the INFO control as Qt widgets. A tab strip (the world's Stat() tabs, then one tab per verb
// category) sits over a row list. The data is model/statpanel.h.
//   * A Stat() tab shows `name  value` rows. A row naming an atom (turf, obj or mob) is clickable on its value, and
//     the session thread builds the Click (model::statRowClickBody). Other rows are inert, as in stock DreamSeeker.
//     Left and middle clicks report. A double click reports click, double-click, click, as stock does.
//     A right click reports nothing.
//   * A verb-category tab shows its verbs in a row-major grid of four columns; clicking one runs it.
//   * Clicking a tab asks the server for that tab (StatTab). The server sends only the selected tab's rows, so the
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
    QString name;            // the label (Stat rows) / the verb name (verb tabs)
    QString text;            // the value column (Stat rows)
    bool clickable = false;  // an atom row the server can be told about
    QImage thumbnail;        // implicitly shared immutable worker pixels, never a model pointer
    quint64 identity = 0;
    bool operator==(const StatRowView& o) const {
        return name == o.name && text == o.text && clickable == o.clickable && identity == o.identity &&
               thumbnail.cacheKey() == o.thumbnail.cacheKey();
    }
};
struct StatView {
    QStringList tabs;  // the whole strip
    int selected = 0;
    bool verbTab = false;  // the selected tab is a verb category: rows are verbs
    QList<StatRowView> rows;
    quint64 generation = 0;  // publication token, excluded from content comparison
    QByteArray wireTab;
    bool operator==(const StatView& o) const {
        return tabs == o.tabs && selected == o.selected && verbTab == o.verbTab && rows == o.rows &&
               wireTab == o.wireTab;
    }
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
    void setControlId(const QString& id) { controlId_ = id; }  // "<window>.<control>", named by a row click
    QString controlId() const { return controlId_; }
    enum RowMods { ModCtrl = 1, ModShift = 2, ModAlt = 4 };
signals:
    void tabRequested(int index);  // a click on a tab other than the selected one
    // An atom row's value: button "left" or "middle", mods = RowMods bits, dbl = the second press of a double click.
    void rowClicked(int row, const QString& control, const QString& button, int mods, bool dbl, quint64 generation,
                    bool icon, int iconX, int iconY);
    void verbClicked(const QString& name);
    // Pointer hover over the rows (stock 516.1689, tests/data/hover-world panel.json): every pick of the pointer, row
    // -1 for the panel's empty space, which the session turns into hover reports through the one record it keeps for
    // all the controls (shell::MapHover). An atom row is its value: the icon and the name text, not its label nor the
    // space past the text. The pointer is picked again when the panel shows another tab, not when rows change.
    void rowHovered(int row, const QString& control, int mods, quint64 generation, bool icon, int iconX, int iconY);
    // The pointer left the rows (the tab strip is not part of them). Sent after the control the pointer went to has
    // picked, as a map's leave is (MapWidget::leaveEvent).
    void hoverLeft();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void changeEvent(QEvent* e) override;  // a new font (the INFO control's) rebuilds the rows
private:
    struct Hit {
        int row = -1;  // an atom row whose value the point is on, else -1
        bool icon = false;
        int iconX = 0, iconY = 0;
    };
    Hit hitAt(QPoint viewportPos) const;
    void pickHover(QPoint viewportPos, Qt::KeyboardModifiers mods);
    void rebuildRows();
    bool hoverInside_ = false;
    bool repick_ = false;  // the tab changed: pick again once the new tab's rows are shown
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

// MacroDialog: the player's own key bindings (DreamSeeker's Client > Macros...), kept beneath the skin's macro set.
// OptionsWindow: the "Options and Messages" window that `.options` opens, reduced to the Client menu that leads there.
//
// Stock DreamSeeker 516 has no menu bar of its own on a skin-less window. Its system menu carries "Options & Messages"
// and a Client submenu holding Macros...; Fugue draws the same entries from a button at the window's top-left corner
// (SkinWindow), because the window manager owns the real caption and its system menu on Linux.
// Not implemented here, so not offered: the greyed "Also pass built-in command" box, and the Server and Help menus.
#pragma once
#include <functional>
#include <QDialog>
#include <QString>
#include "skin/macro.h"

class QCheckBox;
class QLineEdit;
class QMenu;
class QTreeWidget;
class QMenuBar;

namespace fugue::shell {

// One entry's editor. Splits a spec into key + modifier boxes and joins it back: CTRL+SHIFT+ALT+KEY[+REP][+UP].
class MacroEditDialog : public QDialog {
    Q_OBJECT
public:
    MacroEditDialog(const QString& title, const skin::MacroEntry* e, QWidget* parent = nullptr);
    QString spec() const;  // "" when the Key field is blank, which deletes the macro
    QString command() const;
    QString id() const;
    // Fills key and modifiers from a spec such as "CTRL+F8+REP".
    void setSpec(const QString& spec);
    QLineEdit* keyEdit() const { return key_; }
    QLineEdit* commandEdit() const { return cmd_; }
    QCheckBox* box(const char* name) const;  // "alt", "ctrl", "shift", "rep" or "up"
private:
    QLineEdit *key_, *cmd_, *id_;
    QCheckBox *alt_, *ctrl_, *shift_, *rep_, *up_;
};

class MacroDialog : public QDialog {
    Q_OBJECT
public:
    // gameAvailable=false greys "Apply only to this game" and shows the global list.
    MacroDialog(const skin::MacroSet& global, const skin::MacroSet& game, bool gameAvailable,
                QWidget* parent = nullptr);
    const skin::MacroSet& globalSet() const { return global_; }
    const skin::MacroSet& gameSet() const { return game_; }
    bool gameScope() const;
    QTreeWidget* list() const { return list_; }
    QCheckBox* scopeBox() const { return scope_; }
    // Applies one editor result to the list on screen. Returns false when the key names no key.
    bool commit(const QString& oldSpec, const QString& spec, const QString& command, const QString& id = QString());
    // Opens the entry editor for a row (-1 is New Macro...). Non-blocking; the result is committed when accepted.
    MacroEditDialog* openEditor(int row);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    skin::MacroSet& current();
    void refill();
    skin::MacroSet global_, game_;
    QTreeWidget* list_;
    QCheckBox* scope_;
};

// The stock Client menu, reduced to Screenshot..., Reconnect, Macros..., Quit.
void addClientMenuItems(QMenu* client, const std::function<void(const QString&)>& run,
                        const std::function<void(bool)>& metrics = {}, bool metricsVisible = false);
// The entries the game window's system menu gets: "Options & Messages" (`.options`) and the Client menu.
void addSystemMenuItems(QMenu* m, const std::function<void(const QString&)>& run,
                        const std::function<void(bool)>& metrics = {}, bool metricsVisible = false);

// The "Options and Messages" window, reduced to a menu bar with the Client menu (addClientMenuItems).
class OptionsWindow : public QDialog {
    Q_OBJECT
public:
    explicit OptionsWindow(QWidget* parent = nullptr);
    QMenuBar* menuBar() const { return bar_; }
signals:
    void command(const QString& cmd);  // routed like a skin menu item's client command
private:
    QMenuBar* bar_;
};

}  // namespace fugue::shell

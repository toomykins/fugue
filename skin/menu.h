// skin/menu.h - the menu bar a skin declares, as plain data (pure; the Qt layer is shell/skinwindow).
//
// The MAIN window's `menu = "<datum>"` names the menu set; a
//   top-level category is either an elem with no `category` whose `name` some other elem uses as its `category`
//   (a DECLARATION: it fixes the category's position and is the only elem a winset can address, vg's
//   `elem "server_menu"`), or just a `category=` value on items (byond.dmf's convention, no addressable elem);
//   `&` marks the accelerator letter, a `\t` in a name separates the label from its right-aligned shortcut text,
//   an item with an EMPTY name is a separator. A command is either a `.`-prefixed client command or an ordinary
//   command line - the caller routes it exactly like a macro's command (shell/inputrouter.h).
#pragma once
#include <string>
#include <vector>

#include "skin/dmf.h"

namespace fugue::skin {

struct MenuItem {
    std::string name;      // the .dmf elem name (what winset addresses); may be empty
    std::string label;     // display text, '&' removed
    std::string shortcut;  // text after '\t'
    int accel = -1;        // index in label of the accelerator letter
    std::string command;
    std::string group;  // radio group
    bool canCheck = false, checked = false, separator = false;
    bool disabled = false;  // the file's is-disabled (LiveSkin::snapshot() applies the live overlay)
};

struct MenuCat {
    std::string name;  // the declaring elem's name, "" when a category exists only as a `category=` value
    std::string label;
    int accel = -1;
    bool disabled = false;
    std::vector<MenuItem> items;
};

struct Menu {
    std::string datum;
    std::vector<MenuCat> cats;
    bool empty() const { return cats.empty(); }
};

// The menu the main window names (`menu = "x"`), else a menu datum called "menu", else none.
Menu menuFromFile(const File& f);

}  // namespace fugue::skin

#include "skin/menu.h"

#include <map>
#include <set>

#include "skin/layout.h"

namespace fugue::skin {
namespace {

void splitAccel(const std::string& in, std::string& label, int& accel) {
    accel = -1;
    label = in;
    size_t i = label.find('&');
    if (i != std::string::npos && i + 1 < label.size()) {
        accel = int(i);
        label = label.substr(0, i) + label.substr(i + 1);
    }
}

MenuItem itemFrom(const Elem& e) {
    MenuItem it;
    it.name = e.name;
    it.command = e.str("command", "");
    it.group = e.str("group", "");
    it.canCheck = e.boolv("can-check", false);
    it.checked = e.boolv("is-checked", false);
    it.disabled = e.boolv("is-disabled", false);
    std::string name = e.str("name", "");
    if (name.empty()) {
        it.separator = true;
        return it;
    }
    std::string lbl = name;
    size_t t = lbl.find('\t');
    if (t != std::string::npos) {
        it.shortcut = lbl.substr(t + 1);
        lbl = lbl.substr(0, t);
    }
    splitAccel(lbl, it.label, it.accel);
    return it;
}

}  // namespace

Menu menuFromFile(const File& f) {
    Menu m;
    std::string datum;
    if (const Block* mw = findMainWindow(f))
        if (const Elem* me = mw->main()) datum = me->str("menu", "");
    const Block* blk = nullptr;
    for (const Block& b : f.blocks)
        if (b.kind == "menu" && (datum.empty() ? b.name == "menu" : b.name == datum)) blk = &b;  // last wins
    if (!blk) return m;
    m.datum = blk->name;
    std::set<std::string> used;
    for (const Elem& e : blk->elems) {
        std::string c = e.str("category", "");
        if (!c.empty()) used.insert(c);
    }
    std::map<std::string, size_t> idx;
    auto catFor = [&](const std::string& val) -> size_t {
        auto it = idx.find(val);
        if (it != idx.end()) return it->second;
        MenuCat c;
        splitAccel(val, c.label, c.accel);
        m.cats.push_back(c);
        idx[val] = m.cats.size() - 1;
        return m.cats.size() - 1;
    };
    for (const Elem& e : blk->elems) {
        std::string cat = e.str("category", ""), name = e.str("name", "");
        if (cat.empty()) {
            if (used.count(name)) {  // a declaration fixes the category's position and its addressable name
                size_t i = catFor(name);
                m.cats[i].name = e.name;
                m.cats[i].disabled = e.boolv("is-disabled", false);
            }
            continue;  // an empty or bare top-level row places nothing
        }
        m.cats[catFor(cat)].items.push_back(itemFrom(e));
    }
    return m;
}

}  // namespace fugue::skin

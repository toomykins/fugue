#include "shell/skinsync.h"
#include "app/defaultskin.h"
#include "shell/focusfilter.h"
#include "shell/skinwindow.h"

#include <QStringList>

namespace fugue::shell {

void applySkinMacros(const SkinWindow& win, FocusFilter& filt) {
    const skin::MacroTable t = win.macros();
    auto it = t.sets.find(win.activeMacro().toStdString());
    if (it != t.sets.end())
        filt.setMacros(it->second);
    else if (!t.sets.empty())
        filt.setMacros(t.sets.begin()->second);
    else
        filt.setMacros(skin::MacroSet{});
}

void worldSkinLoaded(SkinWindow& win, FocusFilter& filt, bool& sized) {
    win.syncFromLive();
    if (win.live()->loadedFrom(app::kDefaultSkin)) win.markBuiltinSkin();  // a world with no skin of its own
    if (!sized) {
        win.resize(win.designSize());
        sized = true;
    }
    applySkinMacros(win, filt);
}

bool applyUserSize(SkinWindow& win, const QString& wxh, bool& sized) {
    const QStringList p = wxh.split('x');
    if (p.size() != 2) return false;
    bool okW = false, okH = false;
    int w = p[0].toInt(&okW), h = p[1].toInt(&okH);
    if (!okW || !okH || w <= 0 || h <= 0) return false;
    win.resize(w, h);
    sized = true;
    return true;
}

}  // namespace fugue::shell

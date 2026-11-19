// The pager's icons, all drawn with QPainter. The layout follows stock BYOND 516's pager, but every glyph is our own
// stand-in. The app mark is Fugue's own icon (shell/fugueicon.h) and is not drawn here.
#pragma once
#include <QIcon>
#include <QPixmap>
#include <QString>

namespace fugue::pager::icons {

QPixmap gameIcon(const QString& name,
                 int px);  // a stand-in game tile: rounded square, colour from the name, its initial
QPixmap people(int px);    // the "players" glyph next to a player count
QPixmap lock(int px);      // a server this account cannot join
QPixmap avatar(int px);    // a generic grey silhouette (the account row)
QPixmap find(int px);      // "Find games to play!" row
QPixmap recent(int px);    // recent games row
QIcon home();              // the Home tab
QIcon gear();              // the menu button
QIcon plus();              // Open Location
QIcon details();           // Details (the technical pane)
QIcon games();             // the Games tab
QIcon friends();           // not shown: the pager has no Friends tab

}  // namespace fugue::pager::icons

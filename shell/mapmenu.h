// The map's right-click menu: the atoms a right click lists, their verbs, and the picture beside each atom's name.
// What stock DreamSeeker 516.1689 shows was measured with tests/data/menu-world (README.md there).
//
// The menu has one entry per listed atom, topmost first, each with a submenu of its verbs, even when it is the only
// entry. An entry shows the atom's name (model::plainAtomName: text macros gone, '&', '<' and the rest as they are, a
// long name whole, no suffix) after a 16x16 picture; an atom with an empty name has no entry. A verb's item is its
// label alone (model::menuVerbLabel), in atomMenu's order, with no picture and no separators.
#pragma once
#include <QIcon>
#include <QImage>
#include <string>
#include <vector>

#include "model/verbmenu.h"
#include "shell/mapinput.h"

namespace fugue::shell {
// One atom of the stack under a right click, as the session lists it.
struct MapMenuEntry {
    MapPointer rendered;  // the rendered hit that names it (mapPointerStack)
    model::HitTarget hit;
    int x = 0, y = 0;     // the cell its verbs are offered for: 1-based, 0,0 for a HUD atom
    std::string name;     // plain; empty: the atom gets no entry
    std::vector<model::MenuVerb> verbs;  // atomMenu; left empty for a nameless atom
};
// The stack under p (an Open right press), topmost first: every entry of mapPointerStack that is still current and that
// a verb could be run on. A vis_contents turf from another z is left out (a verb names a turf by cell on the map's z
// only, so it would run on the turf under it).
std::vector<MapMenuEntry> mapMenuStack(const MapPointer& p, const model::World& w, uint64_t session);

// The picture stock draws beside an atom's name: its icon as the map last drew it, overlays, underlays, client images
// and vis_contents members included, in their drawing order and places, but none of colour, alpha, transform, filters
// or the atom's own pixel offset; then the whole of it scaled to fit size x size pixels, each pixel the average of the
// pixels it covers. It is drawn from the scene the right click was made on, so it shows the animation frame and the
// appearance the map showed then and does not change while the menu is open (stock 516.1689, tests/data/menu-world:
// anim, changer). Null for an atom that draws nothing (no icon). The size is in device pixels: stock's picture is 16
// pixels at 150 % display scale too.
constexpr int kMapMenuIconSize = 16;
QImage mapMenuIcon(const MapPointer& entry, int size = kMapMenuIconSize);
// The area-average scaling mapMenuIcon uses: image fitted into size x size, centred, aspect kept.
QImage mapMenuFit(const QImage& image, int size);
// The picture as a menu icon on a display of this scale: the style's 16 logical pixels square, with the picture's own
// device pixels in the middle, unscaled. Null for a null picture.
QIcon mapMenuEntryIcon(const QImage& icon, qreal devicePixelRatio);
}  // namespace fugue::shell

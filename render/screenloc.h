// screen_loc grammar -> viewport pixel placement, and line of sight.
#pragma once
#include <functional>
#include <set>
#include <string>
#include <utility>

namespace fugue::render {

struct ParsedScreenLoc {
    std::string mapCtrl;  // "name:" prefix, "" = the default map
    int x = 0, y = 0;     // bottom-left of the object, viewport px, +y UP
    int spanCols = 1, spanRows = 1;
};

// false = "draws nothing" (empty, unparseable, SCREEN_/% forms, a corner keyword with trailing text...).
bool parseScreenLoc(const std::string& loc, int cols, int rows, int tileW, int tileH, ParsedScreenLoc& out);

// The "name:" map-control prefix of a screen_loc ("" = the default map), whether or not the rest parses: a plane master's
// "name:CENTER" names the map it governs.
std::string screenLocMap(const std::string& loc);

using CellSet = std::set<std::pair<int, int>>;
// Cells in line of sight from (ex, ey) within rng; opaque(x, y) blocks sight.
CellSet visibleCells(int ex, int ey, int rng, const std::function<bool(int, int)>& opaque);

}  // namespace fugue::render

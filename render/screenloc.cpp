// See screenloc.h.
#include "render/screenloc.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <vector>

namespace fugue::render {

namespace {

struct Axis {
    int cell = 0;
    double px = 0;
    double frac = 0;
    bool useFrac = false;
    bool set = false;
    // Pixel position along this axis: the cell contributes whole tiles, the extent fraction a share of the leftover
    // extent, and the pixel offset is added last; the sum is rounded half away from zero.
    int pixels(int extentCells, int tile) const {
        double base = double((cell - 1) * tile);
        if (useFrac) base += frac * double(extentCells * tile - tile);
        return int(std::round(base + px));
    }
};

struct Keyword {
    bool setX = false, setY = false;
    int xCell = 0, yCell = 0;      // 1 = min edge, 2 = max edge, 0 = use fraction
    double xFrac = 0, yFrac = 0;
    bool terminal = false;
};

const std::map<std::string, Keyword>& keywords() {
    static const std::map<std::string, Keyword> k = {
        {"NORTH", {false, true, 0, 2, 0, 0, false}},
        {"SOUTH", {false, true, 0, 1, 0, 0, false}},
        {"WEST", {true, false, 1, 0, 0, 0, false}},
        {"EAST", {true, false, 2, 0, 0, 0, false}},
        {"NORTHEAST", {true, true, 2, 2, 0, 0, true}},
        {"SOUTHEAST", {true, true, 2, 1, 0, 0, true}},
        {"NORTHWEST", {true, true, 1, 2, 0, 0, true}},
        {"SOUTHWEST", {true, true, 1, 1, 0, 0, true}},
        {"CENTER", {true, true, 0, 0, 0.5, 0.5, false}},
        {"MIDDLE", {true, true, 0, 0, 0.5, 0.5, false}},
        {"TOP", {false, true, 0, 0, 0, 1, false}},
        {"BOTTOM", {false, true, 0, 0, 0, 0, false}},
        {"LEFT", {true, false, 0, 0, 0, 0, false}},
        {"RIGHT", {true, false, 0, 0, 1, 0, false}},
        {"TOPRIGHT", {true, true, 0, 0, 1, 1, true}},
        {"TOPLEFT", {true, true, 0, 0, 0, 1, true}},
        {"BOTTOMRIGHT", {true, true, 0, 0, 1, 0, true}},
        {"BOTTOMLEFT", {true, true, 0, 0, 0, 0, true}},
    };
    return k;
}

std::string upper(std::string s) {
    for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isWs(s[a])) a++;
    while (b > a && isWs(s[b - 1])) b--;
    return s.substr(a, b - a);
}

// The whole string must be consumed.
bool parseFloatFull(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    v = std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size() && end != s.c_str();
}

void applyKeywordAxis(Axis& a, int cellCode, double frac, int extent) {
    switch (cellCode) {
        case 1: a.cell = 1; a.useFrac = false; break;
        case 2: a.cell = extent; a.useFrac = false; break;
        default: a.cell = 1; a.frac = frac; a.useFrac = true; break;
    }
    a.set = true;
}

// parseToken: consumes ONE coordinate token. own = 0 for x, 1 for y.
bool parseToken(const std::string& s, int own, Axis* axes[2], const int tiles[2], int cols, int rows, bool& terminal) {
    terminal = false;
    Axis* cur = axes[own];
    int curTile = tiles[own];
    bool numbered = false;
    double sign = 1;
    bool cont = false;

    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == ')' || c == '(') { i++; continue; }
        if (c == '+') { sign = 1; cont = true; i++; continue; }
        if (c == '-') { sign = -1; cont = true; i++; continue; }
        if (c == ':') {
            size_t j = i + 1;
            if (j < n && (s[j] == '+' || s[j] == '-')) j++;
            size_t k = j;
            while (k < n && s[k] >= '0' && s[k] <= '9') k++;
            if (k == j) return false;
            std::string num = s.substr(i + 1, k - (i + 1));
            errno = 0;
            long v = std::strtol(num.c_str(), nullptr, 10);
            if (errno == ERANGE || v > INT_MAX || v < INT_MIN) return false;   // Range error
            cur->px += double(v);
            i = k;
            continue;
        }
        if (c == '.' || (c >= '0' && c <= '9')) {
            size_t j = i;
            while (j < n && (s[j] == '.' || (s[j] >= '0' && s[j] <= '9'))) j++;
            double v;
            if (!parseFloatFull(s.substr(i, j - i), v)) return false;
            // A bare coordinate is x if x is unwritten, else y.
            int idx = own;
            if (cont) idx = -1;
            else if (numbered) idx = 1 - own;
            else if (axes[own]->set && !axes[1 - own]->set) idx = 1 - own;
            Axis* a = cur;
            int tile = curTile;
            if (idx >= 0) { a = axes[idx]; tile = tiles[idx]; }
            double whole = std::trunc(v);
            a->cell += int(sign * whole);
            a->px += sign * (v - whole) * double(tile);
            a->set = true;
            cur = a; curTile = tile; sign = 1; cont = false; numbered = true;
            i = j;
            continue;
        }
        // keyword
        size_t j = i;
        while (j < n && std::string(" ,)+-:").find(s[j]) == std::string::npos) j++;
        auto it = keywords().find(upper(s.substr(i, j - i)));
        if (it == keywords().end()) return false;
        const Keyword& kw = it->second;
        if (kw.setX && (own == 0 || !axes[0]->set)) applyKeywordAxis(*axes[0], kw.xCell, kw.xFrac, cols);
        if (kw.setY && (own == 1 || !axes[1]->set)) applyKeywordAxis(*axes[1], kw.yCell, kw.yFrac, rows);
        if ((own == 0 && kw.setX) || (own == 1 && kw.setY)) { cur = axes[own]; curTile = tiles[own]; }
        else if (kw.setX) { cur = axes[0]; curTile = tiles[0]; }
        else if (kw.setY) { cur = axes[1]; curTile = tiles[1]; }
        if (kw.terminal) {
            // A CORNER keyword takes no modifier and ends the whole spec: anything but whitespace or parens after it
            // makes the screen_loc invalid, so it draws nothing.
            for (size_t k = j; k < n; k++) {
                char d = s[k];
                if (d != ' ' && d != '\t' && d != '(' && d != ')') return false;
            }
            terminal = true;
            return true;
        }
        sign = 1; cont = false;
        i = j;
    }
    return true;
}

struct Corner { int x, y; };

bool parseCorner(const std::string& s, int cols, int rows, int tileW, int tileH, Corner& out) {
    Axis x, y;
    Axis* axes[2] = {&x, &y};
    const int tiles[2] = {tileW, tileH};

    std::vector<std::string> toks;
    {
        size_t a = 0;
        for (;;) {
            size_t c = s.find(',', a);
            if (c == std::string::npos) { toks.push_back(s.substr(a)); break; }
            toks.push_back(s.substr(a, c - a));
            a = c + 1;
        }
    }
    if (toks.size() > 2) return false;
    bool sawTerminal = false;
    for (size_t ti = 0; ti < toks.size(); ti++) {
        if (trim(toks[ti]).empty()) continue;
        if (sawTerminal) return false;
        int own = ti > 1 ? 1 : int(ti);
        bool terminal = false;
        if (!parseToken(toks[ti], own, axes, tiles, cols, rows, terminal)) return false;
        sawTerminal = sawTerminal || terminal;
    }
    if (!x.set && !y.set) return false;
    if (!x.set) { x = Axis{}; x.cell = 1; x.frac = 0.5; x.useFrac = true; x.set = true; }
    if (!y.set) { y = Axis{}; y.cell = 1; y.frac = 0.5; y.useFrac = true; y.set = true; }
    out = Corner{x.pixels(cols, tileW), y.pixels(rows, tileH)};
    return true;
}

// splitRange: whitespace-separated fields, plus a case-insensitive "to" strictly inside.
bool splitRange(const std::string& s, std::string& lo, std::string& hi) {
    std::vector<std::string> f;
    {
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && isWs(s[i])) i++;
            size_t j = i;
            while (j < s.size() && !isWs(s[j])) j++;
            if (j > i) f.push_back(s.substr(i, j - i));
            i = j;
        }
    }
    for (size_t i = 0; i < f.size(); i++) {
        if (upper(f[i]) == "TO" && i > 0 && i + 1 < f.size()) {
            lo.clear(); hi.clear();
            for (size_t k = 0; k < i; k++) { if (k) lo += ' '; lo += f[k]; }
            for (size_t k = i + 1; k < f.size(); k++) { if (k > i + 1) hi += ' '; hi += f[k]; }
            return true;
        }
    }
    return false;
}

int spanTiles(int deltaPx, int tile) {
    if (tile <= 0) return 1;
    int n = int(std::round(double(deltaPx) / double(tile))) + 1;
    return n < 1 ? 1 : n;
}

// splitMapCtrl: a "mapname:" prefix, unless the head is a number, a keyword, or has a ',' before the ':'.
bool splitMapCtrl(const std::string& s, std::string& name, std::string& rest) {
    size_t colon = s.find(':');
    if (colon == std::string::npos || colon == 0) return false;
    size_t comma = s.find(',');
    if (comma != std::string::npos && comma < colon) return false;
    std::string head = trim(s.substr(0, colon));
    if (head.empty()) return false;
    double dummy;
    if (parseFloatFull(head, dummy)) return false;
    if (keywords().count(upper(head))) return false;
    for (char r : head) {
        if (r >= '0' && r <= '9') continue;
        if (r == '_' || r == '.' || ((r | 0x20) >= 'a' && (r | 0x20) <= 'z')) continue;
        // Brackets are allowed in a map name: vg names its camera console's map "camera_console_[0x2000a1b]_map", and
        // stock DreamSeeker draws a screen object with that map prefix in it.
        if (r == '[' || r == ']') continue;
        return false;
    }
    name = head;
    rest = s.substr(colon + 1);
    return true;
}

}  // namespace

std::string screenLocMap(const std::string& loc) {
    std::string name, rest;
    return splitMapCtrl(trim(loc), name, rest) ? name : std::string();
}

bool parseScreenLoc(const std::string& loc, int cols, int rows, int tileW, int tileH, ParsedScreenLoc& out) {
    std::string s = trim(loc);
    if (s.empty()) return false;
    out = ParsedScreenLoc{};
    std::string name, rest;
    if (splitMapCtrl(s, name, rest)) { out.mapCtrl = name; s = rest; }
    if (upper(s).find("SCREEN_") != std::string::npos || s.find('%') != std::string::npos) return false;

    std::string lo, hi;
    bool isRange = splitRange(s, lo, hi);
    if (!isRange) lo = s;
    Corner p;
    if (!parseCorner(lo, cols, rows, tileW, tileH, p)) return false;
    out.x = p.x; out.y = p.y; out.spanCols = 1; out.spanRows = 1;
    if (!isRange) return true;
    Corner q;
    if (!parseCorner(hi, cols, rows, tileW, tileH, q)) return false;
    int x0 = std::min(p.x, q.x), x1 = std::max(p.x, q.x);
    int y0 = std::min(p.y, q.y), y1 = std::max(p.y, q.y);
    out.x = x0; out.y = y0;
    out.spanCols = spanTiles(x1 - x0, tileW);
    out.spanRows = spanTiles(y1 - y0, tileH);
    return true;
}

// ---- line of sight --------------------------------------------------------------------------------

CellSet visibleCells(int ex, int ey, int rng, const std::function<bool(int, int)>& opaque) {
    CellSet vis;
    vis.insert({ex, ey});
    if (rng < 0) return vis;
    auto cheb = [&](int x, int y) { return std::max(std::abs(x - ex), std::abs(y - ey)); };
    // The eye's own tile never blocks: an opaque mech, or an opaque turf, sees out of itself.
    auto blocks = [&](int x, int y) { return !(x == ex && y == ey) && opaque(x, y); };
    for (int d = 1; d <= rng; d++) {
        for (int x = ex - d; x <= ex + d; x++) {
            for (int y = ey - d; y <= ey + d; y++) {
                if (cheb(x, y) != d) continue;
                int ax = std::abs(x - ex), ay = std::abs(y - ey);
                bool seen = false;
                for (int nx = x - 1; nx <= x + 1 && !seen; nx++) {
                    for (int ny = y - 1; ny <= y + 1; ny++) {
                        if (nx == x && ny == y) continue;
                        if (cheb(nx, ny) != d - 1) continue;
                        if (std::abs(nx - ex) > ax || std::abs(ny - ey) > ay) continue;
                        if (nx != x && ny != y && blocks(nx, y) && blocks(x, ny)) continue;
                        if (vis.count({nx, ny}) && !blocks(nx, ny)) { seen = true; break; }
                    }
                }
                if (seen) vis.insert({x, y});
            }
        }
    }
    // The wall pass: an opaque cell is visible if any 8-neighbour is, without chaining wall to wall.
    // Decided against the PRE-pass visible set, applied afterwards.
    std::vector<std::pair<int, int>> add;
    for (int x = ex - rng; x <= ex + rng; x++) {
        for (int y = ey - rng; y <= ey + rng; y++) {
            if (vis.count({x, y}) || !opaque(x, y)) continue;
            bool done = false;
            for (int nx = x - 1; nx <= x + 1 && !done; nx++) {
                for (int ny = y - 1; ny <= y + 1; ny++) {
                    if (!vis.count({nx, ny})) continue;
                    if (blocks(nx, ny)) continue;
                    add.push_back({x, y});
                    done = true;
                    break;
                }
            }
        }
    }
    for (auto& p : add) vis.insert(p);
    return vis;
}

}  // namespace fugue::render

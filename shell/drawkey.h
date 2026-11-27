// Layer-band ordering with a float32 viewport-cell tie-break.
#pragma once
#include <cmath>

namespace fugue::shell {
struct DrawKey {
    int band = 0;
    float layer = 0;
    bool operator<(const DrawKey& b) const { return band != b.band ? band < b.band : layer < b.layer; }
    bool operator==(const DrawKey&) const = default;
};
inline DrawKey drawKey(float layer, bool screen, int cell) {
    DrawKey key{screen ? 1 : 0, layer};
    // Round the cell adjustment to float BEFORE testing the band thresholds.
    if (cell >= 0) key.layer = float(double(key.layer) - std::ldexp(double(cell), -18));
    if (key.layer >= 5000) {
        if (key.layer >= 20000) { key.layer = float(double(key.layer) - 20000); key.band -= 64; }
        if (key.layer >= 5000) { key.layer = float(double(key.layer) - 5000); key.band += 16; }
    }
    return key;
}
}

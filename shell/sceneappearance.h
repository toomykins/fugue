// Renderer-facing interpretation of the current decoded appearance fields.
#pragma once
#include "net/bytes.h"
#include "proto/messages.h"
#include <array>
#include <bit>
#include <cmath>
#include <algorithm>

namespace fugue::shell {
using ColorMatrix = std::array<float, 20>;
inline constexpr ColorMatrix identityColor{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0};
// Column-major affine RGBA composition, before the single material clamp.
// Parent material applies after child material (including the offset column).
inline ColorMatrix composeColor(const ColorMatrix& parent, const ColorMatrix& child) {
    ColorMatrix result{};
    for (int column = 0; column < 5; ++column) for (int row = 0; row < 4; ++row) {
        double value = column == 4 ? parent[16 + row] : 0;
        for (int k = 0; k < 4; ++k) value += double(parent[4*k + row]) * child[4*column + k];
        result[4*column + row] = float(value);
    }
    return result;
}
inline ColorMatrix tintColor(ColorMatrix matrix, const std::array<float,4>& tint) {
    for (int column = 0; column < 5; ++column) for (int row = 0; row < 4; ++row)
        matrix[4*column + row] *= tint[row];
    return matrix;
}
inline bool finiteColor(const ColorMatrix& matrix) {
    return std::all_of(matrix.begin(), matrix.end(), [](float value) { return std::isfinite(value); });
}
// Absolute planes occupy [-10000,10000]. Outside that range the signed wire
// short is FLOAT_PLANE + offset, modulo 65536 (including negative offsets).
inline int resolvePlane(int wire, int parent = 0) {
    return wire >= -10000 && wire <= 10000 ? wire : parent + int(int16_t(uint16_t(wire + 32767)));
}
struct SceneAppearance {
    int blend = 0, invisibility = 0;
    uint16_t visFlags = 0; // captured 516.1688 vars bit 0x2000, not appearanceFlags
    bool master = false, resetColor = false, resetAlpha = false;
    bool keepTogether = false, keepApart = false, fixedDir = false;
    ColorMatrix color = identityColor;
    bool colorValid = true;
    bool hasColorMatrix = false; // vars 0x400 present
    explicit SceneAppearance(const proto::Appearance& fields) {
        // This is the packed wire flag, not the DM PLANE_MASTER constant (128).
        const uint32_t flags = fields.appearanceFlags;
        master = (flags & 0x10000000u) != 0;
        // image(icon) inherits, image(icon, dir=SOUTH/NORTH/...) fixes direction.
        // Verified via 516.1688 wire captures, including dir=0 after construction:
        // fixedDir survives that assignment; the separate 0x200 bit does not.
        fixedDir = (flags & 0x00020000u) != 0;
        // Verified with isolated DM appearances and captured 0x26 records (516.1688),
        // not by shifting DM constants. These reset only the supported RGBA inheritance.
        resetColor = (flags & 0x00400000u) != 0;
        resetAlpha = (flags & 0x00800000u) != 0;
        keepTogether = (flags & 0x04000000u) != 0;
        keepApart = (flags & 0x08000000u) != 0;
        for (const auto& var : fields.vars) {
            const auto& bytes = var.data;
            if (var.bit == 0x2000 && bytes.size() == 2) visFlags = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
            if (var.bit == 0x100 && bytes.size() == 1) blend = uint8_t(bytes[0]);
            if (var.bit == 4 && bytes.size() == 1) invisibility = uint8_t(bytes[0]);
            if (var.bit == 0x400 && bytes.size() == 80) {
                hasColorMatrix = true;
                for (int i = 0; i < 20; ++i) {
                    uint32_t bits = 0;
                    for (int b = 0; b < 4; ++b) bits |= uint32_t(uint8_t(bytes[i*4+b])) << (8*b);
                    const float value = std::bit_cast<float>(bits);
                    colorValid &= std::isfinite(value);
                    color[i] = std::isfinite(value) ? value : identityColor[i];
                }
            }
        }
    }
};
}

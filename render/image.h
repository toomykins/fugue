// Common straight-alpha RGBA8 asset pixels.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace fugue::assets {
struct RgbaImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px; // w*h*4, top-down, straight alpha
};
struct Rgba8 { uint8_t r, g, b, a; };
// Round a channel to the nearest byte, saturating at 0 and 255.
inline uint8_t clamp8(double v) {
    if (v <= 0) return 0;
    if (v >= 255) return 255;
    return uint8_t(v + 0.5);
}
}

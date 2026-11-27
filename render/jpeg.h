// JPEG decoder for a plain-image icon (for example, a JPEG under a .gif name).
// Decoded with the system libjpeg(-turbo) at its defaults (JDCT_ISLOW, RGB out), which is what stock DreamDaemon matches
// pixel for pixel. The icon is the whole image, one frame, fully opaque, like a PNG without a DMI Description.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "render/image.h"

namespace fugue::assets {

// false + why on a malformed or unsupported stream (CMYK/YCCK are refused; grayscale is expanded to RGB).
// maxPixels caps w*h before any pixel buffer is allocated.
bool decodeJpeg(const uint8_t* d, size_t n, RgbaImage& out, std::string& why, uint64_t maxPixels);

}  // namespace fugue::assets

// PNG decoder to straight-alpha RGBA8, Qt-free (no QImage, no libpng, no zlib).
// Spec: PNG 1.2 (chunk layout, CRC, filters, colour types, tRNS) + RFC 1951 via inflate.h.
// Output rules (what "straight-alpha RGBA8" means for each source type) are described in png.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fugue::render {

struct RgbaImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w*h*4, row-major, top row first, straight (non-premultiplied) alpha
};

enum class PngStatus { Ok, Malformed, Unsupported };

// Decodes any 8-bit-per-channel (or 1/2/4-bit gray/palette), non-interlaced PNG. Every chunk's CRC is
// verified and IEND is required (a truncated file is an error). 16-bit and Adam7 PNGs return
// Unsupported: refused rather than guessed.
PngStatus decodePng(const uint8_t* d, size_t n, RgbaImage& out, std::string& why);

}  // namespace fugue::render

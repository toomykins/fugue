// GIF87a/89a decoder. decodeGifFirstFrame: frame 0 of the file as straight-alpha RGBA8. Later frames are decoded only to
// validate the stream (a single-frame GIF icon). decodeGifAnimation: every frame, composited on the logical screen, with
// BYOND's delays and loop count (an animated GIF icon).
// Spec: GIF89a (logical screen, colour tables, graphic control ext, image descriptor, LZW variable-width codes, sub-blocks,
// interlace). Decoder semantics:
//   the transparent colour index becomes RGBA (0,0,0,0); an out-of-range transparent index extends the palette with
//   transparent entries; the still icon is the first frame's own rectangle (not the logical screen) and has no animation.
//   The animation rules are stock BYOND 516 behaviour, noted at their lines in gif.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "render/image.h"

namespace fugue::assets {

// false + why on any malformed stream (a bad later frame rejects the whole file).
bool decodeGifFirstFrame(const uint8_t* d, size_t n, RgbaImage& out, std::string& why);

struct GifAnimation {
    int w = 0, h = 0;  // the logical screen = one frame
    int frames = 0;
    RgbaImage strip;             // w x (h*frames): frame i, already composited, at rows [i*h, (i+1)*h)
    std::vector<double> delays;  // deciseconds per frame, BYOND's (max(1, cs/10))
    int64_t loop = 0;            // BYOND's icon loop: 0 = forever, n = play n times
};

// false + why on a malformed stream, or when the composited frames would exceed maxBytes.
bool decodeGifAnimation(const uint8_t* d, size_t n, GifAnimation& out, std::string& why, size_t maxBytes);

}  // namespace fugue::assets

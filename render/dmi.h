// Qt-free icon metadata and decoded pixel sheets. PNG/DMI metadata parsing,
// direction selection and instance playback remain in shell/dmiicon.cpp.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/image.h"

namespace fugue::assets {

struct IconState {
    std::string name;  // "" is a real state (the unnamed default)
    int64_t dirs = 1;  // 1, 4 or 8; cells use S N E W SE SW NE NW slot order
    int64_t frames = 1;
    bool hasDelays = false;
    std::vector<double> delays;  // deciseconds per frame, as written (may be shorter than `frames`)
    bool movement = false;
    bool rewind = false;
    int64_t loop = 0;
    bool hasHotspot = false;
    std::vector<int64_t> hotspot;
    int64_t firstCell = 0;  // linear cell index of (dir slot 0, frame 0)

    // Cell index of (dir slot, frame), FRAME-major: firstCell + frame*dirs + dir. -1 if out of range.
    int64_t cellIndex(int64_t dirSlot, int64_t frame) const {
        if (dirSlot < 0 || dirSlot >= dirs || frame < 0 || frame >= frames) return -1;
        return firstCell + int64_t(frame) * dirs + dirSlot;
    }
};

struct Icon {
    std::string version;        // "4.0", "legacy 4", "png", "gif", ... as written / synthesised
    int width = 0, height = 0;  // per-cell pixels
    bool plainImage = false;    // a bare PNG/GIF: one unnamed state; one cell = the whole image, or one cell per
                                // frame of an animated GIF (stacked vertically, sheetCols 1)
    std::vector<IconState> states;
    RgbaImage sheet;
    int sheetCols = 1;

    // First state with this exact name, or -1. "" finds the unnamed state; a missing name is -1.
    int stateIndex(const std::string& name) const;
    // Straight-alpha RGBA8 pixels (width*height*4) of one cell. false + *err if the cell lies outside
    // the sheet or the indices are out of range.
    bool frame(const IconState& s, int64_t dirSlot, int64_t frame, std::vector<uint8_t>& out,
               std::string* err = nullptr) const;
};

struct ParseResult {
    std::unique_ptr<Icon> icon;
    // On failure: a stable class (see dmi.cpp) and text.
    std::string errClass, errDetail;
    bool ok() const { return icon != nullptr; }
};

// GIF/JPEG and bounded legacy-v4 only. Pixel-limit refusal is retryable when a
// caller shares the allowance across a preparation; unsupported data is not.
ParseResult parseIcon(const uint8_t* data, size_t n, uint64_t maxPixels = 64 * 1024 * 1024);

}  // namespace fugue::assets

// DDMI: BYOND's runtime icon-composition container (magic "DDMI"). It holds no pixels, only a recipe: the /icon procs
// that built the icon, as a postfix program over a value stack. An icon reference, a colour, a number or a string pushes
// a value; every other op pops its arguments and changes the icon below them. The icon drawn is the one value left on
// the stack (the first reference) once every op has run; a tag 0 ends the program early. A recipe this client cannot
// run makes the atom invisible, not a placeholder.
//
// Ops (payload after the tag byte, integers LE, i16 signed):
//   0x01 icon ref      u32 ordinal (0xffff with kind 1: `new /icon`), u8 kind
//   0x03 colour        u8 r, g, b, a (null is 0,0,0,0)
//   0x04 number        4 bytes (null where a string may stand)
//   0x05 string        cstring
//   0x40 icon()        u8 dir index (S N E W SE SW NE NW = 0..7), frame index, moving (0xff: not given); pops the
//                      icon_state (a string; anything else means none) and narrows the icon below it
//   0x41/0x4e Insert   u8 dir index, frame index, movement; i32 / f32 delay; pops the icon_state, then the inserted icon
//   0x42 Blend         u8 wire mode; pops the icon or colour operand
//   0x4d Blend at x,y  u8 wire mode, i16 x, i16 y; pops the icon operand
//   0x43 SetIntensity  f32 r, g, b (-1: same as r)
//   0x44 Flip          u8 dir index (S N E W SE SW NE NW = 0..7)
//   0x45/0x46 Turn     f32 degrees clockwise
//   0x47 Shift         u8 dir index, i16 offset, u8 wrap
//   0x48 SwapColor     u8 1 when the old colour carries alpha (or is null); pops new, then old
//   0x49 DrawBox       i16 x1, y1, x2, y2; pops the colour
//   0x4a MapColors     u8 count (12: rgb rows + constant, alpha kept; 20: full), count f32
//   0x4b Scale         i16 width, height
//   0x4c Crop          i16 x1, y1, x2, y2 as a half-open, top-down, 0-based pixel rectangle
//   0x3f nothing; 0x06 is refused.
//
// The wire blend byte is not an IconBlendMode: it uses its own numbering (ddmi.cpp wireBlend: 0 ADD, 1 SUBTRACT, 2 OR,
// 3 AND, 4 MULTIPLY, 7 OVERLAY, 8 UNDERLAY), so it must be mapped, never cast. Other modes are refused.
#pragma once
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "render/dmi.h"

namespace fugue::assets {

struct DdmiRef {
    uint32_t ordinal = 0;  // index into the ResourceList (the same space as an appearance's icon_res)
    uint8_t kind = 0;
    bool hasName = false;
    std::string name;  // the icon_state within the file
    bool hasDir = false;
    uint8_t trailer[3] = {0, 0, 0};  // the 0x40 payload: dir index, frame index, moving (0xFF: not given)
};

struct DdmiOp {
    uint8_t tag = 0;
    std::vector<uint8_t> payload;  // verbatim, excluding the tag
    int offset = -1;
    int ref = -1;  // 0x01: its index in refs; 0x05/0x40 qualifying a ref: that ref's index
};

struct DdmiRecipe {
    std::vector<DdmiRef> refs;
    int failureOffset = -1;
    std::vector<DdmiOp> ops;  // every op in wire order
};

bool isDdmi(const uint8_t* d, size_t n);
// A ref to no resource: `new /icon`, an icon with no states yet. It needs no operand.
bool ddmiRefIsEmptyIcon(const DdmiRef& r);
// false + *err on a malformed recipe (a PERMANENT refusal).
bool parseDdmi(const uint8_t* d, size_t n, DdmiRecipe& out, std::string* err);

struct DdmiResult {
    std::unique_ptr<Icon> owned;  // a new icon, or
    const Icon* alias = nullptr;  // the operand itself ("the extracted operand, verbatim" shape)
    bool limited = false;
    int failureOffset = -1, failureOperation = -1;
    std::string err;  // set when neither: a refusal (unsupported op / bad operand)
    const Icon* icon() const { return owned ? owned.get() : alias; }
};
// Owned, resumable interpreter. No model pointers or partial published results.
// Work is advisory; each operation still preflights dimensions/cells and a
// conservative 64 MiB live pixel-storage bound (inputs + stack + scratch).
class DdmiEvaluation {
public:
    using Clock = std::chrono::steady_clock;
    DdmiEvaluation(const DdmiRecipe& recipe, std::vector<Icon> inputs);
    ~DdmiEvaluation();
    // Advances at least one operation unless memory-deferred; yields only at
    // operation boundaries. availableBytes includes this job's owned inputs.
    bool step(Clock::time_point deadline, uint64_t availableBytes = 64 * 1024 * 1024);
    DdmiResult takeResult();  // only after step returns true
    uint64_t workPixels() const;
    uint64_t peakBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fugue::assets

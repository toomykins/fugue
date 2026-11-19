// Shared by the WorldState and InView decoders: the ten-array object-update grammar (proto::ObjectArrays).
#pragma once
#include "proto/registry.h"

namespace fugue::proto::g3 {

// Reads the ten counted object arrays and the optional tail byte.
ObjectArrays readObjectArrays(Reader& r);

// A self-delimiting DM value; returns the raw bytes consumed.
ByteSpan readDmValue(Reader& r);

}  // namespace fugue::proto::g3

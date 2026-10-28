// BrowseRecipeFiler: a runtime /icon sent by browse_rsc reaches the client as a DDMI recipe (render/ddmi.h), not pixels.
// A page that references it by name gets the PNG the recipe evaluates to, filed in the BrowseRscStore under the
// recipe's resource id once its operand icons have arrived. Session thread only.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "net/bytes.h"
#include "render/scene.h"
#include "shell/browsestore.h"

namespace fugue::shell {

class BrowseRecipeFiler {
public:
    // A resource landed and is already in `res`. A recipe is queued; then every queued recipe is tried again, since the
    // landing resource may be an operand. `ordinals` is the 0x000e availability list. A recipe that cannot be drawn
    // is dropped. Returns true when `data` is a recipe (the caller must not file its raw bytes).
    bool onResource(uint32_t id, const std::string& name, ByteSpan data, render::ResStore& res,
                    const std::vector<std::pair<uint32_t, uint8_t>>& ordinals, BrowseRscStore& store);
    size_t pending() const { return pending_.size(); }
private:
    std::map<uint32_t, std::string> pending_;   // recipe resource id -> the offer's name
};

}  // namespace fugue::shell

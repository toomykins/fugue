#include "shell/recipefiler.h"
#include "render/ddmi.h"
#include "shell/localorigin.h"

namespace fugue::shell {

bool BrowseRecipeFiler::onResource(uint32_t id, const std::string& name, ByteSpan data, render::ResStore& res,
                                   const std::vector<std::pair<uint32_t, uint8_t>>& ordinals, BrowseRscStore& store) {
    const bool recipe = render::isDdmi(data.data(), data.size());
    if (recipe) pending_[id] = name;
    for (auto it = pending_.begin(); it != pending_.end();) {
        const render::Icon* ic = res.iconResolved(it->first, ordinals);
        if (!ic && !res.refusedRecipes().count(it->first)) { ++it; continue; }   // an operand is still to come
        QByteArray png = ic ? LocalOrigin::cellPng(*ic) : QByteArray();
        if (!png.isEmpty()) store.put(it->first, it->second, std::string(png.constData(), size_t(png.size())));
        it = pending_.erase(it);
    }
    return recipe;
}

}  // namespace fugue::shell

// parseTarget: anything a player pastes -> the host:port the client dials. REFUSES `byond://Key.World` hub references.
// The rules follow the addresses stock DreamSeeker accepts.
#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace fugue::pager {

// Returns the normalized host:port, or nullopt with *err = the message shown to the player.
std::optional<std::string> parseTarget(std::string_view s, std::string* err = nullptr);

}  // namespace fugue::pager

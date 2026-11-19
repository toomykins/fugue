// skin/usermacros.h - the player's own macros: DreamSeeker's Client > Macros layer, beneath the skin's macro set.
//
// The Macros dialog is the `.macros` command, reached from the Options and Messages window (`.options`) and from the
// game window's system menu. It edits the player's lists, never the skin's set.
// Precedence: the skin's set, then this game's list, then the global list. A key the skin binds (even a disabled
// binding) is in use, and its answer stands. A set switch by the world replaces only the skin layer.
// Persistence (stock): global = <userpath>/cfg/macros.dmf; per game = <userpath>/Skins/<author>/<game>/macros.dmf,
// keyed by the world's hub path. The checkbox selects which list the dialog shows and edits. Lists are read at login.
// Format: a `.dmf` macro datum with no name, one `elem` per binding (stock writes CRLF line endings, this client LF;
//   both parse):
//         macro \r\n\telem \r\n\t\tname = "F1"\r\n\t\tcommand = "zap user"\r\n ... \r\n
//   Stock writes the newest entry first; this client writes them sorted by key. The order means nothing on load, so the
//   ordinary .dmf parser reads both.
//
// FUGUE STORAGE (never ~/.byond): <stateDir>/macros.dmf (global) and <stateDir>/macros/<world key>/macros.dmf (per game).
//   The world key is the server address: this client is never told the world's hub path, so it cannot key on the hub
//   the way DreamSeeker does.
#pragma once
#include <string>
#include <string_view>

#include "skin/macro.h"

namespace fugue::skin {

// Parse a macros.dmf (DreamSeeker's own format; CRLF or LF). The FIRST macro datum is the list. A parse error or a file
// with no macro datum yields an empty set (a damaged file must not stop the client; `err` says why when given).
MacroSet parseUserMacros(std::string_view text, std::string* err = nullptr);
// DreamSeeker's format, entries sorted by key spec, LF line endings.
std::string formatUserMacros(const MacroSet& s);

// The editor's commit rule. Removes the entry whose spec is `oldSpec` (when non-empty), then adds spec -> command under
// `id`, unless spec or command is blank (a blank key or command deletes the entry). Returns false when `spec` is
// non-blank but names no key, and nothing changes then.
bool editUserMacro(MacroSet& s, const std::string& oldSpec, const std::string& spec, const std::string& command,
                   const std::string& id = "");

// The macro a key fires with the player's lists beneath the skin's set (lookupHeld): per modifier combination the skin
// (a key it binds, even disabled, is "in use"), then this game's list, then the global list. `skin` may be null.
const MacroEntry* lookupLayered(const MacroSet* skin, const MacroSet& game, const MacroSet& global, const MacroKey& k,
                                bool repeat);
HeldMatch matchLayered(const MacroSet* skin, const MacroSet& game, const MacroSet& global, const MacroKey& k);
// The same for the Any macro that answers the key (MacroSet::findAny); it fires before the key's own.
const MacroEntry* lookupLayeredAny(const MacroSet* skin, const MacroSet& game, const MacroSet& global,
                                   const MacroKey& k, bool repeat);

// Storage paths under the client's state dir. worldKey "" = the global list.
std::string userMacrosPath(const std::string& stateDir, const std::string& worldKey);
std::string worldKeyFor(const std::string& host, int port);  // "127.0.0.1_45000" (filesystem-safe)
MacroSet loadUserMacros(const std::string& path);            // missing file = empty set
bool saveUserMacros(const std::string& path, const MacroSet& s, std::string* err = nullptr);  // creates directories

}  // namespace fugue::skin

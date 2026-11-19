// The window's side of a skin the world sent, or of the default skin a world without one brought back (app/main.cpp,
// SessionWorker::skinLoaded). The skin is already in the live skin; a resend of the loaded one changed nothing there
// (skin/skinfeed.h), so the window keeps its controls and macros.
#pragma once

class QString;

namespace fugue::shell {

class FocusFilter;
class SkinWindow;

// The active macro set into filt: the window's selected set, else the skin's first, else none (a skin with no macro set
// leaves every key unbound, as stock).
void applySkinMacros(const SkinWindow& win, FocusFilter& filt);
// The window syncs from the live skin and the macros are applied. Only the first skin sizes the window (sized): a reboot
// or a link() keeps the player's window size.
void worldSkinLoaded(SkinWindow& win, FocusFilter& filt, bool& sized);
// A --size WxH from the command line: the window takes it, and counts as sized so no skin overrides it.
bool applyUserSize(SkinWindow& win, const QString& wxh, bool& sized);

}  // namespace fugue::shell

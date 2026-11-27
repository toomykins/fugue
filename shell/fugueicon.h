// shell/fugueicon.h - Fugue's own icon and the desktop identity that goes with it.
//
// fugueIcon(): Fugue's own icon, one embedded QIcon file per size (packaging/flatpak/icons, shell/fugue.qrc). It is the
// application icon of both halves (app/main.cpp, app/launcher.cpp). The game window shows the world's skin icon when
// there is one (SkinWindow::setSkinIcon), else this one.
// kDesktopFileName is the app id and the name of the .desktop file that the Flatpak and `fugue-qt pager -install-desktop`
// install (pager/launch.h).
//
// Desktops differ. X11 window managers that draw _NET_WM_ICON show the world's icon on the game window. GNOME takes the
// icon from the matched .desktop file, so it always shows Fugue's. On Wayland a per-window icon needs xdg-toplevel-icon-v1.
#pragma once
#include <QIcon>
#include <QImage>
#include <QString>
#include <utility>
#include <vector>

namespace fugue::shell {

constexpr const char* kDesktopFileName = "io.github.toomykins.Fugue";
constexpr int kFugueIconSizes[] = {16, 24, 32, 48, 64, 128, 256};
QIcon fugueIcon();
// The same PNGs as (size, bytes), for an install into hicolor (pager -install-desktop).
std::vector<std::pair<int, QByteArray>> fugueIconPngs();

// Decode a skin icon using Qt's image formats. Null = undecodable.
QImage skinIconImage(const QByteArray& bytes);

}  // namespace fugue::shell

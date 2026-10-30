# Building

## Prerequisites

For Linux and macOS: a C++20 compiler (g++ 12 or newer, or a recent Xcode/Apple Clang), CMake 3.20 or newer, Ninja, pkg-config, and:

* Qt 6.4 or newer: Base (Core, Gui, Widgets, Network, Test), WebEngine (Widgets) and WebChannel
* libjpeg (libjpeg-turbo)
* zlib
* optional: libopenmpt, which plays tracker modules (MOD/S3M/XM/IT). Without it such music is refused with a line in
  `audio.log`.

| Distribution | Install |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-webengine-dev qt6-webchannel-dev libjpeg-dev zlib1g-dev libopenmpt-dev libgl-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtwebengine-devel qt6-qtwebchannel-devel libjpeg-turbo-devel zlib-devel libopenmpt-devel mesa-libGL-devel` |
| Arch | `sudo pacman -S base-devel cmake ninja pkgconf qt6-base qt6-webengine qt6-webchannel libjpeg-turbo zlib libopenmpt` |

On Linux, audio at run time requires one of `pw-play` (PipeWire), `paplay` (PulseAudio) or `aplay` (ALSA) on the `PATH`.
macOS uses the system Core Audio framework; no external audio player is needed.

## Windows (x64)

Requires Windows 10 version 1903 or newer (or Windows 11), Visual Studio 2022 with **Desktop development with C++**,
CMake, Git, and the **MSVC 2022 64-bit** Qt SDK. Qt 6.8.3 or newer is recommended; select Qt WebEngine, WebChannel,
and Positioning in the Qt installer. Do not use the MinGW Qt kit: WebEngine requires MSVC.

Install [vcpkg](https://github.com/microsoft/vcpkg) and bootstrap it. Qt comes from the Qt installer, while the
repository's `vcpkg.json` installs libjpeg-turbo, zlib and libopenmpt. In PowerShell, from the repository root
(adjust the two SDK paths):

```powershell
$qt = "C:/Qt/6.8.3/msvc2022_64"
$vcpkg = "C:/dev/vcpkg"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON
cmake --build build --config Release --parallel 4
$env:PATH = "$qt/bin;$pwd/build/vcpkg_installed/x64-windows/bin;$env:PATH"
./build/app/Release/fugue-qt.exe
```

Native Windows audio uses the default Windows playback device (waveOut); no external PCM player is required.
`--audio=none` and `--audio=wav:<path>` also work. Linux `cmd:` audio sinks are not supported on Windows.

Run tests:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:FUGUE_AUDIO = "none"
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--no-sandbox --disable-gpu"
ctest --test-dir build -C Release --output-on-failure --timeout 120
Remove-Item Env:QT_QPA_PLATFORM, Env:FUGUE_AUDIO, Env:QTWEBENGINE_CHROMIUM_FLAGS
```

Those WebEngine flags are for tests only; do not disable the sandbox for normal use.
The Git attributes preserve captured resource bytes on Windows; do not apply newline conversion to `tests/data`.

Create a portable directory (including Qt plugins, WebEngine helper/resources, MSVC runtime and dependency DLLs):

```powershell
./packaging/windows/package.ps1 -QtDir $qt
./dist/bin/fugue-qt.exe --version
```

Distribute the **whole `dist` directory**, not just the executable. Windows CI also uploads this directory as
`fugue-windows-x64`. The application retains console output for command-line modes.
`fugue-qt.exe pager -install-desktop` registers `byond://` for the current Windows user without administrator access;
it does not create a Start menu shortcut. Re-run it if you move the installation.

## Linux build

From the repository root:

```bash
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON
ninja -C build
ninja -C build install        # installs fugue-qt to bin/ under the install prefix
```

`-DFUGUE_RELEASE=ON` builds the player-facing binary: it starts as the launcher, the developer test hooks are compiled
out, and on Linux/macOS the client disables core dumps (and refuses ptrace on Linux). Without it, `fugue-qt` with no subcommand is the game client.

| Option | Meaning |
|---|---|
| `-DFUGUE_OPENMPT=AUTO\|ON\|OFF` | tracker-module playback via libopenmpt. `AUTO` (default) uses it when found; `ON` makes it required. A libopenmpt in a private prefix is found with `-DCMAKE_PREFIX_PATH=<prefix>`. |
| `-DFUGUE_RELEASE=ON` | the release build described above |
| `-DFUGUE_HEADLESS_ONLY=ON` | only the Qt-light modules, tools and tests; no window binary and no WebEngine |
| `-DFUGUE_COMPILE_JOBS=N` | cap on concurrent compile jobs. The default is one per GiB of memory available when CMake configures, at most one per core: a Qt-heavy translation unit can use about 860 MB. |
| `-DFUGUE_BUILD_GPU=ON` | the experimental GPU renderer and its tests; needs Qt 6.7+ with its Gui private headers and Shader Tools ([rendergpu/README.md](../rendergpu/README.md)) |

## macOS

Install Xcode Command Line Tools (`xcode-select --install`) and [Homebrew](https://brew.sh), then:

```bash
brew install cmake ninja pkgconf qtbase qtwebengine qtwebchannel qtsvg jpeg-turbo libopenmpt
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" \
  -DPKG_CONFIG_EXECUTABLE="$(brew --prefix)/bin/pkg-config"
cmake --build build
open build/app/Fugue.app
```

Use a native Homebrew installation for your architecture (Apple Silicon or Intel); do not mix arm64 and x86_64
libraries. The minimum supported macOS version is determined by your Qt distribution. The bundle's
`LSMinimumSystemVersion` is `CMAKE_OSX_DEPLOYMENT_TARGET` when set, else the building Mac's version; CI builds for 15.0,
the macOS its Homebrew libraries are built for. Official Qt installations
also work (include the Qt SVG module for bundle deployment): set `CMAKE_PREFIX_PATH` to the Qt macOS kit and the prefixes containing libjpeg/libopenmpt.

The build-tree app uses your installed libraries. To make a self-contained app, including Qt WebEngine's helper,
resources, plugins, and third-party libraries:

```bash
cmake --install build --prefix "$PWD/build/macos"
open build/macos/Fugue.app
# Optional: copy build/macos/Fugue.app to /Applications or ~/Applications.
```

Installation runs the selected Qt's `macdeployqt` and ad-hoc signs/verifies the complete bundle. No Homebrew packages
are needed on the destination Mac. This is **not** Developer ID signing or notarization; public distribution requires
those additional Apple signing steps. CI builds and tests both architectures and uploads ad-hoc-signed app archives.

Command-line options are unchanged; the executable is inside the bundle:

```bash
build/app/Fugue.app/Contents/MacOS/Fugue --version
build/app/Fugue.app/Contents/MacOS/Fugue client --connect host:port
```

Finder launch opens the launcher in release builds. Opening the app registers its `byond://` scheme with Launch
Services; clicked links are handled both at startup and while the launcher is running. The launcher joins a link as
its Join button does, so not while its game runs; a link opened while a game window runs is followed as a clicked page
link (at most one second game window). `pager -install-desktop`
is not used on macOS. Settings and credentials remain under `~/.fugue`, as on Linux. BYOND CTRL macros use the physical
Control key on macOS, not Command; paste and the browser find bar also take Command+V and Command+F.

## Tests

The tests are QtTest executables run through CTest. They need no BYOND installation and no network.

```bash
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R rscstore -V      # one test, verbose
```

`QT_QPA_PLATFORM=offscreen` lets the widget and WebEngine tests run without a display. Some tests skip with a message
when optional input is absent. The Core Audio device smoke test is opt-in (it writes silence):

```bash
FUGUE_TEST_COREAUDIO=1 ctest --test-dir build -R '^audio_sink$' --output-on-failure
```

## Flatpak

The manifest builds the release binary and bundles libopenmpt.

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Sdk//6.10 org.kde.Platform//6.10 io.qt.qtwebengine.BaseApp//6.10
flatpak-builder --user --install --force-clean --jobs=2 build-flatpak packaging/flatpak/io.github.toomykins.Fugue.yml
flatpak run io.github.toomykins.Fugue
```

Inside the sandbox the state directory is `~/.var/app/io.github.toomykins.Fugue/.fugue`, separate from a
native install's. `~/.byond/cfg` is visible read-only so an existing key can be imported.

## AppImage and .deb

Both are built by CI on a release tag and can be built locally. The `install` step puts `fugue-qt`, the desktop file, icons and
metainfo under the prefix (the same files the Flatpak uses).

**.deb**, on Ubuntu 24.04 (the dependency versions it records come from the machine that builds it):

```bash
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON -DFUGUE_STATIC_JPEG=ON
ninja -C build fugue-qt
cpack --config build/CPackConfig.cmake -G DEB        # fugue_<version>_amd64.deb; settings in packaging/deb/CPackDeb.cmake
```

It depends on the distribution's Qt 6 and WebEngine packages and bundles nothing. The Depends list is written out in
`CPackDeb.cmake`: Ubuntu 24.04 and Debian 13 name some Qt packages differently (`libqt6gui6t64` and `libqt6gui6`), so those are
alternatives, and `FUGUE_STATIC_JPEG` links libjpeg in because its soname differs too (`libjpeg-turbo8-dev` provides the static
library). `ctest -R deb_depends` checks both. Ubuntu's Qt is 6.4, so the package has no GPU renderer; Debian 13's is 6.8.

**AppImage**: `packaging/appimage/build.sh` needs a Qt 6.8 with WebEngine, WebChannel and ShaderTools, for example from
`aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtwebengine qtwebchannel qtpositioning qtserialport qtshadertools`. CI builds
on Ubuntu 22.04 for the oldest glibc, with g++ 12; its apt package list is in `.github/workflows/ci.yml` (the Qt libraries it
bundles are linked against X11, xkbcommon, Wayland and CUPS development packages).

```bash
QT_ROOT_DIR=$HOME/Qt/6.8.3/gcc_64 packaging/appimage/build.sh out    # out/Fugue-x86_64.AppImage
```

The script downloads linuxdeploy, its Qt plugin, appimagetool and the AppImage runtime at the versions and sha256 pinned in
it, bundles Qt (including `QtWebEngineProcess`, its resources and locales) and libopenmpt, and adds the launch hook
`packaging/appimage/fugue-sandbox.sh`. The hook keeps Chromium's user-namespace sandbox on where the system allows it
(an AppImage's mount is `nosuid`, so the setuid helper is not an option) and otherwise disables it for that run with a
warning on stderr; `FUGUE_KEEP_SANDBOX=1` turns the fallback off.

`fugue-qt webcheck` is the install smoke test for both: it starts WebEngine, runs a script in one page and prints
`webcheck ok`. CI runs it, offscreen, in clean Ubuntu, Debian and Fedora containers.

## Releases

The version is set once, by `project(... VERSION)` in the top-level `CMakeLists.txt`. It becomes `FUGUE_VERSION`
(`fugue_version.h`) and is what `fugue-qt --version`, the About box and the `fugue-version` winget answer report.
Configuring fails unless `packaging/flatpak/*.metainfo.xml` has a `<release>` for that version. Pushing a `v*` tag
makes CI build the Flatpak bundle and attach it to the GitHub release, then start the `Flatpak repository` workflow. The
AppImage and .deb jobs are switched off until the web sandbox tiers land.

The workflow runs `packaging/flatpak/publish-repo.sh`, which imports the latest release's bundle into a new repository,
signs it, and deploys it to GitHub Pages at <https://toomykins.github.io/fugue/>. It needs the repository secret
`FLATPAK_REPO_GPG_KEY` (the ASCII-armoured private key) and GitHub Pages set to deploy from Actions. Installed clients
trust that key, so changing it means every user adding the repository again. Run the workflow by hand to republish
without a new release.

# Building

## Prerequisites

For Linux and macOS: a C++20 compiler (g++ 12 or newer, or a recent Xcode/Apple Clang), CMake 3.20 or newer, Ninja, pkg-config, and:

* Qt 6.7 or newer: Base (Core, Gui, Widgets, Network, Test), matching Gui private headers, Shader Tools, WebEngine (Widgets) and WebChannel
* zlib and libjpeg (libjpeg-turbo recommended)
* optional: libopenmpt, which plays tracker modules (MOD/S3M/XM/IT). Without it such music is refused with a line in
  `audio.log`.

| Distribution | Install |
|---|---|
| Debian / Ubuntu | `sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-webengine-dev qt6-webchannel-dev zlib1g-dev libjpeg-dev libopenmpt-dev libgl-dev` |
| Fedora | `sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtshadertools-devel qt6-qtwebengine-devel qt6-qtwebchannel-devel zlib-devel libjpeg-turbo-devel libopenmpt-devel mesa-libGL-devel` |
| Arch | `sudo pacman -S base-devel cmake ninja pkgconf qt6-base qt6-shadertools qt6-webengine qt6-webchannel zlib libjpeg-turbo libopenmpt` |

The distribution must supply Qt 6.7+ and matching private headers. Older packages
(such as Ubuntu 24.04's Qt 6.4) cannot build the MAP renderer; use a newer Qt SDK or
configure `-DFUGUE_HEADLESS_ONLY=ON` for the non-GUI modules.

On Linux, running the tests also requires Xvfb and Mesa's OpenGL (Debian/Ubuntu: `xvfb libgl1-mesa-dri libglx-mesa0`,
Fedora: `xorg-x11-server-Xvfb mesa-dri-drivers`, Arch: `xorg-server-xvfb mesa`); see [Tests](#tests).

On Linux, audio at run time requires one of `pw-play` (PipeWire), `paplay` (PulseAudio) or `aplay` (ALSA) on the `PATH`.
macOS uses the system Core Audio framework; no external audio player is needed.

## Windows (x64)

Requires Windows 10 version 1903 or newer (or Windows 11), Visual Studio 2022 with **Desktop development with C++**,
CMake, Ninja, Git, and the **MSVC 2022 64-bit** Qt SDK. Qt 6.11.3 is what CI builds with on every platform (6.7 is the oldest that can build the renderer); select Qt WebEngine, WebChannel,
Shader Tools, and Positioning in the Qt installer, with matching Qt private headers.
Do not use the MinGW Qt kit: WebEngine requires MSVC.

### One-command local builds

With Qt and the repository's vcpkg dependencies already installed, copy
`build.local.example.psd1` to `build.local.psd1` and edit its paths once. The local
file is gitignored and loaded automatically. Then run:

```powershell
./tools/build.ps1 Debug
./tools/build.ps1 Release
```

Alternatively pass `-QtDir` and `-VcpkgRoot`, or `-DependencyRoot` pointing directly
at an installed `x64-windows` tree (including its debug libraries/DLLs).
Precedence is explicit parameters, then local-file settings, then `QTDIR` /
`VCPKG_ROOT` environment variables. Optional local settings are `DependencyRoot`
and `BuildRoot`; relative SDK/output paths are relative to the repository.
Use `-ConfigFile <path>` to select another data file; unknown settings or an
explicitly selected missing file are errors.
The script uses Ninja with the Visual Studio 2022 x64 compiler,
initializing MSVC automatically for child processes. Builds use separate
`build/windows/debug` and `build/windows/release` trees; older trees made with other
generators (such as `build/ninja/`) are left untouched. Override their parent with `-BuildRoot` (use a new directory).
Build concurrency defaults to physical cores, capped at one job per available GiB
after reserving 2 GiB; `-Jobs N` overrides it. The limit is recomputed for each build. Executables are under each tree's `app`.
It sets `FUGUE_RELEASE` appropriately, deploys Qt/WebEngine and matching dependency
DLLs, then checks `--version`. Release opens the launcher by default; use the
`pager` argument to open it in Debug.

**Before configuring or building, it deletes all existing executables in the
selected application's output directory, including deployed helpers.** A locked
executable stops the script immediately: close Fugue and its helpers, then retry.
It does not kill running processes. It does not delete executables in the other
configuration or in older build trees.

All DLLs in the selected dependency tree's `bin` (Release) or `debug/bin` (Debug)
are copied, including transitive dependencies. Prefer a dedicated dependency tree
to avoid copying unrelated packages. Keep the output directory together; do not
copy just the `.exe`. For distributable packages, use `packaging/windows/package.ps1`
with `-BuildDir build/windows/release` and the matching `-DependencyBin`.

### One-command local tests

`tools/test.ps1` configures, builds and runs the tests, with the same local settings and parameters as
`tools/build.ps1`:

```powershell
./tools/test.ps1 -Label fast        # the inner loop: headless suites of a few seconds each
./tools/test.ps1                    # every test, including those that open windows on the desktop
./tools/test.ps1 -Regex shell_entitymap
./tools/test.ps1 Debug -Label fast  # the same tests unoptimised, for stepping through a failure
./tools/test.ps1 -Backends all      # the GPU and native suites on every graphics backend, not only the default
```

It builds `RelWithDebInfo` in `build/windows/relwithdebinfo` by default: an optimised development build that keeps
the test hooks and debug information, links release Qt and the dependency tree's release libraries, and runs the
suite several times faster than `Debug` (the CPU-bound suites about ten times). Run tests in it unless you need
the debugger's view of unoptimised code; `Debug` uses `build.ps1`'s `build/windows/debug` tree with the debug
libraries. The two differ as the configurations do: `RelWithDebInfo` defines `NDEBUG` and `QT_NO_DEBUG`, so
`assert`, `Q_ASSERT` and the debug runtime's container and heap checks apply only in `Debug`. `-Label`, `-ExcludeLabel` and `-Regex` are CTest's `-L`, `-LE`
and `-R` (see [test profiles](#test-profiles)); `-TestJobs N` sets the number of concurrent test processes and
`-NoBuild` reruns the tests of the existing build. The script adds Qt's and the dependency tree's DLL directories
to `PATH` for the test processes only.

### Manual build

Install [vcpkg](https://github.com/microsoft/vcpkg) and bootstrap it. Qt comes from the Qt installer, while the
repository's `vcpkg.json` installs zlib, libjpeg-turbo and libopenmpt. From an **x64 Native Tools** PowerShell
prompt in the repository root (adjust the two SDK paths):

```powershell
$qt = "C:/Qt/6.11.3/msvc2022_64"
$vcpkg = "C:/vcpkg"
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl `
  "-DCMAKE_PREFIX_PATH=$qt" `
  "-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON
cmake --build build/release --parallel (cmake -P cmake/BuildJobs.cmake)
$env:PATH = "$qt/bin;$pwd/build/release/vcpkg_installed/x64-windows/bin;$env:PATH"
./build/release/app/fugue-qt.exe
```

Native Windows audio uses the default Windows playback device (waveOut); no external PCM player is required.
`--audio=none` and `--audio=wav:<path>` also work. Linux `cmd:` audio sinks are not supported on Windows.

Run tests:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
$env:FUGUE_AUDIO = "none"
$env:QTWEBENGINE_CHROMIUM_FLAGS = "--no-sandbox --disable-gpu"
ctest --test-dir build/release -L portable --no-tests=error --output-on-failure --timeout 120
Remove-Item Env:QT_QPA_PLATFORM, Env:FUGUE_AUDIO, Env:QTWEBENGINE_CHROMIUM_FLAGS
```

Those WebEngine flags are for tests only; do not disable the sandbox for normal use.
The Git attributes preserve captured resource bytes on Windows; do not apply newline conversion to `tests/data`.

Create a portable directory (including Qt plugins, WebEngine helper/resources, MSVC runtime and dependency DLLs):

```powershell
./packaging/windows/package.ps1 -QtDir $qt -BuildDir build/release `
  -DependencyBin build/release/vcpkg_installed/x64-windows/bin
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
cmake --build build --parallel "$(cmake -P cmake/BuildJobs.cmake)"
ninja -C build install        # installs fugue-qt to bin/ under the install prefix
```

`-DFUGUE_RELEASE=ON` builds the player-facing binary: it starts as the launcher, the developer test hooks are compiled
out, and on Linux/macOS the client disables core dumps (and refuses ptrace on Linux). Without it, `fugue-qt` with no subcommand is the game client.

| Option | Meaning |
|---|---|
| `-DFUGUE_OPENMPT=AUTO\|ON\|OFF` | tracker-module playback via libopenmpt. `AUTO` (default) uses it when found; `ON` makes it required. A libopenmpt in a private prefix is found with `-DCMAKE_PREFIX_PATH=<prefix>`. |
| `-DFUGUE_RELEASE=ON` | the release build described above |
| `-DFUGUE_HEADLESS_ONLY=ON` | only the Qt-light modules, tools and tests; no window binary and no WebEngine |

## macOS

Install Xcode Command Line Tools (`xcode-select --install`) and [Homebrew](https://brew.sh), then:

```bash
brew install cmake ninja pkgconf qtbase qtshadertools qtwebengine qtwebchannel qtsvg libopenmpt jpeg-turbo
cmake -G Ninja -S . -B build -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)" -DJPEG_ROOT="$(brew --prefix jpeg-turbo)" \
  -DPKG_CONFIG_EXECUTABLE="$(brew --prefix)/bin/pkg-config"
cmake --build build --parallel "$(cmake -P cmake/BuildJobs.cmake)"
open build/app/Fugue.app
```

Use a native Homebrew installation for your architecture (Apple Silicon or Intel); do not mix arm64 and x86_64
libraries. The minimum supported macOS version is determined by your Qt distribution. The bundle's
`LSMinimumSystemVersion` is `CMAKE_OSX_DEPLOYMENT_TARGET` when set, else the building Mac's version; CI builds for 15.0,
the macOS its Homebrew libraries (libopenmpt, libjpeg) are built for, with Qt's own 6.11.3 build. Official Qt installations
also work (include the Qt SVG module for bundle deployment): set `CMAKE_PREFIX_PATH` to the Qt macOS kit and the prefixes containing libopenmpt and libjpeg.

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

Build first, then run the QtTest suites through CTest. **CTest does not
rebuild**: stop after a failed build and rerun after the last relevant source edit.
The automated tests need no BYOND installation or game server; some use loopback
network connections. Python is not required to build or run the application tests.

For a single-config `build/` tree:

```bash
cmake --build build --parallel 2
ctest --test-dir build -L portable --no-tests=error --output-on-failure
```

On Windows, use `tools/test.ps1` ([above](#one-command-local-tests)), or the configured build directory with
Qt/dependency DLLs on PATH. Keep compiler/test logs locally; inspect failures and report skipped cases separately from passes.

Test an optimised build. A single-config tree defaults to `RelWithDebInfo`; an unoptimised `Debug` build
against debug Qt runs the same checks several times slower and is for debugging a failure. While editing,
run the fast tier, then the full selection before sending a change:

```bash
ctest --test-dir build -L fast -j 8 --no-tests=error --output-on-failure   # seconds
ctest --test-dir build -j 8 --no-tests=error --output-on-failure           # everything available
```

Use CTest to discover the current tests and labels:

```sh
ctest --test-dir build -N
ctest --test-dir build --print-labels
```

All suites share the `fugue_tests` executable; CTest runs each selected entry in a
separate process. For direct debugging, use `fugue_tests <suite> [QtTest arguments]`,
and use `fugue_tests --list` to discover suites. The executable is in
the build root, such as `build/fugue_tests.exe` on Windows. Prefer CTest for normal
runs because it applies each entry's arguments and environment.

A long CPU-bound suite is registered as several entries (`shell_entitymap_1of4` ... `_4of4`) so that
`ctest -j` runs its parts side by side. Each entry passes `--shard=K/N`, and the runner deals the suite's test
functions out among the N parts in declaration order, so every function runs in exactly one entry and a new
function needs no registration. `ctest -R shell_entitymap` selects all parts; `fugue_tests entitymap` still runs
the whole suite in one process.

### Test profiles

CTest entries declare their requirements with these labels:

- `portable`: logic and replay checks that run with offscreen Qt.
- `fast`: the portable checks that take a few seconds each in an optimised build: no GPU device, no window
  and no WebEngine. Entries are fast unless their registration says otherwise.
- `gpu`: rendering checks that require a working graphics backend.
- `native`: window-system integration checks that require a display.
- `device`: checks that require a real output device.
- `font`: checks that require installed font files; `FUGUE_FONT_DIR` selects their directory.

A selected profile fails if its required capability is unavailable or a check is skipped.
The general suite above selects `portable`; unfiltered CTest includes every available profile.
Checks for APIs absent from the target platform are omitted when compiling.

```sh
ctest --test-dir build -L portable --no-tests=error --output-on-failure
ctest --test-dir build -L gpu --no-tests=error --output-on-failure
ctest --test-dir build -L native --no-tests=error --output-on-failure
```

Each GPU and native entry runs on one graphics backend, named at the end of its name (`_vulkan`, `_d3d11`,
`_opengl`, `_metal`) and set in its environment as `FUGUE_GPU_BACKEND`; the tests do not choose, and they fail if the
device they get is another backend's. They use the machine's own adapter. Set `FUGUE_GPU_SOFTWARE=1` to ask for a
software adapter instead (Direct3D 11's WARP), as CI does; some stock comparisons hold only on a graphics card.

By default the entries exist for one backend only, the one the client itself uses on the platform: Vulkan on
Windows, OpenGL on Linux, Metal (and OpenGL for the off-screen suites) on macOS. The other backends are tested on
request: `-DFUGUE_TEST_GPU_BACKENDS=all`, or a list such as `d3d11;opengl` (`tools/test.ps1 -Backends all`, or
`-Backends d3d11,opengl`). The off-screen suites can use Vulkan only where the Vulkan headers are installed
(`vcpkg install vulkan-headers`; they are not in `vcpkg.json`), and Vulkan entries need a Vulkan driver to run;
without the headers the Windows default is Direct3D 11, which is what CI tests. To have Vulkan report misuse of its
API, install `vulkan-validationlayers` too and run a suite with
`VK_ADD_LAYER_PATH` set to the directory holding `VkLayer_khronos_validation.json`,
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and `QT_WIDGETS_RHI_DEBUG_LAYER=1`; messages appear as `vkDebug:`
lines.

GPU checks that need a window-system connection also carry `display`. On Windows,
`-L gpu -LE display` selects the Direct3D 11 checks, where those are configured, which can run without a desktop.
Native Windows checks need an unlocked interactive desktop.

Native checks that sample the actual desktop also carry `desktop`. Their test
windows must remain in the foreground and unobstructed throughout the observation.
When another application's window is over the sampled pixels before an observation starts, the check says which
(`Desktop interference: ...`) and CTest reports the entry as not run; one that arrives during an observation still
fails it. Use
`-L native -LE desktop` for the other native checks and `-L desktop` when that
screen access is available. Unfiltered CTest still includes both.

On Linux, every test process runs on a private Xvfb display that its launcher (`tools/xvfb-test.sh`) starts and
stops, so tests never see or draw on your desktop or touch its clipboard, and parallel entries get separate displays.
Plain `ctest` therefore behaves the same on a desktop, over SSH and in CI. Xvfb has no GPU, so OpenGL comes from
Mesa's software rasterizer (llvmpipe), as in CI. To run the tests on the current display and its own GPU driver
instead, configure with `-DFUGUE_TEST_XVFB=OFF`; the `desktop` checks then sample your real screen and the X11
primary-selection check replaces your selection and clipboard.

CTest sets the environment for each profiled entry. Test sources and fixture READMEs under `tests/data/` describe
individual expectations, required capabilities and reproduction conditions.

## Reproducing fixtures

Retained worlds under `tests/data/` are optional independent reproduction sources.
Stage copies under a fresh `build/` directory, generate assets where required,
and compile the staged DME with the recorded BYOND version. Some generators need
Python/Pillow; automated C++ tests use the checked-in wire, observations and goldens.

Launch a separate DreamDaemon on an unused port with `-safe -invisible`, then a
separate client with isolated state. Never replace an existing server, edit an
external game checkout, or stop processes by name/port. Close only processes you
started and can identify; a launcher forwarding to an existing client does not own
that client. A stock guest-mode request does not itself prove account isolation.

`tools/stockprobe/stockprobe.py` provides `record`, `pair` and `click` workflows for supported worlds on Windows.
Other fixtures retain their documented manual collection procedure.
Use each world's documented control file and wait for its readiness acknowledgement.
For worlds reading `probe-request.json`, supply fresh `token`, `phase` and `action`
fields and inspect the matching PROBE reply. A server acknowledgement is not a GPU
presentation fence. Record native DPI, coordinate units, foreground/geometry and
resize/reset history; equal outer windows alone do not establish comparable MAPs.
Keep raw captures locally and never regenerate stock expectations from Fugue output.
Strip account names, machine paths and process metadata before retaining fixture
expectations.

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

## AppImage

The AppImage CI job is currently disabled. For local builds, the
`install` step puts `fugue-qt`, the desktop file, icons and metainfo under the
prefix (the same files the Flatpak uses).

**AppImage**: `packaging/appimage/build.sh` needs a Qt 6 (CI: 6.11.3) with WebEngine, WebChannel and ShaderTools, for example from
`aqt install-qt linux desktop 6.11.3 linux_gcc_64 -m qtwebengine qtwebchannel qtpositioning qtserialport qtshadertools`. CI builds
on Ubuntu 22.04 for the oldest glibc, with g++ 12; its apt package list is in `.github/workflows/ci.yml` (the Qt libraries it
bundles are linked against X11, xkbcommon, Wayland and CUPS development packages).

```bash
QT_ROOT_DIR=$HOME/Qt/6.11.3/gcc_64 packaging/appimage/build.sh out    # out/Fugue-x86_64.AppImage
```

The script downloads linuxdeploy, its Qt plugin, appimagetool and the AppImage runtime at the versions and sha256 pinned in
it, bundles Qt (including `QtWebEngineProcess`, its resources and locales) and libopenmpt, and adds the launch hook
`packaging/appimage/fugue-sandbox.sh`. The hook keeps Chromium's user-namespace sandbox on where the system allows it
(an AppImage's mount is `nosuid`, so the setuid helper is not an option) and otherwise disables it for that run with a
warning on stderr; `FUGUE_KEEP_SANDBOX=1` turns the fallback off.

CI verifies the deployed application offscreen in clean Ubuntu, Debian and Fedora containers.

## Releases

The version is set once, by `project(... VERSION)` in the top-level `CMakeLists.txt`. It becomes `FUGUE_VERSION`
(`fugue_version.h`) and is what `fugue-qt --version`, the About box and the `fugue-version` winget answer report.
Configuring fails unless `packaging/flatpak/*.metainfo.xml` has a `<release>` for that version. Pushing a `v*` tag
makes CI build the Flatpak bundle and attach it to the GitHub release, then start the `Flatpak repository` workflow. The
AppImage job is switched off until the web sandbox tiers land.

The workflow runs `packaging/flatpak/publish-repo.sh`, which imports the latest release's bundle into a new repository,
signs it, and deploys it to GitHub Pages at <https://toomykins.github.io/fugue/>. It needs the repository secret
`FLATPAK_REPO_GPG_KEY` (the ASCII-armoured private key) and GitHub Pages set to deploy from Actions. Installed clients
trust that key, so changing it means every user adding the repository again. Run the workflow by hand to republish
without a new release.

#!/usr/bin/env bash
# Build Fugue-x86_64.AppImage from the source tree.
#
#   QT_ROOT_DIR=/path/to/Qt/6.8.3/gcc_64 packaging/appimage/build.sh [outdir]      (run from anywhere)
#
# Needs a Qt 6 with WebEngine, WebChannel and ShaderTools (the CI job uses aqtinstall's 6.8.3 on ubuntu-22.04, so the
# image's glibc floor is 2.35), plus the build prerequisites of docs/BUILDING.md and libopenmpt-dev. Work files go under
# $WORK (default <outdir>/work); downloaded tools are checked against the sha256 pinned below.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
src="$(cd "$here/../.." && pwd)"
out="$(mkdir -p "${1:-$PWD}" && cd "${1:-$PWD}" && pwd)"
work="${WORK:-$out/work}"
: "${QT_ROOT_DIR:?set QT_ROOT_DIR to the Qt 6 prefix (the directory holding bin/qmake)}"
qt="$QT_ROOT_DIR"

declare -A url sha
url[linuxdeploy]=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
sha[linuxdeploy]=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
url[linuxdeploy-plugin-qt]=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
sha[linuxdeploy-plugin-qt]=15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
url[appimagetool]=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
sha[appimagetool]=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
url[runtime]=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
sha[runtime]=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

tools="${TOOLS:-$work/tools}"
mkdir -p "$tools"
for t in "${!url[@]}"; do
  f="$tools/$t"
  if [ ! -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "${sha[$t]}" ]; then
    curl -fsSL --retry 3 -o "$f" "${url[$t]}"
  fi
  echo "${sha[$t]}  $f" | sha256sum -c --quiet - || { echo "build.sh: $t does not match its pinned sha256" >&2; exit 1; }
  chmod +x "$f"
done
# linuxdeploy finds its plugin by the name linuxdeploy-plugin-qt*.AppImage beside it. Neither needs FUSE.
cp "$tools/linuxdeploy" "$work/linuxdeploy-x86_64.AppImage"
cp "$tools/linuxdeploy-plugin-qt" "$work/linuxdeploy-plugin-qt-x86_64.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1

build="$work/build"
appdir="$work/AppDir"
rm -rf "$appdir"
cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt" \
  -DFUGUE_RELEASE=ON -DFUGUE_OPENMPT=ON -DFUGUE_BUILD_GPU=ON -DFUGUE_COMPILE_JOBS="${FUGUE_COMPILE_JOBS:-2}" \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$build" --target fugue-qt -j"${FUGUE_COMPILE_JOBS:-2}"
DESTDIR="$appdir" cmake --install "$build"

# The AppRun hook decides about Chromium's sandbox at launch (see the file).
mkdir -p "$appdir/apprun-hooks"
install -m644 "$here/fugue-sandbox.sh" "$appdir/apprun-hooks/fugue-sandbox.sh"

id=io.github.toomykins.Fugue
export QMAKE="$qt/bin/qmake"
export LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Wayland sessions need the Wayland platform plugins; the xcb one is always deployed.
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so"   # offscreen: the CI smoke test
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration;xcbglintegrations;tls"
( cd "$work" && ./linuxdeploy-x86_64.AppImage --appdir "$appdir" --plugin qt \
    --executable "$appdir/usr/bin/fugue-qt" \
    --desktop-file "$appdir/usr/share/applications/$id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$id.png" --icon-filename "$id" )
test -e "$appdir/usr/lib/libopenmpt.so.0" || { echo "build.sh: libopenmpt was not bundled" >&2; exit 1; }
test -x "$appdir/usr/libexec/QtWebEngineProcess" || { echo "build.sh: QtWebEngineProcess was not bundled" >&2; exit 1; }

rm -f "$out/Fugue-x86_64.AppImage"
ARCH=x86_64 "$tools/appimagetool" --no-appstream --runtime-file "$tools/runtime" "$appdir" "$out/Fugue-x86_64.AppImage"
echo "built $out/Fugue-x86_64.AppImage"

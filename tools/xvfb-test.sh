#!/bin/sh
# Runs one test process on a private Xvfb display. On Linux, cmake/FugueTests.cmake makes this the launcher of every
# CTest entry, so no test sees or draws on the developer's desktop or touches its clipboard, and entries run in
# parallel each get a display of their own (Xvfb picks a free one with -displayfd). Xvfb has no GPU: OpenGL comes
# from Mesa's software rasterizer, as in CI.
#
# Usage: xvfb-test.sh <program> [arguments...]
set -u

if ! command -v Xvfb >/dev/null 2>&1; then
    echo "xvfb-test.sh: Xvfb not found. Install it (Arch: xorg-server-xvfb, Debian/Ubuntu: xvfb), or configure" \
         "with -DFUGUE_TEST_XVFB=OFF to run the tests on the current display." >&2
    exit 1
fi

dir=$(mktemp -d "${TMPDIR:-/tmp}/fugue-xvfb.XXXXXX") || exit 1
Xvfb -displayfd 3 -screen 0 1280x1024x24 -nolisten tcp -noreset 3>"$dir/display" 2>"$dir/xvfb.log" &
xvfb=$!
cleanup() {
    kill "$xvfb" 2>/dev/null
    wait "$xvfb" 2>/dev/null
    rm -rf "$dir"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Xvfb writes the display number and a newline once it accepts connections.
tries=0
until [ "$(wc -l <"$dir/display" 2>/dev/null || echo 0)" -ge 1 ]; do
    if ! kill -0 "$xvfb" 2>/dev/null; then
        echo "xvfb-test.sh: Xvfb exited before it was ready:" >&2
        cat "$dir/xvfb.log" >&2
        exit 1
    fi
    tries=$((tries + 1))
    if [ "$tries" -gt 200 ]; then
        echo "xvfb-test.sh: Xvfb did not become ready within 10 seconds" >&2
        exit 1
    fi
    sleep 0.05
done

DISPLAY=":$(head -n 1 "$dir/display")"
export DISPLAY
unset WAYLAND_DISPLAY        # nothing reaches the desktop's compositor either
export LIBGL_ALWAYS_SOFTWARE=1

"$@"
status=$?
exit "$status"

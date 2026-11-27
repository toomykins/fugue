# Manual MAP gesture callback oracle

No game assets or BYOND executables are included. Generate in a separate directory:

```sh
python -B tests/data/map-gesture-world/prepare.py build/map-gesture-world
# Compile build/map-gesture-world/world.dme with the installed Dream Maker.
# Check that a separate test port is free, then launch the compiled world with
# DreamDaemon world.dmb <unused-port> -safe -invisible (from the staged directory).
```

Use an owned, separate stock Dream Seeker process and a Fugue guest with an isolated
`--state-dir`; never replace the existing SS13 server. Wait for `READY` in
`callbacks.log`. Capture only an owned foreground window. Close/stop only recorded
process identities. See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

The red world object is at (3,4,1), blue mob at (4,4,1), green destination at
(5,4,1). Red/green HUD objects occupy screen-loc 2,2 and 6,2. A translucent green
blocker above the red world object starts with mouse_opacity=0. The callback log
records event order, source/destination types, world coordinates, locations,
controls and params. It appends across server restarts; retain each run's evidence.
The initial view size and native DPI can differ between clients: find the actual
rendered rectangles instead of replaying desktop coordinates blindly.

Write `0`, `1`, or `2` (without a newline) to the fixture's `control.txt` to change
blocker opacity. `popup-off` / `popup-on` change connected clients' popup setting.
These controls are **only** in the synthetic world, not in the SS13 checkout.

Manual cases:

- Click object/mob/turf, including Alt/Ctrl/Shift. Opacity 0 must click through the
  blocker; opacity 1 names the blocker. Record packed appearance flags separately
  (Fugue's opt-in `FUGUE_INPUT_TRACE=1` worker trace), not DM constants.
- World→world, HUD→world, world→HUD, HUD→HUD: Down, Drag, Up, Drop; no Click.
- Move far within one atom: Down, Up, Click. Cross a target boundary by only a few
  physical pixels: Drag/Drop. Cross and return: Drop, no Click. Motion inside the
  same destination does not repeat Drag.
- Change modifiers between Down, Drag and release; callbacks use event-time mods.
- Release outside MAP: null destination, Up then Drop, no Click. Return and click
  normally; input must not remain held.
- Escape during a drag does not cancel stock's drop. Popup-enabled right presses
  are consumed by stock's menu; compare right dragging with popup-off separately.

Automated native submission, cancellation, pending-frame and stale-identity tests
live in `shell/tests/mapnative_test.cpp`, `mapinput_test.cpp`, and `entitymap_test.cpp`.
See [renderer input contracts](../../../docs/RENDERING.md#presentation-and-input)
for the rendered-frame and identity/lifetime requirements.

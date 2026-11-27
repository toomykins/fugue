# Local target / PASS_MOUSE evidence

`world.dm` is the retained `build/passmouse-world.dm` stock fixture, with file
logging added. `aistatic.dm` is the retained aistatic source, unchanged.
`stock-mouse.tsv` retains the original **24 callbacks / ten menu stacks** from
DreamSeeker 516 (Wine), DreamDaemon 516.1684. It is not a fresh Wine run.
The original icon binary was unavailable here: `prepare.py` generates the
specified 32px top-left opaque quadrants and ordinary floor/rock/mob controls.
The old callback expectations pass unchanged with these assets and fresh wire.
No historical renderer implementation supplies expected results.

## Fresh stock 516.1688 observations (2026-10-29)

Both Windows executable versions were queried as `5.0.516.1688`. Isolated port
14579; recorded process ownership used PID, creation-time and executable checks. No port-1337 process, external
checkout, pager identity or global setting was changed. Trust was approved for
these synthetic worlds without selecting “don't ask again”. Network recorders
used separate guest state directories; stock used `-new -guest` with its existing
saved UI state (not claimed isolated).

- `frames.txt`: fresh current `net::Session`/model capture. PASS_MOUSE is packed
  `0x80000000`, not the DM constant. Turf images use `locKind=7`, with a flattened
  zero-based world cell. The recording tool is opt-in `relay_wire_probe`; it does
  not render or provide a pixel/input oracle. Resource files are verified session
  assets, not screenshots or CPU-rendered substitutes.
- `group-world.dm` / `frames-group.txt`: two-level extension proves cell 370 is
  `(10,9,1)` zero-based in a 15×15 world. An explicit KEEP_TOGETHER source with an
  ordinary -12,+8 overlay captures a **44×40** local rectangle. The wire source
  has `appearanceFlags=0x04000a04`. Omitting KEEP_TOGETHER leaves the overlay at
  the original source instead of relaying it. Nested groups, group tint/effects
  and duplicate-name winner semantics were not independently established.
- Fresh callbacks: the world source at `(8,11,1)`, relayed to `(8,10,1)`, reports
  **source** location `(8,11,1)`, icon `(9,24)` and vis `(9,-8)` at viewport
  bottom-origin pixel `(104,183)`. Group overlay pixels `(120,159)` and `(56,159)`
  name `obscured`, icon `(-7,32)`, vis `(121,160)` / `(57,160)`. The group's ordinary
  red pixel `(136,151)` retains icon `(9,24)`, vis `(137,152)`.
- `stock-base.png`, `stock-group.png`: independently captured stock MAPs normalized
  to **224×224 source pixels**. Native tests compare every pixel on default and
  OpenGL backends, not just geometry or agreement with another renderer.

Artifacts:

- Initial wire: `build/local-relay-runtime/wire-fields.txt{,.wire}`;
  owned server `build/probes/20261029T160906Z-1e52f3fc`.
- Base screenshot: `build/probes/20261029T162551Z-e7d5e9cd/capture-20261029T162634Z-35f56396.png`;
  callbacks: `build/local-relay-runtime/events.log`.
- Extra source/location/level and group observations:
  `build/local-relay-stock-extra/{world.dm,wire-fields.txt,group-wire-fields.txt,events.log}`;
  final owned server `build/probes/20261029T163338Z-60032984` and stock client
  `build/probes/20261029T163412Z-390adba7`.
- Group screenshot: that client's `capture-20261029T163503Z-64f59508.png`.
  Before KEEP_TOGETHER: `build/probes/20261029T163058Z-d7b5beda/capture-20261029T163236Z-d0b03dc4.png`.

Both accepted stock screenshots have DPI **144**, window 470×529 physical,
client 448×473 physical, and MAP 448×448 physical (two pixels per source pixel).
The PNG crop is `(11,45)..(459,493)`, reduced by nearest source-pixel centers.
Base uses startup layout, without resize/refit. The extra/group runs explicitly
restore/resize the owned window to client 448×473 at physical `(80,80)` before
capture; readiness is READY/GROUP_READY plus stable visible output. These are not
paired whole-window comparisons: native Qt tests use a 448×448 **logical** MAP,
normal QRhi submission and device-pixel-ratio-aware framebuffer samples. They test
source pixels and logical input, not stock DPI/layout-history parity.

A foreground-timeout capture and a subsequent visibly incorrect crop were rejected;
only the explicit resize and accepted captures above are used. Early network
connections made while the server's trust dialog was open failed; they produced
no evidence. All owned processes, including the failed-connect stock instance,
were stopped by identity. No live-game or broad performance parity is claimed.

## Reproduction

```
python -B tests/data/local-relay-world/prepare.py build/new-relay-runtime
# Or add --group; wait for GROUP_READY in events.log.
# Compile world.dme with DreamMaker; start an owned isolated DreamDaemon.
cmake --build build/windows/release --target relay_wire_probe --parallel 2
./build/windows/release/tools/relay_wire_probe.exe PORT build/new-relay-guest build/new-relay-runtime/fields.txt
```

Adjust the executable path for a single-config generator and supply the configured
Qt/dependency DLL directories on PATH. The recorder explicitly refuses port 1337. Preserve full `.wire` files locally;
the checked-in replay drops handshake/resource-transfer frames and retains the
current model's normal server frames. Regression coverage lives in
`composition_test`, `entitymap_test`, `mapinput_test`, and native `mapnative_test`.

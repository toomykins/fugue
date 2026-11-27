# Synthetic built-in darkness fixture

Regression fixture for the bounded built-in illumination subset, not full lighting
parity. See [renderer visibility and lighting](../../../docs/RENDERING.md#visibility-and-lighting)
for the supported scope.

Generate into a separate disposable directory, then compile with BYOND:

```sh
python -B tests/data/darkness-world/prepare.py build/darkness-runtime
'C:/Program Files (x86)/BYOND/bin/dm.exe' build/darkness-runtime/world.dme
```

Launch the staged DMB manually with DreamDaemon `-safe -invisible` on an unused
port. Connect stock and Fugue sequentially with separate client state so captures
do not compete for foreground ownership. Do not replace existing servers; see
[manual fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

The default 9x9 room has an observer at (5,5), a cyan lamp at (3,5), a red target at
(8,5), and a yellow HUD marker. Pass `--size 17` to `prepare.py` for the source-window
variant; its recorded phases use `eyex=9&eyey=9`. All assets are generated locally.
The original 32 cases use no authored lighting effects; the blackness extension
adds optional plane masters/targets and a lower-plane screen backdrop. No external
game assets are used.

Write a query string without a trailing newline or duplicate keys to the runtime
`phase.txt`. Wait for its matching `PHASE` in `events.log`, then a rendered frame.
Controls (all reset on each phase):

- `ambient=0|1`: the room area's luminosity (default 1).
- `see=0|1|2|6`: observer `see_in_dark` (default 0).
- `lamp=0|3`: lamp luminosity (default 0).
- `sight=normal|blackness|turfs|objs|ghost|through|self`: ordinary sight,
  `SEE_BLACKNESS`, the individual categories, all three categories, `SEE_THRU`,
  or `SEE_SELF`.
- `wall=1`: an opaque column at X=6; `wallx` moves it, `door=open` opens Y=5.
- `lampx`, `lampy`: source position; `lamp_invisible=1` makes it invisible, not unlit.
- `eyex`, `eyey`: observer position; `view=3|4` selects view radius (default 4).
- `offset`: target pixel_x, for separating lighting anchor from appearance bounds.
- Add `run=<nonce>` to repeat an identical phase.

Blackness extension (all reset each phase):

- `black=1` adds `SEE_BLACKNESS` to any sight selection. Additional selections
  `mobs`, `turfobj`, `mobobj`, `turfmob` exercise individual/pair category bypasses.
- `targetx`, `targety`, `plane`, `layer`, `wide=1`: target placement/order and a
  64x32 icon; `offset` is still a pixel offset, never the light anchor.
- `selfplane`, `selfoffset`: independently place the admitted observer pixels.
- `backdrop=1`: repeated purple screen tiles, normally on plane -5;
  `backplane`, `backlayer` vary screen ordering.
- `master=identity|alpha|zero|matrix|add|multiply|target|targetalpha`: a plane-0
  master, optionally with hidden target and a mouse-transparent plane-5 SOUTHWEST
  source. Alpha is 128/255; matrix adds (0.25,0,0.5) to RGB.
- `turfplane`, `flooricon=none`, `hudplane`, `masterplane`: input probe controls
  for independently placed or iconless content.
- `source=none` removes the consumer; `targetname=coverage` changes the default
  hidden `*coverage` producer to a directly shown named producer.
- `events.log` records selected `/atom/Click` type/location/params.

`blackness-observations.json` retains 42 additional stock configurations, native
pixel samples (including both sides of a cell boundary), whole-MAP RGB SHA-256 and
selected input callbacks. Samples/hashes are normalized to the stock 288x288 native
MAP. Compare physical pixel **centers**, not rounded pixel corners, at differing
DPI. `shell/tests/blacknessfixture.h` reproduces these scenes for native regression
checks on both QRhi backends. All retained callbacks, including the originally
excluded target case, now participate in callback parity assertions.
`source-input-observations.json` adds 14 configurations / 98 stock click positions
for the phantom-turf fix, consumer presence, named targets, zero-alpha masters,
independent planes/HUD, transparent turf pixels and iconless turfs. Interactive
composited-source hits remain outside this fixture's verified subset. See the
[renderer input contracts](../../../docs/RENDERING.md#presentation-and-input).

For example, compare `ambient=1&see=0&sight=normal` with
`ambient=0&see=2&sight=normal`, then `ambient=0&see=0&sight=normal&lamp=3`.
The 32 recorded phases are in `stock-observations.json`,
`extended-observations.json` and `source-window-observations.json`. Captured
scalar/presence and area-rebind messages are in `wire-scalars.json`.
`mapvisibility_test` consumes these fixtures; native `mapnative_test` separately covers
pixels, last-rendered picking and warm zero uploads. The masks come from
**stock MAP screenshots**, not server `view()` output:
rows run north-to-south, columns west-to-east; `1` = floor, `W` = wall,
`0` = black at an interior point away from the marker icons.

Capture only the known client's foreground window under stable geometry, then
close only the clients/server you started. No capture/process runner is retained.

Use the actual physical MAP tile size when sampling screenshots. On the recorded
150%-DPI desktop stock used 32-pixel tiles and Fugue used 48-pixel tiles; do not
assume the two full-window PNGs have identical geometry. The 288x288 skin MAP size
is logical, not a guarantee of physical screenshot dimensions. Changing `view`
can also rescale stock tiles: recalibrate rather than sampling a new view on an
old grid. The retained tile masks all use view radius 4.

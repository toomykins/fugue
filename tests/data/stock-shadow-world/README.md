# Stock outer-shadow regression

Independent BYOND **516.1688** observations for `MapNativeTest::stockShadowPixels`.
Sources/assets are synthetic. Captured player display names use the same-length
synthetic `TestGuest`; all other retained wire bytes are unchanged. This does not
establish guest-account isolation.

## Automated inputs

- `observations.json`: **32** outer-shadow recipes and three canonical wire rows,
  including six actual stock Click receipts. Inner shadows are not renderer goldens.
- **26** deduplicated PNGs: independent **224x224** stock MAP crops, not
  renderer-generated expected images or a UI screenshot archive.
- Three `frames-*.txt` files and `res/`: wire/assets for the TG shadow parameters,
  a plane target/source and repeated shadows.

Native tests compare source-pixel centers with per-channel tolerance one, check
warm texture reuse, exercise worker filter admission and original-ink ownership,
and reject stale source identities. The [plane-input fixture](../plane-pass-world/README.md)
adds independent world/HUD PASS_MOUSE receipts. These are bounded cases, not
arbitrary shadow/filter or input parity.

Recipes include fractional placement, growth, partial source/color alpha,
small/large blur, clipping, repeated/mixed operations and a plane target. Negative
growth showed no erosion in the recorded controls. Historical experiments and
failed initial assertions are not generalized into passing cases; no new live-game,
inner-shadow or full gesture claim follows.

## Reproduction

Requires BYOND and Python/Pillow:

```sh
python -B tests/data/stock-shadow-world/prepare.py build/shadow-repro
```

This stages a standalone `world.dm`, generated control assets, `cases.json` and a
skin. Compile its `world.dme`; launch a separately owned DreamDaemon with
`-safe -invisible` on an unused isolated port, then a separate stock client.
Do not replace existing sessions, modify global settings or stop by name/port.

Select a case by writing `unique-token:<case>` to the runtime's `phase.txt` and
wait for `READY` in `events.log` plus stable foreground/geometry. `control` supplies
the calibration scene. Collect a native 224x224 source crop after settling, retain
DPI and resize/reset history, and compare sequential clients under matched controls.
For input, sample `(70,104)` and `(104,104)` in `shadow-tg`, `shadow-target` and
`shadow-repeat2`; retain actual EVENT receipts, not inferred pixel ownership.

The old broad matrix capture driver and its pixel wrapper are removed. Fresh
collection is manual; no replacement capture framework is required by the tests.
`relay_wire_probe` remains available for separately owned wire collection. Full
windows/logs belong under `build/`; review independent stock output before changing
an oracle, and never regenerate it from Fugue.

See [renderer composition](../../../docs/RENDERING.md#composition-and-transforms).

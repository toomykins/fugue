# Plane-master tree fixtures

Independent Windows BYOND **516.1688** observations for master-owned leaves,
nested masters, grouping, outlines and bounded placement. Sources and generated
control assets are synthetic. Neither a historical Fugue renderer nor current
Fugue output supplies expected stock results. Captured player display names use
the same-length synthetic `TestGuest`; all other wire bytes are unchanged. This
anonymization does not establish guest-account isolation.

## Retained test inputs

- `frames-*.txt` and `res/`: canonical wire/model inputs and shared icon resources.
- `stock-observations.json`, `stock-group-observations.json` and the three
  `stock-outline*-observations.json` files: phase names, sampled source points and
  actual stock Click receipts, consumed by `MasterTreeFixture`.
- `stock-*.png`: independent stock MAP crops used by native `mapnative_test` rows.
  Byte-identical crops share `stock-overlay.png` through
  `MasterTreeFixture::goldenFile()`; an absent alias is not a missing expectation.
- `stock-placement.json` and placement crops: independent expectations for
  `MasterPlacementFixture`'s explicit root/child placement mutations.

`composition_test`, `entitymap_test`, `mapinput_test` and `mapnative_test` exercise
supported/refused structures, original-ink coordinates, immutable publications,
stale lifetimes and warm-cache behavior. The fixture helpers and test methods
specify the admitted combinations; this is not arbitrary master-tree parity.

The recorded live/default/OpenGL comparison reports are not automated oracles and
have been removed. No wire replay, stock callback or test-consumed golden was
removed with them. Historical failures did not become passes: earlier placement
probes found clipping/ownership differences, outline experiments failed before
precision corrections, and no general live-game or non-Windows parity follows.

## Focused input and outline replays

Four small standalone replays cover plane rotation, ordered-filter provenance,
popup release and outline precision/workload: `frames-plane-rotate.txt`,
`frames-filtered-reverse.txt`, `frames-filtered-input.txt` and
`frames-edge-outline.txt`. They reuse `res/`; expected coordinates and sparse stock
pixel samples are explicit in the focused C++ tests, not a case registry.
`../local-relay-world/frames-forward.txt` covers forwarding lifetime separately.

`prepare.py build/focused-input --input` stages `input-world.dm` with five explicit
phase recipes: `plane-rotate`, `filtered-reverse`, `filtered-input`, `edge-outline`
and `forward`. Write a phase name to its `phase.txt`; the server acknowledges READY.
These controls do not preserve the removed broad matrix's combination coverage.

## Reproduction

Requires BYOND and Python/Pillow. Stage a new runtime:

```sh
python -B tests/data/master-tree-world/prepare.py build/master-tree-repro
```

Compile the staged `world.dme` and host on an unused isolated port with
DreamDaemon `-safe -invisible`. `phase.txt` selects the phase; the stock observation
JSON declares corresponding phase names and sample points. `placement-world.dm`
is the separate placement source.

The old native collectors/comparison scripts and duplicated case modules are not
retained. Collect native source pixels and Click receipts manually after READY;
review any new expectation against independent stock output, never Fugue. Keep
raw collection in the disposable runtime, not this fixture tree.

Native captures need stable foreground ownership, source calibration and matching
DPI/resize/reset histories. Source-pixel agreement does not establish whole-window
layout equivalence, general gestures or unsupported effect combinations. Stop only
verified owned processes and leave unrelated sessions/settings untouched.

See [renderer composition](../../../docs/RENDERING.md#composition-and-transforms)
and [rendered-frame input](../../../docs/RENDERING.md#presentation-and-input).

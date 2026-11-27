# Plane-source regression data

Independent BYOND **516.1688** observations for bounded plane-source materials,
filters, grouping and original-ink input. Sources/assets are synthetic. A relay
here is a non-master sampling a plane master's target; local object PASS_MOUSE is
a separate path.

## Automated consumers

`PlaneSourceFixture` and tests in `mapnative_test`, `mapinput_test`, `entitymap_test` and
`compositionplan_test` consume the retained JSON observations:

- `matrix.json`: decoded fields, sparse stock pixels and Click coordinates for
  material/source placement cases, including explicit visual counterexamples.
- `source-coverage.json`: source materials and source-master filter chains.
- `capture-pixels.json`, `precision-model.json`, `master-arithmetic.json`:
  independent precision, alpha and material-boundary samples.

These are stock oracles, not renderer-generated expected results. Input receipts,
original source ink and generated filter pixels have distinct meanings. A passing
Click does not establish pixel parity, menu ownership or arbitrary gestures.
Rows marked as pixel disagreements remain counterexamples, not passing parity.
General affine placement, transformed consumers, attachment input and unmeasured
material/filter products are not established by this corpus.

## Reproduction

Stage a new runtime with:

```sh
python -B tests/data/relay-gate-world/prepare.py build/relay-repro
```

Compile the staged `world.dme`, launch a separate safe/invisible DreamDaemon on an
unused port, and connect one client at a time. Use the case labels in the retained
observations to select `phase.txt`; wait for the matching READY and visible badge
before sampling. The source declares the exact synthetic assignments for each
label; production code does not dispatch on fixture/game names.

The old native observer, its identity-guard self-tests and investigation diary are
removed. Collection is manual. Record client/server versions, source calibration,
DPI, native geometry and reset/resize history. The recorded stock launcher did not
reliably enforce guest identity, so guest flags do not prove account isolation.
Keep identity-bearing logs local and close only processes you started. See
[fixture collection](../../../docs/BUILDING.md#reproducing-fixtures) and
[renderer composition/input](../../../docs/RENDERING.md#composition-and-transforms).

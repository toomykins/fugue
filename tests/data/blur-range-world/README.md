# Stock standalone-blur fixtures

Independent Windows BYOND **516.1688** pixel expectations for
`MapNativeTest::standaloneBlurPixels`. Two synthetic patterns—a centered opaque white
256x256 square and an opaque white left half-plane—use transparent remaining
pixels, black backing and one plane-master blur. Four corner markers calibrate a
**992x672** native-resolution MAP at DPI **144**.

## Retained goldens and boundaries

The **18** `stock-*.png` files cover both patterns at sizes
**4, 6.25, 8, 12.5, 16, 96, 99, 99.5 and 100**. Full viewports preserve transparent
borders and wide edge responses. They are consumed by the native test, not kept
as a UI screenshot archive. Asset generation must not regenerate expectations.

Stock captures at sizes **100/101/104/128/256** were byte-identical for each pattern;
96 and 99 differed. The server retained the requested value, so this establishes
rendering saturation at 100 for these cases, not stored-property clamping or an
arbitrary truncated-kernel approximation.

The source profile uses a bounded short-kernel cascade with precision-preserving
intermediates and scratch reuse. Native default/OpenGL comparisons used a one-LSB
threshold, not exact equality. Earlier Gaussian/truncation/rounding approaches
failed; no wider tolerance was accepted. Standalone and chained blur bounds remain
separate. Large colored/partial-alpha sources, arbitrary chains, other stock
backends/versions and live input are not established by this fixture. No GPU-time
or FPS benchmark follows from allocation/pass counts.

Unconsumed capture summaries and process records have been removed. The stock
images and their automated expectations remain unchanged.

## Reproduction

1. Copy `world.dm`, `world.dme`, `world.dmf` and `assets.py` to a new directory under
   `build/`; run `python -B assets.py` there (Pillow required).
2. Compile with BYOND 516.1688 and launch a separately owned DreamDaemon on an
   unused port with `-safe -invisible`; connect a separate stock client.
3. Write `unique-token|square|96` to the runtime's `phase.txt`; `edge` selects the
   other pattern. `events.log` acknowledges requested and stored sizes.
4. Wait for phase acknowledgement and at least 0.7 seconds settling, then require
   three identical captures at 0.25-second intervals. Verify process/foreground
   ownership, stable geometry, all corner markers and the calibrated MAP crop.
5. Start/end at size zero, compare with generated source-over-black controls and
   repeat sizes in reverse order to detect stale/history effects. Stop only owned
   processes; do not resize/refit between phases.

Fractional phases use the same manual procedure; no native capture helper is
retained.

The original stock graphics API was not independently instrumented. A successful
new capture is not permission to overwrite a golden without reviewing its source,
state/history and comparability. See
[renderer composition](../../../docs/RENDERING.md#composition-and-transforms).

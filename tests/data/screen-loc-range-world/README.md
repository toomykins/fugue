# Stock screen-range differential corpus (516.1688)

This fixture replaces the coarse TOP/RIGHT-range refusal with measured endpoint
modes. It is not a complete screen_loc implementation or a cross-version promise.
The stock reference describes TOP/RIGHT as control-edge alignment, not compass
coordinates, and warns against using them in ranges:
https://www.byond.com/docs/ref/#/atom/movable/var/screen_loc
We reproduce the observed behavior in our bounded subset rather than normalizing
those keywords or hiding every such HUD placement.

## Independent oracle

The handwritten DM world receives strings from `cases.json`. It displays a centered
8x8 green marker in an otherwise transparent **32x32 icon**, over blue terrain.
Each phase settles for six 20-fps ticks; the server then waits for a file ack before
changing anything. Recorded collection took two identical owned-window captures
with stable measured geometry and verified process/foreground ownership before
acknowledging. The old native collector is not retained; fresh collection is manual.

`generate.py` produces 1,600 cases, not expected answers:

- 1,024 corner-to-corner axis strings: both spellings and both axis orders;
- 128 bare/mixed corner shorthand ranges;
- 256 further corner pairs on an even 8x6 view;
- 128 signed tile-offset and 64 equal ascending pixel-offset cases.

`generate-modes.py` adds 288 endpoint-mode/reversal cases. `large-pixel-cases.json`
adds 16 translation/boundary observations that expose otherwise clipped copies.
The blue MAP bounds establish scale/origin independently of the implementation.
`export.py` extracts integer logical marker origins from the screenshots; it does
not call or reimplement the parser. The maximum rounding residual was 0.137 pixels.
All measured markers were fully visible or fully clipped, not partial edge samples.

`stock-positions.json` contains **1,904** observations. **1,902** are admitted corpus
rows; two explicitly tagged reversed pixel-offset cases remain outside the existing
subset. These are retained gaps, not parity passes. The stock version, marker
geometry and measurement method are recorded here; machine-specific capture paths
are not distributed. Fixtures use no game assets.

## Verified rule

Each axis retains its tile-coordinate expression and alignment mode separately:

- EAST/NORTH contribute view-dependent tile endpoints; RIGHT/TOP have local tile
  coordinate 1 and a far-edge alignment. LEFT/BOTTOM have near-edge alignment.
- Far-edge start -> tile-coordinate end is directed in local coordinates. An empty
  interval produces no copies; it is not a blanket unsupported-syntax rejection.
- Far-edge start -> aligned end sorts local endpoints but retains the first far
  anchor. Otherwise the lower local endpoint owns the origin; ties retain first.
- Only after selecting the origin/count is the edge translation applied. Far-edge
  repetition clips to the unchanged viewport rather than expanding it.

Examples: `WEST,SOUTH TO EAST,TOP` is a bottom row; `RIGHT,NORTH TO WEST,SOUTH` is a
right column; `TOPLEFT TO BOTTOMRIGHT` is one top-left copy. Full-range work limits
include clipped copies: a 272-copy request is not admitted just because only one
column would be visible. Unequal/reversed pixel-offset grids, CENTER/fractional
ranges, automatic view expansion, arbitrary icon dimensions/control-edge zoom
alignment and other unverified grammar combinations remain gaps. The verified
corpus uses 7x5/8x6 views, 32-pixel tiles/icons and Normal presentation.

## Reproduction

```
python -B tests/data/screen-loc-range-world/generate.py <cases.json>
python -B tests/data/screen-loc-range-world/generate-modes.py <modes.json>
python -B tests/data/screen-loc-range-world/prepare.py <world-directory> <cases.json>
```

Compile staged `world.dme` and launch a separate server on an unused port with
`-safe -invisible -logself`. For fresh manual collection, wait for each phase,
record calibrated marker origins and stable geometry, then supply the world's
file acknowledgement. See [collection guidance](../../../docs/BUILDING.md#reproducing-fixtures).

The remaining offline helpers operate on already collected marker observations:

```
python -B tests/data/screen-loc-range-world/export.py <stock-positions.json> <stock-observations.json> ...
python -B tests/data/screen-loc-range-world/select-native.py <native-cases.json>
```

Their input schema is documented by the converters, not a retained client runner.
The native sample has 254 admitted cases and two exclusions. Recorded comparison
matched all 254 admitted cases; the two exclusions differ and are not parity passes. Every admitted corpus
row also runs in `EntityMapTest::screenRangeStockCorpus`.

## Evidence and interruptions

- `build/screen-range-matrix/stock.json`: first 866 stable cases. Capture 867 was
  discarded after foreground/geometry changed while the user interacted with the
  desktop. The client was stopped; this initial failure was not retried silently.
- User-authorized continuation: `build/screen-range-matrix-resume/stock.json`, the
  remaining 734 cases, starting again at the interrupted case. No observations from
  the first segment were overwritten.
- Further stock evidence: `build/screen-range-modes/`,
  `build/screen-range-large-pixels/`. Final native comparison:
  `build/screen-range-native/fugue.comparison.json` (254 passes, two explicit gaps).
- Discovery: `build/screen-range-discovery/` contains 28 stock/before-client cases
  and a temporary guest wire log. All 28 original strings occur unchanged in
  plaintext 0x0026 appearance records (`wire-summary.json`). Temporary wire logging
  was removed from the client afterward. No packed-flag guesses were needed.
- An exploratory `build/screen-range-shift/` tried instance pixel offsets to expose
  clipped copies; stock did not shift these HUD markers that way. Those 24 samples
  are not oracle rows or parity passes. The maintained large-offset evidence uses
  screen_loc offsets instead. Hypotheses that missed first two, then eighteen cases
  were refined with additional mode probes, not encoded as per-string exceptions.
- The initial corpus regression had 1,104 failures before the implementation. A
  later worker test mistakenly expected 35 *published* sprites instead of five
  after existing off-view culling; that test expectation was corrected, not the
  renderer. Detailed run paths and final validation are in `docs/RENDERING.md`.

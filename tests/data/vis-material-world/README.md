# Vis materials and lighting surfaces

Independent DreamSeeker/DreamDaemon 516.1688, port 18439, 2026-11-02.
No game assets. `prepare.py` generates eight deliberately different RGBA patches;
`stock.json` retains actual stock center samples and captured wire material/filter
fields. `mapnative_test::visMaterialPixels` consumes all samples on default/OpenGL,
through worker publication and normal QRhi, with ≤1 channel tolerance and zero
warm texture uploads. `viscontents_test` checks admission, immutable publications,
matrix/reset tree refusal, finite values and exact filter envelopes.
`compositionplan_test` checks source ordering, absent/ambiguous masks, cycle cuts
and backend/material refusals.

## Stock conclusions

- Turf holders do not tint their vis copies. Object holders' scalar RGBA applies
  before matrix clamping; RESET_COLOR/ALPHA clears inheritance, not own material.
- RGB at source alpha zero is cleared; partial-alpha RGB remains straight.
  The fixture's matrix generates opaque/half-alpha output even from empty texels.
- Childless KEEP_TOGETHER masters capture and relay normally.
- Alpha filters use the source alpha, not its RGB. Inversion is 1−alpha.
  A nonexistent target supplies zero alpha. Sources can be on later planes.
- RGB-space color filters run in order, clamping at each step: +0.125 followed
  by −0.125 does not recover clipped highlights. Additional cross-channel
  RGB→alpha / white-output and subsequent opaque-alpha cases confirm alpha
  replacement, half-alpha inputs and clearing hidden RGB between operations.
- Wire: reset sample flags 0x01c03004; alpha type 7 / mask 0x32c,
  zero x/y, flags 0/1, absent icon and named target; color type 9 / mask 0x420,
  zero two-byte RGB-space selector, twenty little-endian floats. Captured record
  flags are 1 (and 2 for the subsequent color filter), not DM constants.

## Provenance / reproduction

Final oracle: `build/probes/20261102T001801Z-compare-7dae158c/comparison.json`.
18 phases, 8 interior samples each. All paired samples differ by at most one
channel level after the clamp correction; stock samples are unchanged. Final
source/readiness rerun: `20261102T002217Z-compare-1e9d5def`, independently checked
against every retained sample (`build/vis-material-probe/final-pixel-check.json`). Physical clients 672×700, DPI 144; MAP source
224×224, 3× pixels with window-relative origin (11,46). Samples are recorded in
both coordinate systems. Both clients restore/resize once, reset, then keep
geometry; no fit/zoom. Each phase waits for a fresh fixture acknowledgement after
two ticks and a skin query. The uniform patch interiors avoid edge/resampling
ambiguity; this is not full-image or general filtered-input parity.

Stock uses `-new -guest` with existing global settings, Fugue a fresh state.
The compare runner records binary/DMB/source hashes, geometry, readiness tokens
and cleanup. Wire-only guests independently collected `build/vis-material-probe/
wire-{reset,reset-half,capture,mask-inverse-color}.txt`. The retained fields are
subsets of those records, not guessed packed flags.

Stage in a **new** build directory:
`python -B tests/data/vis-material-world/prepare.py build/<run>`.
Compile `world.dme`; check an unused non-1337 port and launch an owned safe/invisible
DreamDaemon. Connect stock and Fugue sequentially. Use the phases in `stock.json`
and fresh `probe-request.json` tokens: first resize/reset, then keep geometry and
measure. Record versions, DPI and readiness replies; close only processes you
started. See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

Earlier failed evidence remains local: first fixture assigned alpha after color,
which rewrote its matrix; corrected before retaining the oracle. The first mask
fixture leaked objects across sequential guests, invalidating mask comparisons;
explicit client-owned cleanup fixed that before the final capture. Early
material-only code stayed overbright live. The final fixture additionally caught
the 12-level object-holder clamp-order mismatch; retained stock samples were not
changed to match the renderer. No screenshot archives are retained in Git.

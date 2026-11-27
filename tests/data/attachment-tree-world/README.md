# HUD vis and anonymous appearance groups

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-02, port 18441.
No game assets or screenshots in Git. `prepare.py` generates four 32×32 solid
RGBA source tiles. `stock.json` retains **42 phases × 24 stock pixel samples** and
two stock Click callbacks. `mapnative_test::attachmentTreePixels` consumes every phase
on default/OpenGL through worker publication and the normal mailbox/QRhi path,
with ≤1 channel tolerance and zero warm texture uploads during movement and
sampled grouped offset/alpha animation. `viscontents_test` and
`compositionplan_test` cover ordering, sharing, lifecycle, work/depth/cycle bounds,
immutable publications and unsupported input/transition rejection.

## Independent conclusions

- HUD vis children draw at screen placement, independently of world position/LOS;
  the world-holder control produces the same pixels. Parent object instance pixel
  offsets affect the world occurrence but not the HUD vis origin. Child instance
  offsets apply to both. Captured appearance JSON excludes these object offsets;
  ordinary overlay offsets remain in appearance fields.
- Ungrouped scalar/matrix/reset/blend inheritance extends through ordinary trees
  and nested vis holders. Additive holder material is inherited by default-blend
  children; nested finite matrices are not flattened at an extra clamp boundary.
- KEEP_TOGETHER applies material once after its contents. Alpha, tint, matrix,
  normal/add/subtract/multiply, underlays, child layers and nested groups are
  distinguished by overlapping blue/red/green tiles.
- KEEP_APART escapes the nearest group but inherits its material. Resets outside
  the group can remove that inheritance; resets inside cannot remove the enclosing
  material. Detached branches inherit the group's matrix about their **own** pivot.
- Group transforms include translation, half scale, quarter turn, reflection and
  shear. Iconless groups use the tile pivot. Nested transformed groups and
  transformed/alpha groups are sampled independently; material precedes resampling.
- Ungrouped HUD child Clicks name the child with null world location. Root ink and
  an extended ordinary overlay report root icon coordinates `(17,20)` / `(37,20)`;
  screen coordinates are `3:17,3:20` / `4:5,3:20`. No grouped/affine input claim.

The runtime wire-only `wire-group.txt` and `wire-offset.txt` corroborate packed
flags/placement: group child `0x04003004`, nested KEEP_TOGETHER|KEEP_APART overlay
`0x0c000004`, mouse-zero holder `0x3204`; blend is vars bit 0x100, relative plane
−32767. Existing independently recovered reset and transform encoding is reused.
No packed flag is inferred by shifting a DM constant.

## Provenance and readiness

Artifacts are under `build/probes/` (not retained in Git):

- Base 30-phase final source pairing: `20261102T011431Z-compare-c467cad7`.
- Additional ordering/world-offset/input controls: `20261102T011511Z-compare-f499b494`.
- Twelve-phase overlap/transform follow-up (six repeated controls):
  `20261102T012119Z-compare-c31a416a`. Repeated stock samples are unchanged.
- Stock Click run `20261102T011615Z-5aa7920b`; matching Fugue callbacks
  `20261102T011605Z-74873c77`. `click-check.json` retains identity/readiness/capture.

All retained stock samples differ from their paired Fugue samples by at most one
channel level. This is selected-pixel parity, not full-image/game parity. Physical
clients 672×700, DPI 144, MAP source 224×224 at 3× scale, window-relative origin
(11,46); samples use each source pixel's interior. Each role restores/resizes once,
then keeps geometry, with no fit/zoom. Fresh fixture acknowledgements follow two
ticks and a skin-size query. Stock uses `-new -guest` with retained global settings;
Fugue used a fresh state directory. Recorded comparisons pinned binary/DMB/source
hashes, geometry, request tokens and cleanup. Click checks use the same geometry
and verified foreground ownership. Only owned processes were stopped.

## Reproduce

Stage in a **new** build directory:

```
python -B tests/data/attachment-tree-world/prepare.py build/<new-run>
```

Compile `world.dme`, check an unused port, then launch a separate safe/invisible
DreamDaemon. Connect stock and Fugue sequentially with isolated client state.
Use the phases in `stock.json` and fresh `probe-request.json` tokens: first
resize/reset, then keep geometry and measure. Every phase resets its own data,
so names are independently repeatable. The `input` phase also records Clicks in
`clicks.jsonl`; click source pixels (80,140)/(100,140) with the owned foreground
window and compare retained callback fields. Record DPI/history and close only
processes you started. See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

## Re-measured 2026-11-23 (516.1689)

`group-under-inner-add-half` (a BLEND_ADD underlay of an additive KEEP_TOGETHER root) depends on what stock showed
before it: reached from `group-inner-add-half` it keeps that phase's picture, which the first oracle recorded; in a
fresh world the underlay is under the root's icon. `stock.json` now holds the fresh-world samples, which agree with
`tests/data/stock-parity-world` group-blend-add (layer ties keep placement order, BLEND_ADD included).

## Failures and limits retained

Early fixture compilation failed on mutable-appearance construction, later on an
untyped Click argument; readiness initially counted clients incorrectly. Both
were fixed before the retained oracles. The first click reader failed on a blank
line after one valid stock callback; the corrected repeat is recorded above.
Early renderer tests exposed empty-chain sampling, pending-resource retry and old
flattened/refusal assumptions. Native offset replay initially encoded object
pixel fields as appearance fields; wire evidence corrected the adapter, **not**
the stock samples. Stock probes caught lost transformed-group alpha and missing
KEEP_APART matrix inheritance; production behavior was corrected, not whitelisted
around those cases.

Grouped/transformed/animated vis holders, cross-plane group escapes, named/filter
child captures, other vis flags, mob/turf children, grouped/affine input and vis
hover/drag remain outside the supported contract. Active interpolation reuses
previously verified animation primitives; the new static stock phases do not
claim arbitrary easing, changing color matrices or structural transitions.

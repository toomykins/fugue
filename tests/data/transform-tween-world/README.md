# Transform tweens

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18505 (`-params set=a` and `set=b`).
No game assets or screenshots in Git. `prepare.py` writes the world into a separate runtime directory with a
32×32 "arm" (red east arm, green north arm, blue west dot) and the clock markers. `stock.json` keeps, for
both sets, the cases (start matrix, one or two steps with their flags) and **164 stock frames** of the
measured affine matrix of every case. `entitymap_test::transformTweenMatchesStock` composes each case
through `animation::composeChain` with per-step flags and checks every sample (linear part ≤ 0.1,
translation ≤ 0.6 px; samples with |det| < 0.35 are skipped because a nearly flat image can't be measured).

## Method

Each case animates `transform` from its start matrix to a target over 20 s (two-step cases spend 10 s per
step), then snaps back, looping. A linear `pixel_x` marker on the bottom row is the clock. The three colour
features have known centroids in icon space (red (8,0), green (0,6), blue (−10,0) from the centre), so the
weighted centroids in a frame give the whole affine matrix (`extract.py`). Measurement noise is about 0.03
in the linear part and 0.3 px in translation; the candidate rules differ by 0.2 or more mid-tween.
`model.py` is the rule `shell/animation.h` implements, used to score captures.

## Independent conclusions

- The linear part is decomposed as rotation × [[sx, k], [0, sy]], with the angle taken from the first
  column, and angle, scales and shear interpolate separately; translation interpolates linearly. Turns of
  90°, 135°, 179°, 181°, −179°, a 90° turn with 2× scale, a 45° turn with unequal scale, and a turn with a
  32 px translation all fit this (median 0.03–0.05). A polar decomposition fits the unequal-scale case
  clearly worse (0.11).
- The angle goes the shorter way: `Turn(270)` and `Turn(-90)` both turn 90° anticlockwise.
- A change of handedness rides on sy: an axis swap `matrix(0,1,0,1,0,0)` and `matrix(0,-1,0,-1,0,0)` rotate
  90° while flattening through the middle.
- An exact half turn between the two poses, by the column angle, is interpolated component-wise (it shrinks
  through zero): `Turn(180)`, `matrix(-1,0,0,0,-1,0)`, half-scale and unequal-scale half turns, mirrors
  (`matrix(-1,0,0,0,1,0)`, ×2, ×½), and 90°→270° and 45°→225° from a turned start.
- With a column half turn but a sheared row, `matrix(-1,0.05,0,0,-1,0)` turns clockwise through −180°,
  while `matrix(-1,0,0,0.05,-1,0)` (column angle 177°) turns anticlockwise. Fugue models this as: on a column
  half turn, the row angle atan2(−b, a) picks the direction, and only when that is a half turn too does the
  tween go component-wise. This fits every case here; other ways of breaking the tie were not probed.
- `ANIMATION_LINEAR_TRANSFORM` makes its own step interpolate component-wise. In two-step chains with the
  flag on only one step, the other step still rotates (per-step fit median 0.04 versus 0.06–0.08 and
  0.24 at p90 when the flag is applied to the whole chain, which Fugue used to do).

Rebuilt Fugue on both sets sits within a median 0.03–0.05 of the rule on every case (worst samples 0.1–0.17,
from nearest-neighbour sampling of the thin arms).

## Not covered

Transforms on images, overlays and vis contents, parallel transform steps, and easing on transform steps
(they share `ease`) are not sampled here.

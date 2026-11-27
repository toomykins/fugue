# Colour-matrix tweens

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18640. No game assets or screenshots in Git.
`prepare.py <dir>` reuses `transform-tween-world`'s clock markers and skin and adds a grey (100,100,100) box. Boxes
animate their colour over the same 20 s loop as a map clock marker (bottom row), so each grab pairs colour and time.
`stock.json` keeps 10 stock frames of every case. `entitymap_test::colorTweensMatchStock` samples each tween at the
stock clock readings and computes the grey through Fugue's sprite colour.

## Independent conclusions

- A colour-matrix tween blends every matrix element linearly by the eased factor (identity to M1, M1 to M2, with
  SINE_EASING too).
- A plain colour tweens as its diagonal matrix, to and from a matrix.
- Setting a colour matrix over a plain colour leaves the old RGB on the wire next to the matrix; stock draws the
  matrix alone (`static-plain-then-m1`: 200,50,100, not 200,25,25).

## Fugue after the change

Fugue snapped colour matrices at the halfway point and multiplied a matrix by a stale plain colour. It now blends in
matrix space (parallel steps and end corrections add their change relative to their base) and drops the stale RGB
(and, since 516.1689 stock-parity-world color-matrix-alpha, the stale alpha). Every sample matches stock within 2 levels.

## Not covered

Parallel colour steps (blended additively; not probed), colour tweens on plane masters and HUD trees.

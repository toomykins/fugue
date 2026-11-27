# Static transforms: sampling, children and maptext

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18507 (`-params set=a|b|c`).
No game assets or screenshots in Git. `prepare.py` writes the world into a separate runtime directory with a
quartered 32×32 icon (with a dotted top row), a 16×16 and a 48×48 icon. Stock and Fugue were connected to
the same server and grabbed at native 1:1 resolution, so crops compare pixel for pixel. `stock.json` keeps,
per case, the number of blended (non-palette) pixels in the stock 96×96 crop and how far rebuilt Fugue's
crop is from stock, plus the per-icon sampling verdicts read from paired cases.
`entitymap_test::transformSamplingMatchesStock` builds each verdict case and checks the root's and the
overlay's `smooth` decision.

## Independent conclusions

**Sampling (A6).** Without `PIXEL_SCALE`, stock samples every scaled or transformed icon bilinearly in
straight RGBA: 45°, 2×, 1.5×, 0.75×, ½×, a half-pixel translation, shear and a scaled 30° turn all show
blended edges (the dotted row turns into a grey band at 2×). With `PIXEL_SCALE` (packed bit `0x40000000`,
DM bit 9 at `0x200000 << 9`) all of them, ½× included, are nearest-neighbour: Fugue's nearest path matched
them exactly before this change, and its old ½×-only bilinear path matched the unflagged ½× case.

**PIXEL_SCALE is per icon.** A flagged parent's unflagged overlay is still smoothed, and a flagged overlay on
an unflagged parent stays sharp, for 2× and 30°. An image follows its own flag, not its target's: a flagged
target with a plain image has a sharp target and a soft image, and the reverse. A `RESET_TRANSFORM` overlay
with its own scale under a flagged parent is smoothed.

**Child composition (A4).** Overlays of 16 and 48 px under a 90° parent, a 48 px overlay with pixel offsets
under a 0.75× parent with its own pixel offset, a three-deep chain with turns and scales at each level,
`RESET_TRANSFORM`, an overlay with its own 90° turn under a 2×1 parent, and offset overlays and underlays
under a turned parent all match stock within one channel level once sampling matches: parent × own about
each part's own centre, pixel offsets outside the inherited matrix.

**Maptext under transforms (A5).** Maptext turns, scales, shifts and keeps `maptext_x/y` and a wide
`maptext_width` box with the atom, pivoting where Fugue pivots it; text-pixel centroids agree within about a
pixel. The remaining differences are glyph rasterisation, which the untransformed `mt-plain` control shows
just as much; they are not transform behaviour.

After this change every set-a and set-c sampling case and every set-b child case is within one channel
level of stock (`fugueMaxDiff` ≤ 1).

## Still different

- `img-*` (set c): a 16×16 image on a turned target. Stock draws it turned with its target; Fugue matches since the
  image-tree-transform-world change (it used to refuse images whose parts were not 32×32).
- `mt-*`: glyph rasterisation, as above.

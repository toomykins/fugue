# Atom filters

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18610. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and two solid icons (a 16 px box with a translucent step on its
right, and a small 8 px box). `tools/stockprobe/stockprobe.py pair` runs dd with `-params set=<a|b>`, stock and Fugue on the
same server and grabs both at 1:1; `compare.py <dir> a b` reads which parts each filter reaches from probe pixels and
Fugue's difference from stock per 96 px case crop. `stock.json` keeps both. `entitymap_test::atomFiltersReachLikeStock`
checks the reach rules below against it and that the plan runs the stock outline pass.

## Independent conclusions

- Stock draws an atom's filters (outline at sizes 1 and 2, OUTLINE_SHARP, OUTLINE_SQUARE, translucent colours,
  drop_shadow with and without blur and `offset` growth, blur, chains), on the atom wherever it is drawn, including a
  vis copy (vg's bar sign puts its drop_shadow on one).
- A filter covers the atom's **own icon only**: its overlays and underlays are drawn around it unfiltered. With
  KEEP_TOGETHER the filter covers the whole group. An overlay's own filters filter that overlay. An image on a filtered
  target is not filtered.
- The filter also covers the atom's maptext, and an image's own filters filter the image (Fugue does both since
  the filter-reach-world change).
- With a transform, the outline follows the turned shape (the filter runs before the matrix: see
  `transform-group-world`).

## Fugue after the change

An atom's or overlay's own filters run on a surface of that part through the same stock-profile passes as plane-master
filters. Blur, outline and drop_shadow were compared; alpha-mask and colour filter records take the same path untested. Every case except the four below
matches stock within one channel level (set a's bar-sign shadow) or exactly. Filtered atoms stay clickable on their own
icon ink; the filter's ink is not hit (not checked against stock). Float surfaces are required; without them atoms draw
unfiltered as before.

## Not covered (drawn unfiltered, as before)

`image` (an image's own filters, A8), `maptext` (falls back to the unfiltered tree), `wave-shadow` (wave is not
decoded: only the shadow is drawn); also animating atoms and flicks. `turn` matches stock since the
transform-group-world change.

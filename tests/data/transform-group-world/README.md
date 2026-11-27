# Transforms with group effects

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18620. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and the atom-filter fixture's two solid icons (a 16 px box with a
translucent step on its right and a white corner, and a small 8 px box). `tools/stockprobe/stockprobe.py pair` runs dd with
`-params set=<a|b>`, stock and Fugue on the same server and grabs both at 1:1; `compare.py <dir> a b` reads probe
facts from the stock grab and Fugue's difference from stock per 96 px case crop. `stock.json` keeps both.
`entitymap_test::transformedGroupsLikeStock` consumes it.

## Independent conclusions

- **Filter first, then the matrix.** An atom's filters run on its untransformed surface and its transform then maps
  the filtered surface: a 1 px outline under `Scale(2,1)` is 2 px wide on the stretched sides and 1 px on the others
  (`outline-scale`, `outline-scale-x`), a drop_shadow at x=6 under `Turn(90)` lands below the box (`shadow-turn`), a
  shadow offset doubles under a 2x stretch (`shadow-scale`), and blur stretches with the atom (`blur-scale`).
- **KEEP_TOGETHER: the group surface takes the matrix**, about the root icon's pivot, so an overlay 14 px right of the
  root swings with the group (`kt-turn`, `kt-scale`, `kt-pixel-turn`, `kt-underlay-turn`); without KEEP_TOGETHER each
  overlay turns about its own pivot (`turn-overlay`). Filters on a KEEP_TOGETHER root cover the group before the
  matrix (`kt-outline-turn`, `kt-outline-45`); alpha applies once to the turned group (`kt-alpha-turn`).
- Filters on a turned root without KEEP_TOGETHER still cover the root's own icon only; its overlays turn unfiltered
  (`outline-turn-overlay`). An overlay's own filters under a turned root (`overlay-own-turn`) or with its own matrix
  (`overlay-own-scale`) also run before that overlay's matrix.
- KEEP_APART leaves a turned KEEP_TOGETHER group and then turns about its own pivot (`ka-in-kt-turn`); on a turned
  root without KEEP_TOGETHER it changes nothing (`ka-turn`). A KEEP_TOGETHER overlay under a turned root turns as one
  surface about the overlay's pivot (`kt-child-turn`).

## Fugue after the change

The decoder no longer refuses a matrix combined with filters or KEEP_APART. Ordinary appearance trees compose a
parent's matrix into overlays and underlays about each part's own pivot (as vis copies already did), except inside a
KEEP_TOGETHER surface, which carries the matrix itself. A filter-only group now takes the part's matrix after its
filter passes; the composition plan runs the filters on the untransformed group surface. Trees with a nested
KEEP_TOGETHER take the group path. Every case matches stock exactly or within one channel level except:

- `kt-45`: a plain KEEP_TOGETHER group turned 45 degrees differs on 34 edge pixels (max 86): sub-pixel placement of
  the bilinear group surface. It matched no better before the change and `kt-outline-45` matches.
- `kt-overlay-own-matrix`: 8 pixels within 10 levels.

## Not covered

render_source and render_target with a matrix (still refused), PLANE_MASTER trees with a matrix, animating
transforms with filters (the sampled path draws them unfiltered), clicks on turned filter groups (not checked
against stock).

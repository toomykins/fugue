# Image trees and image matrices

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18623. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and two solid icons: the image is a 32 px icon with a 16 px yellow
box (a white corner shows its turn, a translucent step its right side), targets and overlays a small 8 px blue box.
`tools/stockprobe/stockprobe.py pair` runs dd, stock and Fugue on the same server and grabs both at 1:1; `compare.py <dir> a`
reads ink bounding boxes from the stock grab and Fugue's difference per 96 px case crop. `stock.json` keeps both.
`entitymap_test::imageTreesTransformLikeStock` consumes it.

## Independent conclusions

- An image's own transform draws, about the image's own centre, with its pixel offsets outside the matrix
  (`own-turn`, `own-scale`, `own-turn-pixel`).
- With a transformed target, the image's own matrix applies first and the target's after it, as for an overlay:
  Turn(90) on a Scale(2,1) target is wide, Scale(2,1) on a Turn(90) target is tall. RESET_TRANSFORM on the image drops
  the target's matrix and keeps its own (`own-reset`).
- Image maptext on a turned target turns with it about the image icon's centre, as atom maptext does
  (`maptext-turn`, `maptext-only-turn`, `maptext-own-turn`).
- An image's overlays and underlays on a transformed target each take the matrix about their own pivot, like an
  ordinary atom's (`overlay-turn`, `underlay-turn`, `overlay-scale`, `overlay-own-turn`); an overlay with
  RESET_TRANSFORM stays upright (`overlay-reset`); the target's own overlays (here 16 px) don't move the image
  (`overlay-target-overlay`).

## Fugue after the change

Images expand through the ordinary tree with the target's matrix at the bottom of the matrix stack, so every image
part composes it about its own pivot; the image-owned transform refusal, the image-leaf and image-maptext refusals on
transformed targets, and the 32 px part check on target overlays are gone. Every case matches stock exactly or within
one channel level, except the three maptext cases, whose 74 differing pixels are glyph rasterization (the same count
as untransformed maptext).

## Not covered

Underlays on the target of a transformed image (still refused), target maptext with a transformed image, image
KEEP_TOGETHER/PLANE_MASTER/render_target (still refused), animating image matrices, clicks.

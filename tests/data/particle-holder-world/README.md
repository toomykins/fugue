# Particles and their holder

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-23, port 18713. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and solid icons. Every set holds one still particle, so one grab
of stock and one of Fugue on the same server compare 1:1 (96 px crops around each case of the 3-tile grid).

## Independent conclusions

- A set draws on an obj, a mob and a screen object alike (`obj`, `mob`, `screen`), with or without a holder icon.
- The holder's `transform` scales and turns its particles about the holder's centre (`holder-scale2`,
  `holder-turn90`); its `color` and `alpha` multiply theirs (`holder-color`, `holder-alpha`, `holder-kt`); its pixel
  offsets move them (`holder-pixel`). They draw at the holder's layer (`holder-layer`, `holder-layer-over`).
- A particle that is scaled or turned is cut at the set's `width` x `height` box like an unturned one
  (`small-edge-scale`, `small-edge-rot`); one wholly outside is not drawn (`small-out`).

## Fugue after the change

Identical: `obj`, `screen`, `mob`, `holder-turn90`, `holder-color`, `holder-pixel`, `p-scale3`, `p-rot45`, `p-rot90`,
`p-grow`, `small-edge`, `small-out`, `holder-layer`, `holder-layer-over`. The rest differ at edges only, except as
listed below. Before, the holder's transform, colour and alpha were not applied, a screen object's set was not drawn,
and turned or scaled particles were neither filtered nor cut.

## Not covered

- The edge pixels of a particle scaled by its holder, by the set's transform, or cut at the box while scaled
  (`holder-scale2`, `p-transform`, `small-edge-scale`, `small-edge-rot`).
- A translucent holder and its particles are one surface in stock: where they overlap the holder does not show
  through (`holder-alpha`, `holder-kt`). Fugue blends the particle over the holder.
- A holder that is itself animating its transform or colour; a holder in `vis_contents` with a transform.

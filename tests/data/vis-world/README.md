# vis_contents members and vis_flags

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18600. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and solid-colour icons (two 4-dir DMIs drawn with a white notch
on the facing edge). `tools/stockprobe/stockprobe.py pair` runs dd with `-params set=<a|b|c|d|e>`, stock and Fugue on the same
server and grabs both at 1:1; `compare.py <dir> a b c d` reads fixed probe pixels on each case tile and prints the facts
for both clients. `stock.json` keeps the stock facts. `entitymap_test::visFlagsMatchStock` and
`visTurfAndMobMembersMatchStock` rebuild every case with the same icons, composite Fugue's sprites in draw order and
read the same probes.

Cases sit on a 3-tile grid. The holder is a 24 px blue box at layer 5 on a layer-2 floor; members are 16 px boxes.

## Turfs and mobs as members (sets a and d)

Set a keeps the remote turfs on the top row of the same level, set d on level 2 (out of view); both give the same
pixels.

- A **turf** member draws the turf's own appearance at the holder's position (its pixel offset included), then its
  **contents** (objs and mobs standing on it) at their own offsets from it, then the turf's own **vis_contents**.
  Each copy keeps its own layer and plane, so a layer-3 obj on the remote turf draws under the layer-5 holder, and a
  layer-6 turf over it.
- At an equal layer a copy is ordered by **where its source stands**, any level: a remote turf north of the holder
  draws under the holder's floor (both layer 2), one south of it over the floor; a layer-2 obj located north is
  hidden by the floor, one located south or in nullspace is not. A nullspace source sorts by its own offset like any
  atom.
- A **mob** member draws like an obj member, and a **mob holder** draws its vis_contents (vg's typing indicator is an
  obj in a mob's vis_contents), also with VIS_INHERIT_ID|PLANE|LAYER.

## vis_flags (set b)

The kid sticks out down-right of the holder; a layer-4 wall bar crosses it.

| flag | stock |
|---|---|
| VIS_INHERIT_ICON | the holder's icon with the kid's own state |
| VIS_INHERIT_ICON_STATE | the kid's icon with the holder's state |
| VIS_INHERIT_DIR | the holder's direction |
| VIS_INHERIT_LAYER | the holder's layer (a tie with the holder draws after it) |
| VIS_INHERIT_PLANE | the holder's plane |
| VIS_UNDERLAY | only matters at a tie with the holder's layer (FLOAT_LAYER or INHERIT_LAYER): drawn before the holder; with an explicit layer of its own it sorts by that layer |
| VIS_HIDE | the member and everything under it are not drawn |
| FLOAT_LAYER, no flags | the holder's layer, after the holder |

`all-inherit` (63) draws the holder's look at the kid's offset over the holder.

## Transformed holders (set c)

`compare.py` records where each box lands (centroid and size), which way its notch points and whether the kid's
edges are blended.

- A vis copy under a transformed holder takes the holder's matrix **about the copy's own centre**; its pixel offset is
  not transformed. A turned holder's kid sits at the same offset, turned in place; a 2x holder's kid is 2x about its
  own centre; a translation matrix moves the kid by the same amount. This is the overlay rule (static-transform-world).
- The kid's own transform composes after the holder's (holder × own); RESET_TRANSFORM drops the holder's.
- Nested members compose down the chain, and a turf member's contents are turned about their own centres too.
- Sampling follows each copy's own PIXEL_SCALE: a 30° kid is blended unless the kid has PIXEL_SCALE; the holder's flag
  does not reach it.

## Underlays, floats and animation (set e)

`stock.json` set e counts kid, holder and notch ink per case and keeps Fugue's difference after the change.

- A **turf holder** lends its own layer (2) to FLOAT_LAYER and VIS_INHERIT_LAYER copies, so a float kid draws over
  the floor (`turf-float`). A VIS_UNDERLAY copy at that layer draws **before the turf**: the opaque floor hides it, and
  a layer-2 obj on the same turf still shows (`turf-underlay-float`, `turf-underlay-inherit`, `turf-underlay-obj`).
- A VIS_UNDERLAY member of a **member** draws just before that member and after the holder: the kid's notch shows
  over the grandchild, which it covers without the flag (`nested-underlay`, `nested-float`). It sorts where its
  parent copy does, even when it reaches into the cell below.
- **Animating members** keep their vis_flags mid-animation: VIS_INHERIT_LAYER, VIS_INHERIT_ID with an alpha tween,
  VIS_UNDERLAY and VIS_INHERIT_ICON|DIR (`anim-kid-*`). An animating holder's turn, scale or pixel offset carries its
  copies, as for a settled holder (`anim-holder-*`).

## Fugue after the change

Every probe of sets a, b, c and d agrees with stock; the 1:1 grabs agree pixel for pixel inside the MAP for sets a, b
and d, and within one channel level for set c. `entitymap_test::visUnderTransformedHolderMatchesStock` covers set c
(geometry from an unsmoothed composite, sampling from the sprite's flag), `visLeftoversMatchStock` set e. Every static
set e case matches stock pixel for pixel; the animated ones agree in what is drawn, their differences being animation
phase (the clients are grabbed about a second apart). Transformed copies are not hit-tested.
Turf copies and everything reached through a turf member are drawn but never hit: stock clicks on them are not
established. A mob member is clicked as that mob, like an obj member, which is not checked against stock either.

## Not covered

Clicks on turf, mob, inherited and transformed copies; animated members and holders paired by a clock reading (set e
grabs are not); VIS_UNDERLAY with an explicit layer under a turf holder.

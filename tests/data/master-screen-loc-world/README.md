# Plane masters without a usable screen_loc (stock 516.1689)

Does a plane master in `client.screen` act on its plane when its own `screen_loc` is null, empty, invalid or a
range? Windows BYOND **516.1689**, one DreamSeeker, Normal presentation, a 7x7 view (`view = 3`) of 32-pixel tiles.
Sources and icons are synthetic.

## Setup

`world.dm` puts a 16x16 red square (`st` from `z.dmi`) on plane 1 at the centre turf over a dark floor. For each
phase it creates a fresh `/obj/master` (`PLANE_MASTER`, plane 1), sets its `screen_loc` and one effect, and adds it to
`client.screen`. A phase is `<screen_loc>|<effect>`: `null` and `empty` mean `screen_loc = null` and `""`; effects
are a white size-1 outline, a size-2 blur, a green drop shadow, `alpha = 0`, `color = "#00ff00"`, KEEP_TOGETHER with
`alpha = 0`, a 16x16 cyan overlay (`img`), that overlay with the outline, and `render_target = "*cap"` with a
relay object (`render_source = "*cap"`, `screen_loc = "2,2"`). The `obj` effect is a control: the same object
without `PLANE_MASTER`, with the cyan square as its own icon. Between phases the screen is emptied.

The world is 15x15 with two levels. Level 1 is the original room: only its 7x7 corner has a floor icon, and the
first captures, taken when the world was 7x7, were taken again with this world and came out identical. A phase with a
third field `wide` (`8,1|obj|wide`) moves the eye to 8,8 on level 2, where the turf columns are dark, green and blue
by x modulo 3, a yellow square stands on the default plane at 6,8 (in the view), 12,8 (past it) and, moved 16
pixels right, 5,9, a blue tile at 4,8, a cyan square on plane 1 at 12,9 and a yellow one moved 16 pixels right at
5,10, the turf at 10,10 is coloured magenta by a colour matrix and an object at 10,6 orange by another.

## Observations

`stock-observations.json` holds, for each phase, its capture and the number of pixels of each marker colour in the
top-left 200x200 of the client area (outline white, part red, shadow green, overlay cyan, blurred red). The captures
are the matching `stock-*.png`; each was taken 1.5 s after the world logged READY for the phase and was identical to
a second capture 0.3 s later.

- `screen_loc` null, `""` or `bogus`: the master does nothing. The square draws plainly: no outline, blur or shadow,
  `alpha = 0`, colour and KEEP_TOGETHER have no effect, and the overlay is not drawn. With null or `""` the render
  target is not made either: the square stays in place and the relay shows nothing (`bogus` was not tried with it).
- `screen_loc = "1,1"`: every effect applies (outline 64 pixels, square hidden by `alpha = 0` or the green colour;
  with the render target the square leaves its place and the relay draws it one tile right and one up).
- `screen_loc = "1,1 to 3,3"`: the master acts. The plane is outlined once (64 pixels, as at 1,1), `alpha = 0`
  hides it, and an overlay is drawn at each of the nine positions, outlined too.

Phases with their own `"crop": [0, 0, 224, 224]` count the whole map and also give each marker colour's `bounds`
(left, top, right, bottom) and the number of black pixels. In the plain view the square is at 96..111 both ways and
an overlay at 7,1 at 192..207 (`7,1|ov`).

- One position off the view (`8,1`, `0,4`, `1,9`): the master acts (`alpha = 0`, the green colour and KEEP_TOGETHER
  with `alpha = 0` hide the square, the outline is 64 pixels), and the map grows by the missing tiles exactly as it
  does for the control object: the larger map is centred, so the square moves 16 pixels left for `8,1`, 16 right
  for `0,4` and 32 down for `1,9`, with black where there is no map. The overlay is drawn at that position: for
  `8,1` at 208..223 across, the new column; for `0,4` and `1,9` it falls outside the 224 pixel window, and only the
  right edge of its outline shows for `0,4`. Every `|ov` capture equals its `|obj` capture.
- A pixel offset past the edge (`7:40,1`): the master acts, nothing is added to the map and the overlay is out of
  sight; at `7:24,1` its left 8 pixels show.
- A range partly or wholly off the view (`6,6 to 9,9`, `9,9 to 10,10`): the master acts, the map grows to hold the
  whole range (9x9 and 10x10 tiles), and an overlay is drawn at each position (nine of the sixteen are inside the
  window for `6,6 to 9,9`, none for `9,9 to 10,10`), again as for the control object.
- A range with different pixel offsets at its ends: the master acts and the grid of overlays is moved by the first
  end's offsets only. `1,1 to 3:8,3` and `1,1 to 3:40,3` draw the nine overlays of `1,1 to 3,3`; `1:8,1 to 3,3`
  draws them 8 pixels right; `1:4,1:8 to 3:12,3` 4 right and 8 up; `3:8,3 to 1,1` 8 right. The control object's
  icons are in the same places.
- A range with a percent end: the master acts, and the overlays are where the control object's icons are, which is
  not a plain grid between the two ends. `0%,0% to 50%,50%` and `1,1 to 50%,50%` draw one overlay at 1,1;
  `2,2 to 100%,3` draws two, at 7,2 and 7,3; `50%,50% to 6,6` draws sixteen, from 4,4 to the top right corner (the
  rule is in the next section).

In the larger maps part of the world next to the added tiles is black as well (a 16 pixel strip on the left for
`8,1`; the top 64 rows for `1,9`); it is the same for the master and the control object and is not examined here.

Fugue does the first three. A master with no screen_loc, or with text that is not coordinates, is skipped
(`EntityMapTest::masterWithoutScreenLocIsInert`); `bogus` is the only such text that was tried, and anything the
screen_loc grammar cannot read as two axes is treated like it. A master with a range acts once and draws what it owns
at every position (`EntityMapTest::masterRangeDrawsOverlaysAtEveryPosition`).

A master off the view, alone or as a range, acts and asks for the larger map like any screen object; its overlays
are drawn there once the scene has grown, which is the frame after the one that learns of the placement, and are
left out until then or when the scene cannot grow (`EntityMapTest::masterOffViewActsInTheLargerMap`, which compares
the square's and the overlay's places with these captures). A range's grid follows its first end's pixel offsets for
masters and ordinary objects (`EntityMapTest::masterRangePixelOffsetsFollowTheFirstEnd`), when the range is written
from its lower end.

## Percent ends, ranges written backwards, anchors and named maps

A second set of 108 phases (captured together, with the level 2 phases below, on 516.1689) has, in each entry, the
whole map counted and `"copies"`: the left, top and visible pixels of every cyan square, from the top. The older
entries were given `"copies"` from their captures too. In every `|ov` phase the master's overlays are exactly where
the `|obj` control's icon copies are, and every `|alpha0` phase of a placed master hides the square: a master places
its range like an ordinary screen object. The rule, per axis, on this 7x7 view:

- The tile part of each end (its number, or its anchor, with CENTER at 4 and EAST at 7; LEFT counts as 1) orders the
  ends, counts the copies and decides how far the map grows. A percent does neither: it moves the grid like a pixel
  offset, by that share of the view less one tile. `2,1 to 50%,1` draws two copies from 4,1; `50%,1 to 3,1` three
  from 4,1; `25%,1 to 3,1` three from 2.5,1; `1,1 to 3+50%,1` three from 1,1; `50%,50% to 6,6` sixteen from 4,4 of
  which those past the view are not drawn; `50%,1 to 6,1` four (4 to 7), the map unchanged; `50%,1 to 9,1` grows the
  map by two columns as `1,1 to 9,1` would; `3+100%,1 to 1,1` shows one copy, at 7,1, and the map keeps its size.
  A single `5+50%,1` (8,1) is not drawn and does not grow the map, and that master still acts.
- Two ends on the same tile keep the first end's percent and offsets: `1,1 to 50%,1` is one copy at 1,1, `50%,1 to
  1,1` one at 4,1, `100%,1 to 50%,1` one at 7,1, `1,1 to 100%,100%` one at 1,1, `100%:-8,100% to 1,1` one at 7,1
  less 8 pixels.
- Written from the lower end up, the first end's percent and offsets move the grid (`1:8,1 to 50%,1`,
  `50%:8,1 to 3,1`, `LEFT+50%:8,1 to 3,1` 8 pixels right of the plain form).
- Written from the upper end down, the grid starts at the lower end and is moved by the percents and offsets of both
  ends added together: `3,1 to 1:8,1` 8 pixels right, `3:4,1 to 1:8,1` 12, `3:8,3:8 to 1:4,1:4` 12 both ways,
  `3:-8,3 to 1,1` 8 left (the first column half cut), `3:40,3 to 1,1` 40 right; `3+50%,1 to 1,1` three from 4,1,
  `2+50%,1 to 1+25%,1` two from 5.5,1, `3,1 to 2+25%,1` two from 3.5,1, `3,1 to LEFT+50%,1` three from 4,1,
  `EAST-50%,1 to 1,1` seven from -2,1 of which 1 to 4 show. Each axis on its own: `1:8,3:8 to 3,1` is 8 pixels right
  and 8 up.
- On this odd view CENTER is a whole tile, and a range from it down is like any other: `CENTER,1 to 1,1` four copies
  from 1,1, `CENTER+25%,1 to 1,1` four from 2.5,1. `CENTER to 6,6`, `CENTER,CENTER to 6,6`, `1,1 to CENTER,CENTER`,
  `LEFT,BOTTOM to 3,3`, `WEST,1 to EAST,1`, `WEST,SOUTH to EAST,NORTH`, `1,1 to 3,TOP` (three copies on the bottom
  row) and `LEFT,BOTTOM to RIGHT,TOP` (one at 1,1) were tried too, as were `RIGHT-50%,1 to 1,1` (one at 4,1),
  `RIGHT-50%:8,1 to 1,1` (8 pixels right of it), `RIGHT-50%,1 to 3,1` (4 to 6) and `1,1 to RIGHT-50%,1` (1,1).
- One end off the view: `5,5 to 9,9` grows the map by two columns and rows and draws the sixteen copies; `0,0 to
  2,2` grows it by one column and row on the left and bottom.
- `map:1,1` and `map:1,1 to 3,3`, where `map` is the id of this world's only map: nothing is drawn, and such a
  master does nothing (`|alpha0` and `|outline` leave the square as it is).

Fugue places all of these as stock does, for masters and ordinary objects (`EntityMapTest::masterLocStockPositions`
compares every `|obj` and `|ov` entry with `"copies"`, the map's growth and the master's overlays; the
`screenLocationGrammar` cases cite these phases). Still not placed, which refuses a master tree with overlays,
KEEP_TOGETHER or tree filters: a range written backwards with a pixel offset on an edge-aligned end (`RIGHT`, `TOP`,
`LEFT`, `BOTTOM`) or a percent on a `RIGHT` or `TOP` second end, different offsets with an edge-aligned end other than
a first end's own before a plain second end, a range running down to a CENTER, and one running down from a half tile.

## What the larger map shows (level 2, `|wide`)

With this skin (`icon-size = 32`, a 224x224 map for the 7x7 view) the larger map is not scaled: it is centred and
cut by the control, and world and screen objects stand where the larger map puts them (the red square 16 pixels left
for `8,1|obj|wide`, 32 for `9,1`, 16 right and up for `0,0`, 32 down for `1,9`; Fugue shows the same). What fills it:

- The world past the view is drawn in the added tiles, turfs and objects on any plane: for `8,1` and `9,1` the
  turfs of column 12, the yellow square at 12,8 and the cyan one on plane 1 at 12,9 show in the new column(s).
- A black strip covers the world at the control's left and top, 16 pixels times (near + max(near, far)) for the
  tiles added on the near (left, top) and far (right, bottom) side: 16 pixels wide for `8,1`, 32 for `0,4`, `9,1` and
  `0,0` (and 16 rows on top for `0,0`), 64 rows for `1,9`. Objects on the default plane and on plane 1 under it are
  hidden (the yellow squares at 5,9 and 5,10 for `8,1`); screen objects are drawn over it (the overlays of
  `6,6 to 9,9|ov|wide` in the top 64 rows).
- An atom coloured by a colour matrix is drawn as if the view filled the control less the strip, scaled to it: the
  magenta turf at 10,10 is 29 pixels wide at 165..193 for `8,1` instead of 32 at 144..175, which stays black; the same
  at 13/14 of a tile for one added column or row and 6/7 for two.

The first two are one rule, which holds for controls of other sizes too. A fourth field in the phase resizes the
window and its map control (`8,1|obj|wide|288x288`, by `winset`), and stock was grabbed with controls of 192, 240 and
288 pixels (`stockprobe.py --map 288x288 pair` of `8,1`, `0,4`, `9,1` and `null|none`, `wide`, at each size). On each
axis take the view's place in the control (the larger map is centred, so the view stands where the added tiles put
it), keep the part the control shows, and when the map is larger than the control and so starts before it, move that
part right (down) by what is cut off. The world is drawn only in that part; screen objects everywhere.

- 224 pixels: the map starts 16 pixels left for one added column, so the part of the view shown, 0..208 for `8,1`,
  moves to 16..224, which takes in the new column and leaves the 16 pixel strip; for `0,4` the view shown, 16..224,
  moves to 32..240 (a 32 pixel strip). The formula above is this.
- 240 pixels: world 8..224 for `8,1` (the new column shows 216..224, the control's last 16 pixels are black), 32..240
  for `0,4`, 24..224 for `9,1`; the 224 pixel view stands 8 pixels down, with no strip on top.
- 288 pixels: the map fits, nothing moves and the world fills only the view: 16..240 for `8,1`, 48..272 for `0,4`,
  0..224 for `9,1`. The added tiles are black but for the screen object.
- 192 pixels, `8,1`: world 32..192 across and 16..192 down. The strip is on top too, where nothing was added, because
  the map is cut there as well. With no screen object outside the view (`null|none|wide|192x192`) the map is centred
  and cut with no strip at all: the rule belongs to the enlarged map only.

A colour-matrix atom is the view-sized picture of it stretched from the view's place onto that part (the magenta turf
at 169..191 and 43..47 for `8,1` at 192 pixels: 6/7 of a tile both ways), and its own tile stays black (more in
`|wide-fx` below).

Fugue draws the world in the added tiles and only in that part (`mapWorldClip` in `shell/mappresentation.h`, applied by
`SpriteRenderer` to world sprites; `EntityMapTest::worldInTheLargerMap` has the strip and edge each capture shows,
and `CompositionGpuTest::masterLocWideReplay` replays six recorded cases, at 192, 224, 240 and 288 pixels, against
stock's grab of the control).

### Clicks in the larger map

Every atom logs `Click()` with its name, icon-x/y and place (`at=x,y`; screen objects stand at 0,0), and each kind of
atom has one verb, `poke_<kind>` (`floor` for turfs, `nst`, `rock` and `img` for the objects named `obj` by their icon
state, `part`, `master`), so that a menu entry tells its kind; neither changes what is drawn. `clicks.json` holds what
stock 516.1689 answered (2026-11-26, `stockprobe.py --stock-only --map <control> --params case={} click` with the
script below; `clicks.py <output> 516.1689 2026-11-26` writes it), for `8,1`, `1,9` and `0,4` at 224 pixels, `8,1` at
288 and at 192:

- The world takes the pointer only in the view. In the added tiles nothing of the world is clicked or listed, whether
  stock draws it there (`8,1` at 224: 216,100 over the yellow square at 12,8, 218,150, `1,9` at 100,16) or leaves the
  tile black (`8,1` at 288: 248,135 and 256,180; `0,4` at 8,112 over the blue object at 4,8). The world logged no
  `Click()` and no menu opened. A screen object is clicked there (`8,1` at 212,200 and 248,232) and listed by its
  rectangle (`0,4` at 8,112, where its icon is off the control).
- In the black strip the world of the view is clicked and listed as if it were drawn: `8,1` at 8,72 is the yellow
  square at 5,9 hidden there (x=9 y=24) and the menu lists it over the floor; 8,112 is the floor at 5,8; `1,9` at 100,48
  is 8,11, `0,4` at 24,112 is 5,8 (x=9), `8,1` at 192 pixels 100,8 is 9,11 and 16,100 is 6,8, with the icon
  coordinates of where the larger map puts them.
- A colour-matrix atom is clicked on its own tile, where stock leaves it black, and not where it draws it stretched:
  `8,1` at 150,48 is the magenta turf 10,10, at 185,48 the turf 11,10 under the stretched picture; 148,165 is the
  orange object at 10,6 (x=5 y=27) and 172,165 the floor under its picture.

Fugue now does all of this (`MapInputTest::largerMapClicksLikeStock` replays each case's `replay/<case>.txt` and asks
the real hit test and menu stack for every point): world sprites take the pointer only in the view
(`MapPointer::worldView`), the turf under a pointer outside it is not named, and screen objects are unchanged. Not
measured: whether stock sends a click on nothing there to `client.Click()` (Fugue sends it as a click on the map's empty
area, as on the letterbox), hover and drags over the added tiles, and a turned client direction.

```
8,1_obj_wide 8,72 8,112 17,112 216,100 218,150 212,200 150,48 185,48 148,165 172,165 right:8,72 menu: right:8,112 menu: right:216,100 menu: right:185,48 menu:
1,9_obj_wide 100,16 100,48 100,66 right:100,16 menu: right:100,48 menu:
0,4_obj_wide 8,112 24,112 40,112 right:8,112 menu: right:24,112 menu:
8,1_obj_wide_288x288 248,135 256,180 248,232 right:248,135 menu: right:256,180 menu:      (--map 288x288)
8,1_obj_wide_192x192 100,8 16,100 100,30 right:100,8 menu: right:16,100 menu:             (--map 192x192)
```

### What takes the stretched path, turned sprites, a scaled map (`|wide-fx`)

A third field `wide-fx` is `wide` with white squares (`w.dmi`, staged by `prepare.py`) added on level 2, each drawn
one way (`extras()` in `world.dm`): a plain `color` at 7,6, a colour matrix that swaps red and blue on the red square
at 6,10, an outline filter at 9,7, KEEP_TOGETHER with an overlay at 7,10, `alpha = 128` at 9,10, BLEND_ADD at 9,6, a
matrix scaling by 2 at 7,8, and a whole tile turned by 45 degrees at 5,7 (the view's left column) and at 12,6 (the
column `8,1` adds). A fifth field sets the map's `icon-size` (`8,1|obj|wide|336x336|48`). Grabbed on 2026-11-26 with
`stockprobe.py --map <size> pair` (96 dpi):

- Only the colour matrices take the stretched path: the swap moves with the magenta turf and the orange object (at
  224 pixels 46..60 instead of 16..31 across, at 192 pixels 59..72 and 43..56), while the plain colour, alpha, the
  outline, KEEP_TOGETHER, BLEND_ADD and the scale stay on their tiles. The picture of the view is mapped onto the
  clip as a whole (at 192 pixels the view's 224 pixels go to 32..224 across and 16..208 down, cut by the control), a
  pixel is covered when its centre is, and it is drawn in the atom's turn: the swap's picture covers the left of
  KEEP_TOGETHER's square at 7,10, and at 192 pixels the turfs of the row below cover most of the magenta turf's.
- A turned world sprite is cut like any other: at 224 pixels the one at 5,7 shows from x=16 only, at 288 pixels the
  one at 12,6 shows its corner in the view up to x=240 and nothing in the black added column.
- With icon-size 48 in a control of 336 pixels the picture is the 224 pixel case magnified 1.5 times: the strip is
  0..23, the swap 69..90, the magenta turf 248..290, the orange object 248..267: the rule holds in scene pixels.

Fugue now draws a world sprite coloured by a colour matrix stretched from the view (`mapWorldView`) onto the clip and
not on its own tile, and cuts turned world sprites by the clip (`SpriteRenderer`). `replay/` gained `8,1_obj_wide-fx`
at 224, 192 and 288 pixels (`stockprobe.py --map <size> record`); `CompositionGpuTest::masterLocWideReplay` now
compares every pixel of all nine controls, colour-matrix atoms included, with no channel more than 8 levels off, and
`stockprobe.py pair` of `8,1_obj_wide-fx` at 224 and of `8,1_obj_wide-fx_336x336_48` showed no differing pixel. The
scaled case is checked by `EntityMapTest::worldInTheLargerMap`. Not measured: a turned or filtered atom with a colour
matrix, a colour matrix on an image, a vis copy or a plane-1 atom, a colour from `client.color`, and maps scaled
other than by icon-size 48.
The live client applies the phase's `winset` of the window's and the map's size as stock does
(`tests/data/winset-size-world`): `stockprobe.py --map 288x288 pair` of `8,1_obj_wide_288x288` gave no differing
pixel (2026-11-26, 516.1689, 144 dpi), and at 240 and 192 pixels the map differed only in the colour-matrix atoms above,
which Fugue now stretches too.
## Reproduction

Requires BYOND and Python/Pillow. Stage icons, then this world:

```sh
python -B tests/data/master-screen-loc-world/prepare.py <runtime>
dm.exe <runtime>/world.dme
python -B tests/data/master-screen-loc-world/collect.py <runtime> <out> "1,1|outline" "null|outline" ...
python -B tests/data/master-screen-loc-world/collect.py <runtime> <out> --port=18851 "8,1|ov" "8,1|obj" ...
python -B tests/data/master-screen-loc-world/measure.py <out> --copy > <entries.json>
```

`prepare.py` stages the icons `local-relay-world/prepare.py` generates and this world. `collect.py` hosts the world
with DreamDaemon `-safe -invisible` on TCP port 18811 (or the one given by a leading `--port=`), starts DreamSeeker,
writes each phase to `phase.txt`, waits for READY and captures the client area with `PrintWindow`, 1.5 s after READY
and again 0.3 s later (`stable`: the two were identical). It stops only the two processes it started. The window
must not be minimised. `measure.py` makes the entries of the second set from a run (the counts, bounds and copies
described above; it does not count `blurred`) and copies its captures here under the fixture's names; `measure.py
--add-copies` gave the older entries their copies. The world also takes its phase from `world.params` `case`, with
`_` for `|` and `~` for `:`, so `tools/stockprobe/stockprobe.py --world tests/data/master-screen-loc-world --params
case={} pair 8,1_obj_wide` grabs stock and Fugue side by side.
The replay set came from `stockprobe.py --world tests/data/master-screen-loc-world --map 288x288 record
8,1_obj_wide 0,4_obj_wide 1,9_obj_wide 8,1_obj_wide_288x288 9,1_obj_wide_240x240 8,1_obj_wide_192x192` (the world reads
`set` as it reads `case`), which writes `replay/` (frames, resources, stock's map at the top left of the window); the
`8,1_obj_wide-fx` sets came from the same command with `--map 224x224`, `192x192` and `288x288`.

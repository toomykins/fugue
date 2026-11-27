# A plane drawn through a relay

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-25 and 2026-11-26, display at 144 dpi. No game assets in Git.
`prepare.py <dir>` writes the world, a 224x224 skin (a 7x7 view of 32 px tiles) and solid icons: a floor and 16 px boxes
at 8..23 (`box.png` red, `blue.png`, `green.png`, `yellow.png`).

Plane 1's master (`screen_loc = "1,1"`) has `render_target = "*p1"`. On plane 1 stand `part` (red) at (5,4), `other`
(blue) at (2,5) and a HUD object `button` (green, `screen_loc = "6,2"`). A screen object `relay` on plane 5 draws the
plane with `render_source = "*p1"` and `PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR`, as SS13 codebases relay their
planes. The case name's words (see `set_case()`): `norelay` (no relay), `nopass` (no PASS_MOUSE), `visible` (the target
is `p1`, so the plane draws itself too), `loc23`, `locpx`, `loccenter` (the relay at `2,3`, `2:5,3:-7`,
`CENTER-2,CENTER-1`), `rturn90`, `rt_45` (the relay turned), `rhalf`, `rscalex2` (scaled), `rflip` (`matrix(-1,0,0,0,1,0)`),
`rmove` (`matrix(1,0,10,0,1,5)`), `rgrowmove` (`matrix(1.5,0,100,0,1.5,0)`), `rturnmove` (turned by 30 degrees, then
moved 20 right and 10 up), `malpha` (the master's alpha 254, so its picture is processed), `mturn90` (the master
turned). `rm_a_b_c_d_e_f` puts any matrix on the relay; it was used to probe and is in no retained case. The words of
the second round: `locleft`, `locright`, `locroff`, `locws` (`LEFT+1,BOTTOM+2`, `RIGHT-5,TOP-4`, `RIGHT-5:4,TOP-4:-3`,
`WEST+1,SOUTH+2`), `locpb`, `locpd`, `locpe`, `locpf`, `locpg` (`1+25%,1+40%`, `1+10%,1+10%`, `1-20%,1-20%`,
`RIGHT-90%,TOP-90%`, `1+40%,1+25%`), `locrange`, `locrow` (`1,1 to 3,3`, `2,3 to 4,3`), `rpx`, `rpw` (the relay's
`pixel_x = 10, pixel_y = 5`, or `pixel_w = 10, pixel_z = 5`) and `mark` (an ordinary screen object `mark`, yellow, on
plane 6, at the relay's screen_loc). The third round's words: `grid` (in place of `part` and `other`, a 224 px icon
on plane 1 at 1,1 whose red is the pixel's column, green its row and blue a one-pixel checker) and `rnear` (PIXEL_SCALE
on the relay). The fourth round's words: `chain` (plane 5, where `relay` stands, gets a master `master5` with
`render_target = "*p5"`, and a second relay `relay2` on plane 7 draws it, flagged as `relay` is), with `bneg`,
`bturn90`, `bhalf` (`relay2` at `1:-40,1:-72`, turned by 90 degrees, halved), `bopaque` (`relay2` without PASS_MOUSE)
and `m5turn90` (`master5` turned); `mcenter` (the master at `CENTER`); `fblur`, `foutline`, `fcolor` (a size 2 blur,
a white size 1 outline, or a colour matrix swapping red and blue, on the relay); `rov` (a yellow box overlay on the
relay; with `nokt` the relay has no KEEP_TOGETHER); `ovrelay` (the relay object draws nothing itself: an overlay of it
with `render_source = "*p1"` and PASS_MOUSE does, the object keeping PASS_MOUSE without KEEP_TOGETHER); `locpbare`,
`locpneg`, `locprange` (`40%,40%`, `RIGHT-15%,TOP-15%`, `1+10%,1+10% to 2,2`); `iv` (an object `holder` at 3,3 that
draws nothing but carries the client's own image, a yellow box on plane 1, and `visholder` at 6,5, also iconless, whose
`vis_contents` holds `child`, a green box on plane 1; all made once, in `world/New()`).

Every atom logs `Click()` with its name and icon-x/y, and has one verb, `poke_<name>`, so that an entry of the
right-click menu is known by its verb.

## How the data was taken

`tools/stockprobe/stockprobe.py --world tests/data/plane-relay-world --map 224x224 --still 0.5 --jobs 3 record <case>...`
wrote `replay/<case>.txt` (what DreamDaemon sent the client), `replay/res/` and `replay/<case>.png` (stock's map).
`stockprobe.py --world tests/data/plane-relay-world --map 224x224 --params set={} --still 0.5 --live --stock-only click
@<script>`, one `<case> ACTION...` line per case: a left click `X,Y` and a right click `right:X,Y menu:` where the relay
draws each box, where the relay's matrix about the view's centre would put it (when that is elsewhere), where the box
stands, and on empty floor. `clicks.py <output> 516.1689 2026-11-25` kept stock's answers in `clicks.json`. The
second round's cases (`locleft`, `locright`, `locroff`, `rpx`, `rpw-loc23`, `locpd-mark`, `locpe`, `locpf-mark`,
`locrange`, `locrow`) were recorded the same way on 2026-11-26, a few points each: inside a drawn box, just outside it
where an unmoved, differently rounded or offset picture would draw it, and on empty floor; their `clicks.py` output
was appended to `clicks.json`, each case with its date. The third round (`grid-*`, 2026-11-26, display at 96 dpi, no
clicks) was recorded the same way, with `--port 18920 --jobs 4`. The fourth round (2026-11-26, display at 96 dpi,
`--port 18960 --jobs 3`) recorded 21 cases and took stock's clicks for 19 of them in one `--live` session, a few points
each: where each link would put a box, where the other rules would, the blur's and the outline's edges, and for the
menus of `nopass-rhalf` and `nopass-rt_45` points inside and outside each candidate rectangle; their `clicks.py`
output was appended to `clicks.json`. `iv-loc23` was recorded and clicked the same way afterwards, on its own.
`mapinput_test::planeRelaysLikeStock` replays each case without a server and asks the real hit test and menu stack for
every point; `compositiongpu_test::planeRelayReplay` draws each case and compares it with stock's grab.

Matrices on the relay were also probed with `stockprobe.py ... --stock-only pair` over many more matrices (turns of
10, 15, 30, 45, 60, 120, 135, 270 and -45 degrees, scales of 0.5, 0.75, 1.5 and 2 on either axis, shears, moves and
their combinations): the rule below put every box within a pixel of where stock drew it. Those grabs are not retained,
nor are the `pair` grabs of `locws`, `locpb`, `locpg`, `rpx-loc23` and `rpx-locright`, which drew as stated below
(`TOPRIGHT`, `50%,50%` and `RIGHT-25%,TOP-10%` move every box out of the view, so stockprobe saw a blank map). In the
fourth round `mcenter-rcenter` (the master and the relay at `CENTER`) and a chain with `relay` at `2,3` and `relay2`
at `3,2` likewise saw a blank map, every box moved out of the view, and were not retained.

## Independent conclusions

- **A relay draws the plane's picture moved as far as the relay stands from 1,1**, pixel offsets included (`loc23`,
  `locpx`), CENTER, EAST and NORTH counted in tiles as for any screen object (`loccenter`).
- **Every screen_loc form places a relay as it places an ordinary screen object with a one-tile icon.** The edges
  count in tiles, as WEST and SOUTH do: `LEFT+1,BOTTOM+2`, `RIGHT-5,TOP-4` and `WEST+1,SOUTH+2` draw as `2,3` does, and
  `RIGHT-5:4,TOP-4:-3` 4 pixels right and 3 down of it (`locleft`, `locright`, `locroff`); the relay's picture, as wide
  as the view, is not aligned by its size. A percent moves the picture exactly as far as it moves `mark` at the same
  screen_loc (`locpd-mark`, `locpf-mark`): a share of the view less one tile, whose pixels, added to a tile, are cut
  towards zero (`1+10%,1+10%` is 19 right and 19 up for 19.2; `1-20%,1-20%` 38 left and 38 down for 38.4, `locpe`;
  `1+25%,1+40%` 48 right and 76 up) but from an edge go left and up (`RIGHT-90%,TOP-90%` is 19 right and 20 up).
- **A range draws the plane's picture once at every position**: `1,1 to 3,3` nine times, `2,3 to 4,3` three times (the
  third out of the view), and each copy is clicked and listed through like a single relay (`locrange`, `locrow`).
- **The relay's own pixel_x/y and pixel_w/z move nothing**, alone or with a moved screen_loc, neither its picture nor
  where its atoms are clicked (`rpx`, `rpw-loc23`). DreamDaemon sends the relay's appearance without them.
- **A relay with a matrix draws the picture through it**, about the view's centre, but stock moves a neutral plane's
  picture first by u per axis (Y down): half of what the view taken back through the matrix's linear part outgrows the
  view by, plus the matrix's own move taken back the same way, rounded up to a whole pixel and never negative. So a half
  scale lands in the view's bottom right quarter (`rhalf`), a turn of 45 degrees lies 66 px lower than one about the
  centre (`rt_45`), a quarter turn, a flip or an enlargement is about the centre (`rturn90`, `rflip`, `rscalex2`), and
  a positive move counts twice where a negative one counts once (`rmove`). A processed plane's picture is not moved
  (`malpha-*`).
- **Clicks go through a PASS_MOUSE relay to the plane's atoms**, back by the relay's offset and through the relay's
  matrix about the view's centre, never with the move above: under `rhalf` and `rt_45` the boxes are clicked where an
  unmoved picture would show them, and where they are drawn is floor. The pointer then goes back through the master's
  matrix (`mturn90`, `mturn90-loc23`). The pixel taken is the one whose corner the inverse lands in (`rturn90` at
  114,139 names `part` y=18).
- **A relay without PASS_MOUSE is clicked itself where the plane has ink** (empty floor stays floor), with the
  picture's own icon-x/y (`nopass-loc23` at 171,45: x=140 y=115, as `nopass` at 139,109); not through the master's
  matrix (`nopass-mturn90` at 114,139: x=115 y=85).
- **Where a relay draws the plane, its atoms take no clicks where they stand** (`loc23` at 139,109 is floor). **With no
  relay a `*` plane is not drawn but stays clickable** (`norelay`).
- **The right-click menu lists the plane's atoms where they stand** (through the master's matrix: `mturn90-loc23` at
  100,150, not at 139,109), relayed or not, and **also through a PASS_MOUSE relay** where it draws them (`loc23` at
  171,45). A relay without PASS_MOUSE is listed itself over the whole rectangle of the picture it draws (`nopass` at
  100,150) and lists nothing through it.
- A relay of a visible target draws the plane once more (`visible-loc23`).
- **A relay's matrix samples the whole plane, bilinearly, and repeats its edge texels past it.** Enlarged by 2
  (`grid-rm_2_0_0_0_2_0`) the view's border mixes with the plane just past the part the view shows (a master's matrix
  would not: master-transform-world `fx-grid-s200`); moved so that the plane's bottom right quarter fills the view
  (`grid-rm_2_0_-112_0_2_112`) the last column and row are the plane's own, not faded to the floor.
- **PIXEL_SCALE on the relay samples nearest**, by a master's rule: the texel the pixel's centre falls in, on a
  boundary the one before it, nothing past the plane's edge (`grid-rnear-rm_0.5_0_0_0_0.5_0`, `-0.6_..`).
- **The master's own screen_loc moves nothing**: at `CENTER` (`mcenter`) the plane draws and is clicked as at `1,1`.
  A relay at `CENTER` moves the picture three tiles in this view whatever the master's place (`mcenter-rcenter`, blank).
- **A chain draws each link by the rules above, one after the other.** `relay2` draws plane 5, which holds `relay`'s
  picture: alone it changes nothing (`chain`), its move adds to `relay`'s (`chain-loc23-bneg`: 8 left and 8 down), its
  matrix turns the moved picture about the view's centre (`chain-loc23-bturn90`), a half scale on either link lands in
  the bottom right quarter as one relay's does (`chain-rhalf`, `chain-bhalf`: plane 5 is neutral, so it is moved too),
  and `master5`'s matrix turns it as a master's does (`chain-m5turn90`).
- **Clicks and menus go through every PASS_MOUSE link**, each taken back by its own rule (offset, matrix about the
  centre without the move, master's matrix): a halved link is clicked where an unmoved picture would show the box and
  its drawn box is floor (`chain-rhalf`, `chain-bhalf` at 127,111 and 183,167). The menu lists the plane's atoms where
  they stand, and through the chain where it draws them, also through a turned `master5`. **`relay2` without
  PASS_MOUSE is clicked itself where the first plane's atoms are drawn**, with the pixel of plane 5's picture as its
  icon-x/y (`chain-bopaque-loc23` at 175,47: relay2 x=176 y=177), and its menu lists relay2 over the whole picture and,
  where they are drawn, the atoms `relay` passes to, `relay` standing where it is on plane 5.
- **A relay's own filters change its picture and nothing else**: a blur, an outline or a colour matrix draws on the moved
  or turned picture (`fblur-loc23`, `foutline-loc23`, `foutline-rturn90`, `fcolor-loc23`), and clicks go to the
  atoms' own pixels: the faded edge of a blurred box is the box (`fblur-loc23` at 169,41), the blur past it and the
  outline are floor (166,47; `foutline-loc23` at 167,47). The menu lists by the atoms' rectangles as without filters.
- **A relay's own overlay is clicked and listed as the relay** with its icon-x/y in the relay's tile, PASS_MOUSE or
  not, and under a master's matrix (`nokt-rov`, `nokt-rov-mturn90` at 15,207). **An overlay with render_source draws
  the plane as the object would**, moved by the object's place, and passes clicks and menus through to the atoms
  (`ovrelay-loc23`, `ovrelay-mturn90`).
- **Through a PASS_MOUSE relay a client image is clicked as its atom and a vis_contents member as itself**, as where
  they stand, and the menu lists the member and then its iconless holder (`iv-loc23` at 111,79: holder; at 207,15:
  child, menu child, visholder).
- **A relay without PASS_MOUSE that has a matrix** is clicked where an unmoved picture has ink (`nopass-rhalf` at
  127,111: x=143 y=114; `nopass-rt_45` at 89,44: x=48 y=144), its drawn boxes are floor, and **its menu rectangle is
  the view taken through the matrix about the centre, without the move**: `nopass-rhalf` lists it at 60,60 but not at
  200,200 (drawn) or 20,20; `nopass-rt_45` at 112,112 and 112,10 but not at 5,5 (inside the turned square's bounds) or
  5,218 (inside the drawn, moved square).
- **A bare percent counts from tile 1** and is cut towards zero as `1+40%` would be (`locpbare-mark`: `40%,40%` is 76
  right and 76 up for 76.8, where an edge's rule would give 77 up); `RIGHT-15%,TOP-15%` goes left and up as other edge
  percents do (163 right, 164 up for 163.2, `locpneg-mark`), and a range from `1+10%,1+10%` starts 19 right and 19 up
  (`locprange-mark`). The relay's picture and `mark` move alike.

## Fugue

Before: a relay of a plane was drawn only at `1,1` (or `SOUTHWEST`) and only with no matrix or a positive scale and
whole-pixel move; elsewhere or turned it was dropped, and the plane's atoms were then clickable where they stood. Clicks
did not pass through a relay over a master with a matrix, nor through a moved or turned one, and the menu listed no
relayed atom and no relay. After: every grab matches stock (all 23 cases, on Direct3D 11, OpenGL and Vulkan), and every
click and menu in `clicks.json` matches stock (155 clicks, 155 menus). The quarter-turn rows of
`relay-gate-world/matrix.json`, recorded earlier and left out until now, also pass.

Second round: a relay with an edge, a percent or a range screen_loc was dropped, so its plane was not drawn and its
atoms took clicks where they stood. A percent added to a tile was rounded up instead of cut towards zero vertically,
and down instead of towards zero horizontally, for ordinary screen objects as well (`mark` in `locpd-mark` stood a
pixel too high). `rpx` and `rpw-loc23` already drew and clicked as stock, since the offsets never reach the relay's
appearance; a relay whose appearance does carry them is no longer dropped and is drawn unmoved (inferred). After: all
33 grabs match stock (checked on Vulkan), and all 190 clicks and 169 menus.

Third round: Fugue faded the relayed plane at its edge (`grid-rm_2_0_-112_0_2_112`: 447 pixels) and sampled a
PIXEL_SCALE relay bilinearly (`grid-rnear-*`: 12480 and 15948 pixels). After: all 37 grabs match stock (Direct3D 11,
OpenGL and Vulkan).

Fourth round: every grab but one already matched stock, chains, percents and overlays included; a relay with a colour
matrix filter was dropped (`fcolor-loc23`), so its plane was not drawn. Clicks did not pass through a relay with any
filter, the overlay relay's clicks were found but then judged stale, a relay's own overlay took no click and was not
listed, `relay2` without PASS_MOUSE was not clicked over a plane holding another relay, and a turned `master5` hid
the chain's atoms from the menu (10 of the 37 new clicks and 4 of the 30 new menus differed). Through a relay a
client image and a vis_contents member were floor, and the holder was missing from the menu (`iv-loc23`). `nopass-rhalf`, `nopass-rt_45`, the
percents and `mcenter` already matched. After: all 59 grabs match stock (checked on Vulkan), and all 227 tested
clicks and 201 menus. The clicks of `ovrelay-*` named the right atom but were not sent: the check that a click is
still current when it is sent expected the relay's own appearance, not its overlay, to carry the render source. It
now accepts an overlay or underlay too, and those three clicks are tested with the rest (230).

## Not covered

A relay with an icon of its own larger than a tile at a RIGHT or TOP edge, a relay without PASS_MOUSE as the first
link of a chain, a chain whose second relay stands on a turned plane of its own, filters with a matrix on a chain link,
an overlay relay without PASS_MOUSE, a relay among the overlays of a relay, an image or a vis_contents member with a
matrix of its own through a relay, hover and drag. Clicks through an overlay relay are not sent (above).

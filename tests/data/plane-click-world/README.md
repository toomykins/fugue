# Clicks under filtered and turned planes

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-25, port 18840. No game assets in Git.
`prepare.py <dir>` writes the world, a 224x224 skin (a 7x7 view of 32 px tiles) and solid icons: a floor, 16 px boxes at
8..23 (`box.png` red, `blue.png`, `green.png`) and `mask.png` (the left half of a tile opaque).

A HUD plane master (plane 1, `screen_loc = "1,1"`) gets a matrix, filters or both from the case name (see
`set_master()`). On plane 1 stand a red box `part` at (5,4); a client image (`green.png`, plane 1) on the iconless obj
`holder` at (3,4); and a vis_contents member `child` (`blue.png`, plane 1) of the iconless obj `visholder` at (4,6). On
plane 0, `leaner` at (2,6) is the red box turned by 45 degrees. Case words: `turn90` (Turn(90)), `scalex2`
(matrix(2,0,0,0,1,0)), `outline1` (outline of 1), `tree45` (an overlay on the master, `blue.png` at pixel 64,32 turned by
45 degrees, as master-transform-world's `child-turn`), and alpha masks moved 32 px right from an icon (`iconalpha`), from
an atom's render target (`rtalpha`: `maskatom`, render_target `*maskrt`, whose icon is `mask.png`) or, unmoved, from a
plane (`ptalpha`: plane 2's master has render_target `*maskplane`, and `mask.png` stands on plane 2 at (5,4));
`inverse` adds MASK_INVERSE.

Words of the second round (2026-11-26): `selfturn` (the client image and the vis_contents member turned by 45 degrees
on their own), `spin90` and `grow2x` (the master's matrix animated to `turn90`'s or `scalex2`'s, as
master-transform-world's `ax-*` cases: half a second, then ten minutes held at the end value), `slide` (the master's
first filter's x animated from 32 to 40 the same way), `rtturn` and `rtoutline` (`maskatom` turned by 90 degrees, or
given an outline of 4), `ptturn` and `ptoutline` (the same on plane 2's master), `selfmask` (the part's own alpha
mask from `*maskrt`, moved 4 px right) and `relay` (plane 1's master gets `render_target = "*p1"` and a screen object
`relay` on plane 5 draws it with PASS_MOUSE, as in plane-relay-world).

Every atom logs `Click()` with its name and icon-x/y, and has one verb, `poke_<name>`, so that an entry of the right-click
menu is known by its verb (stock draws the entries' names itself, so they do not read back). Since the second round
every atom also logs MouseEntered, MouseMove, MouseDown, MouseUp, MouseDrag and MouseDrop (the atom dragged over, and
icon-x/y); the first round's cases were recorded without them.

## How the data was taken

`tools/stockprobe/stockprobe.py --world tests/data/plane-click-world --map 224x224 --params set={} --still 0.5 --live
click @<script>`, one `<case> ACTION...` line per case: left clicks `X,Y` and right clicks `right:X,Y menu:` at MAP
pixels, in stock and in Fugue on one server. `clicks.py <output> 516.1689 2026-11-25` keeps stock's answers in
`clicks.json`: per click the atom and its icon-x/y, per right click the menu's entries, topmost first.
`stockprobe.py --world tests/data/plane-click-world --map 224x224 --still 0.5 --jobs 3 record <case>...` wrote
`replay/<case>.txt` (what DreamDaemon sent the client for the case), `replay/res/` and `replay/<case>.png` (stock's map).
`mapinput_test::planeClicksLikeStock` replays each case without a server and asks the real hit test and menu stack
for every point in `clicks.json`, six seconds after login (so an animated case is at the end of its first step, held).

The second round's twelve cases were clicked the same way twice, before and after the change below (`clicks.py
<output> 516.1689 2026-11-26 --add` added them), and recorded with `--jobs 3 record`: stock answered the same both
times, Fugue's grab matched stock's in every pixel, and after the change Fugue answered every click and menu as stock.
The `--live` check flags such runs as in doubt only because stock logs MouseEntered where the real pointer happens to
be when a menu closes. Hover and drag: `stockprobe.py ... --live --stock-only --cursor click` on `outline1`, `turn90`
and `relay`, kept by `clicks.py <output> 516.1689 2026-11-26 --gestures` in `gestures.json`.

## Independent conclusions

- **A client image and a vis_contents member on a filtered or turned plane are clicked like the plane's own atoms**:
  by their ink where they stood (`outline1`: the outline round them falls through), and under a matrix where the plane
  draws them, with their own icon coordinates (`turn90` at 120,79 reads `holder` x=16 y=24; `scalex2` at 96,47 reads
  `child` x=9). The image's click names the atom it is on. The menu lists them there too.
- **A mask from a render target hides from clicks what it masks out**, as a mask from an icon does: an atom's target
  centred on the plane and moved by x, y (`rtalpha`: 139,111 is `part`, 147,111 the floor, the image and the member
  are hidden), a plane's target where it is drawn (`ptalpha`), MASK_INVERSE hides what it keeps (`*-inverse`), and
  under a matrix the mask applies before the matrix (`turn90-rtalpha`, `scalex2-ptalpha`). The plane that is only the
  mask's source takes no clicks itself (`ptalpha` at 139,111: stock names `part`, not what stands on plane 2).
- **The right-click menu takes the pointer back through a plane's matrix as a left click does**: the pixel whose corner
  the inverse lands in, not the pixel whose centre does. Under `turn90` the part's tile is listed at x=128 and not at
  x=96 (columns 97..128), with rows 128..159 either way.
- **The menu lists a part that carries a matrix of its own where its turned icon is, not over its bounds**: `tree45`
  at 66,162, a corner of the turned overlay's bounds, lists only the floor; 70,166 lists the master. So does `leaner`
  on plane 0 (34,34 and 38,38), and the overlay under a plane scale (`tree45-scalex2`, nine points along an edge).
- **The menu ignores alpha masks**: `iconalpha` at 147,111 and `turn90-rtalpha` at 111,148, both masked out of left
  clicks, list `part`. The menu is otherwise rectangles: a transparent part of an icon is listed.
- An iconless holder of a vis_contents member is listed at its own tile with the member (`plain` at 111,47).
- **An image or a member with a matrix of its own on a turned or scaled plane is clicked where its turned icon is
  drawn**: the pointer goes back through the plane's matrix, then its own, as for a master's overlay
  (`turn90-selfturn` at 121,79, outside where the unturned box would be, reads `holder` x=9 y=22; 105,73, inside that
  but off the turned box, is the floor; `scalex2-selfturn` at 132,47 reads `child` x=23 y=24). The menu lists it where
  its turned icon is (105,73 lists `holder`, the icon's corner being turned over that point).
- **A master held at the end of an animation is clicked as the settled master with the end values**: the matrix
  (`spin90`, `grow2x` give `turn90`'s and `scalex2`'s answers and menus, and the plane's unturned places are the
  floor) and a filter's parameter (`iconalpha-slide` at 147 and 150,111 is `part`, which the unslid mask hides).
- **A render-target mask's alpha for clicks is its source's parts with their own matrix, but without their filters**:
  `rtalpha-rtturn` hides the bottom half of the part (the mask's left half turned to the top), and so does a plane's
  master's matrix (`ptalpha-ptturn`: the turned mask covers no part of the box, so 139,111 is the floor); an outline of
  4 on the source, atom or plane, adds nothing (`*-rtoutline`, `*-ptoutline` at 146,111 are the floor).
- **An atom's own alpha mask from a render target hides from clicks what it masks out**, centred on the atom's icon
  and moved by x, y (`selfmask`: 146,111 is `part`, 149,111 the floor), MASK_INVERSE hiding what it keeps
  (`selfmask-inverse`); the menu still lists the atom (149,111).
- **Drags and hover name what a click names.** With the real pointer moved (`gestures.json`), MouseDown, MouseDrag's
  atom dragged over, MouseDrop, MouseUp and the MouseEntered stock sends after a drop give, at every point, the atom
  and icon-x/y that `clicks.json` holds for a click there: on the outlined plane (`outline1`: 71,111 through the
  outline is the floor x=8), on the turned plane (`turn90`: `holder` x=16 y=24 at 120,79) and through a PASS_MOUSE
  relay (`relay`: the part, the image's holder and the member). A client image and a vis_contents member are dragged
  and dropped on like any atom (`DRAG holder over=child`, `DROP child over=floor`). A posted pointer move sent no
  MouseEntered or MouseMove here, with or without the real pointer, so MouseMove was not measured. (`hover-world`
  later found a posted move taken at its own point with stock's window on top; why these gave nothing was not
  settled. Hover itself is measured there.)

## Fugue

Before: the image and the member on a filtered or turned plane were not clicked at all (the floor was), a render-target
mask hid nothing and the plane that was its source took clicks (`ptalpha` at 139,111 named `maskplanepart`), the menu
took the pixel whose centre the inverse landed in (it listed the part at 96,144 and not at 128,144), listed a turned
part over its whole bounds and left out what an icon mask hid. After: every click and every menu in `clicks.json`
matches stock (77 clicks, 50 menus).

Second round, before: an image or a member with a matrix of its own on a turned or scaled plane was neither clicked
nor listed (the floor was), a plane's mask ignored its master's matrix (`ptalpha-ptturn` at 139,111 named `part`),
and an atom's own render-target mask hid nothing. The animated masters, the turned atom's mask and the outlined
sources already matched. After: all 130 clicks and 67 menus in `clicks.json` match stock.

Hover and drag go through the same hit test as a click in Fugue (`MapWidget::pointerAt`), so they name the same atom
and icon-x/y. Fugue used to send no MouseDrag or MouseDrop whose dragged or dragged-over atom was a client image's
atom, a vis_contents member or reached through a plane relay; it now sends them as stock does, and
`mapinput_test::planeDragsLikeStock` replays the recorded drags and drops of `outline1` and `turn90` (the `relay`
case has no replay here, so its drags are covered only by the check that such a drag is sent). A relay that takes the
pointer itself (no PASS_MOUSE) still takes no part in a drag and gets no hover: stock was not measured there.

## Not covered

An image overlay on a vis member, a plane render target that is hidden (`*`) but consumed by nothing, MouseMove,
drags and hover on a relay without PASS_MOUSE, mid-flight clicks on an animating master, and a mask
source turned other than by a quarter or with other filters than an outline.

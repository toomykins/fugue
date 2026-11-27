# Clicks

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18650. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin, a floor, a turf icon and three boxes (`box.png` red 16x16,
`blue.png`, `small.png` yellow 8x8). Every atom logs `Click()` and `MouseDown()` with src, usr and icon-x/y.
`tools/stockprobe/stockprobe.py click` left-clicks the points `compare.py points` prints, first in the stock window, then in
Fugue's, on one server (run both with `KEEP=1 pair.sh <dir> <port> a "Click probe"`); `compare.py <dir>` pairs the
CLICK lines and writes `stock.json`. `entitymap_test::clicksLikeStock` and `viscontents_test::turfMembersClickLikeStock`
consume it. The first point is a throwaway floor click.

## Independent conclusions

- **Turf copies.** A turf in an obj's vis_contents is clicked as that turf (`copied-turf`), and an obj standing on it as
  that obj (`copied-content`). Location is the holder's turf. (Fugue drew these but never hit them.)
- **Mob members** are clicked as the mob; usr stays the clicking client's mob. **VIS_INHERIT_ID** members as the
  holder; plain members as themselves.
- **Transformed vis members** are hit through their matrix, with inverse-mapped icon coordinates: a Turn(45) member at
  (16,5) reads x=9 y=24, its untransformed corner (9,9) falls through to the floor; a 2x-wide member at (2,16) reads
  x=10. A turned screen object behaves the same (already so in Fugue).
- **KEEP_TOGETHER.** Root and overlay ink both hit as the root, with root-relative coordinates (overlay at x=31). With a
  matrix (`kt-turned`, Turn(90)) the pointer is mapped back through it first: (16,12) reads x=13 y=16, the turned
  overlay at (16,30) reads x=31.
- **Filter ink is never hit**: outline, drop_shadow, an image's own outline, wave-displaced ink and a KEEP_TOGETHER
  group's outline all fall through to the floor; the atom's own icon ink hits. A turned filtered atom
  (`filter-turned`, Turn(45) with an outline) hits its box through the matrix ((16,10) reads x=12 y=21).
- **Images** are clicked as their target (icon coordinates from the image's own icon). On a turned target every part of
  the image tree reports the target's icon through the target's matrix about the tile centre: the image's overlay at
  (29,15) reads x=16 y=29, though it is drawn turned about its own centre.
- **Maptext ink** is not hit at the pixels both clients draw (`maptext`, `maptext-big`); the floor answers.

## Fugue after the change

Turf copies and their contents are hit (the click names the copied turf; currency checks the list still holds that
turf and that the content still stands on it). Transformed vis members are no longer refused. Group parts are hit like
ungrouped parts; a turned group maps the pointer through the inverse of its quads' matrices first. Image trees on a
transformed target report coordinates through the target's matrix. All 33 compared clicks match stock (src, icon-x/y
and location).

## Not covered

Maptext ink at pixels only one client draws (Fugue's glyphs differ; stock clips a 24pt line Fugue draws). Mouse
opacity 2, drag/drop, hover and right-click menus on these shapes. Turf copies seen through a HUD holder or another
turf member's vis_contents (validated, not clicked in stock). A KEEP_TOGETHER group with alpha 0 or a colour matrix.

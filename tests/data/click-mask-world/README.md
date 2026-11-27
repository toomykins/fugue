# Clicks on masks, groups and filtered planes

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-23, port 18714. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin, solid icons, and `points.txt` / `labels.txt`: the points to
left-click in the map (`tools/stockprobe/stockprobe.py click`), first in stock, then in Fugue on the same server. Every atom
logs `Click()` with the clicking key, its name and icon-x/y. `mapinput_test::opacityAndRenderedStacks` consumes the
opacity-2 conclusion.

## Independent conclusions

- **Alpha masks hide from clicks what they mask out.** On an atom's own filter list (`mask-icon`, `mask-inverse`,
  `mask-swap`) and on a plane master (`plane-mask` with MASK_INVERSE shows and hits; `plane-mask-hide` hides and falls
  through). A half-alpha mask pixel still hits.
- **A plane with a colour filter, a blur or an outline is clicked by its ink** (`plane-color`, `plane-blur`,
  `plane-outline`); the outline's and blur's own pixels are not hit. A plane master with alpha 0 still hits.
- **An image composed in its KEEP_TOGETHER target's surface** reads the target's icon coordinates, like an overlay
  (`img-kt`: the image at pixel_x 8 reads x=21 where its own icon would read 13).
- **`mouse_opacity = 2` on a turned atom is the turned icon rectangle**, flat or grouped: a corner of its bounds falls
  through, and coordinates are mapped back through the matrix (`flat-turn-opaque2`, `kt-turn-opaque2`).
- **Particles are not clicked** (`particle`); the holder's ink under them is.
- A plane master's translation moves what is clicked with what is drawn (`plane-shift`).

## Results

| point | stock | Fugue |
|---|---|---|
| `throwaway` | floor x=13 y=14 | same |
| `img-kt@20,20` | img-kt x=21 y=12 | same |
| `img-kt@6,6` | img-kt x=7 y=26 | same |
| `img-kt@30,16` | img-kt x=31 y=16 | same |
| `mask-icon@8,20` | mask-icon x=9 y=12 | same |
| `mask-icon@22,22` | floor x=23 y=10 | same |
| `mask-icon@22,11` | mask-icon x=23 y=21 | same |
| `mask-inverse@8,20` | floor x=9 y=12 | same |
| `mask-inverse@22,22` | mask-inverse x=23 y=10 | same |
| `mask-swap@8,20` | mask-swap x=9 y=12 | same |
| `mask-swap@22,22` | floor x=23 y=10 | same |
| `particle@16,1` | floor x=17 y=31 | same |
| `particle@16,4` | particle x=17 y=28 | same |
| `particle@16,16` | particle x=17 y=16 | same |
| `kt-anim-turn@16,16` | kt-anim-turn x=17 y=16 | same |
| `kt-anim-turn@5,5` | floor x=6 y=27 | same |
| `kt-anim-turn@16,2` | kt-anim-turn x=7 y=26 | kt-anim-turn x=6 y=26 |
| `kt-turn-opaque2@16,16` | kt-turn-opaque2 x=17 y=16 | same |
| `kt-turn-opaque2@5,5` | kt-turn-opaque2 x=1 y=16 | same |
| `kt-turn-opaque2@16,2` | kt-turn-opaque2 x=7 y=26 | same |
| `kt-turn-opaque2@1,1` | floor x=2 y=31 | same |
| `flat-turn-opaque2@16,16` | flat-turn-opaque2 x=17 y=16 | same |
| `flat-turn-opaque2@5,5` | flat-turn-opaque2 x=1 y=16 | same |
| `flat-turn-opaque2@16,2` | flat-turn-opaque2 x=7 y=26 | same |
| `flat-turn-opaque2@1,1` | floor x=2 y=31 | same |
| `plane-shift@30,16` | plane-shift x=23 y=16 | same |
| `plane-shift@6,16` | floor x=7 y=16 | same |
| `plane-shift@16,16` | plane-shift x=9 y=16 | same |
| `plane-mask@16,16` | plane-mask x=17 y=16 | same |
| `plane-mask@6,6` | plane-mask x=7 y=26 | same |
| `two-outlines@2,16` | floor x=3 y=16 | same |
| `two-outlines@16,16` | two-outlines x=17 y=16 | same |
| `plane-blur@16,16` | plane-blur x=17 y=16 | same |
| `plane-blur@3,16` | floor x=4 y=16 | same |
| `plane-outline@16,16` | plane-outline x=17 y=16 | same |
| `plane-outline@3,16` | floor x=4 y=16 | same |
| `plane-color@16,16` | plane-color x=17 y=16 | same |
| `plane-mask-hide@16,16` | floor x=17 y=16 | same |
| `plane-plain@16,16` | plane-plain x=17 y=16 | same |
| `plane-alpha0@16,16` | plane-alpha0 x=17 y=16 | same |
| `plain@16,16` | plain x=17 y=16 | same |
| `plain@1,1` | floor x=2 y=31 | same |

## Fugue after the change

Before: a plane with a colour or mask filter took no clicks at all, masked-out pixels of an atom were clicked, a
joined image read its own icon's coordinates, and opacity 2 on a turned flat atom hit its whole bounds with
out-of-range coordinates. All points now match except `kt-anim-turn@16,2`, a group caught mid-rotation, off by one
pixel in x.

## Not covered

Masks by `render_source` on an atom's own filters are not applied to clicks (on a plane master they are:
`tests/data/plane-click-world`). Hover, drag/drop and right-click menus on these shapes.

# Sight flags

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18627. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and solid icons. The scene has a lit and an unlit area, an opaque
wall with objects behind it, an object with `infra_luminosity = 3` in the dark and objects with pixel offsets reaching
past the wall and out of the dark; the observer has `see_in_dark = 0`. `tools/stockprobe/stockprobe.py pair` runs dd with
`-params set=<plain|blind|infra|pixels>` and grabs both clients at 1:1; `stock.json` keeps the whole-MAP differences.
`mapvisibility_test::infraAndPixelSightDrawLikePlain` and `blacknessSightIsOrdinaryLos` consume it.

## Independent conclusions

- **SEE_INFRA** (64) draws exactly like plain sight here: the infra-luminous object in the unlit area stays unseen.
- **SEE_PIXELS** (256) draws exactly like plain sight here: the pixel offsets past the wall and out of the dark change
  nothing.
- **BLIND** (1) draws the whole MAP black.

## Fugue after the change

SEE_INFRA and SEE_PIXELS no longer mark the visibility frame unsupported (which drew nothing); they take the ordinary
line-of-sight and lighting field. BLIND stays unsupported, which already drew the same all-black MAP. All four sets
match stock pixel for pixel.

## Not covered

SEE_INFRA with see_in_dark or other infra rules (`infra_luminosity` against a nonzero see_in_dark), SEE_PIXELS with
pixel movement (`step_x`/bounds) or partially opaque turfs, and what BLIND does to HUD objects.

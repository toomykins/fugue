# Screen-object animation

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18620. No game assets or screenshots in Git.
`prepare.py <dir>` reuses `transform-tween-world`'s arm, clock markers and skin. Screen objects animate over the same
20 s loop as a map clock marker (bottom row, `pixel_x` 0 to 320), so each grab pairs HUD and map at one instant.
`stock.json` keeps 14 stock frames. `entitymap_test::hudAnimationMatchesStock` samples a HUD alpha tween and a HUD
quarter turn at every stock clock reading; `sampledOccurrenceCompatibility` (hud-root) checks a HUD object is drawn,
unmoved, mid-animation.

## Independent conclusions

- Screen objects animate on the same clock as map atoms: alpha follows 255 × (1 − t) and a Turn(90) tween follows
  90° × t, with or without a parallel step.
- A screen object's own `pixel_x`/`pixel_y` never moves it: a static `pixel_x = 16`, a sliding `pixel_x` or
  `pixel_y` tween and an ended `pixel_x` tween all leave it at its screen_loc. (Pixel offsets inside `screen_loc`
  were not probed.)

## Fugue after the change

Fugue skipped an animating screen object until its chain settled, and refused transformed screen objects outright.
It now samples them like map atoms and drops the sampled root pixel offset; static and turned screen objects draw with
their transform (smoothed like map atoms). Fugue's HUD alpha and arm angle follow the same curves as stock within
3 levels and 1 px.

## Not covered

Screen objects with filters, KEEP_TOGETHER or plane-master HUD trees mid-animation (not compared), and hit-testing of
turned screen objects (not checked against stock).

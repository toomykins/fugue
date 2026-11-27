# A large animated GIF as a turf icon

DreamDaemon/DreamSeeker **516.1689**, 2026-11-26. Synthetic: no game assets, no screenshots.

Games show their lobby splash as a turf whose icon is an image read from disk at run time with `file()`, often an
animated GIF of 480x480 on a 32-pixel map. One such splash is a GIF of 138 frames: its frames take 121 MiB once
composited to RGBA. Stock draws and plays it; Fugue refused every animated GIF whose composited frames exceeded 64 MiB
(or whose frames held more than 16 million pixels), reported the resource as `UNDRAWABLE ... MalformedSource` and drew
nothing in its place.

- `mkgif.py <dir>` writes `splash.gif`: 480x480, 80 whole frames, frame i a solid (8 + 3i, 240 - 3i, 96), 30 cs each,
  looping forever. Composited, 70.3 MiB; 18.4 million frame pixels. `res/77795e41` is that file, as the server sent it.
- `world.dm` (`world.dme`) puts `/turf/splash` at 1,1 and sets its icon with `file("splash.gif")` in `New()`, as a game
  does; the viewer stands at 8,8 with `view = 7`, so the icon covers the whole view.
- `frames.txt` is the server-to-client wire of one Fugue guest session (`FUGUE_RECORD_DIR`), without resource-pump
  frames.

Run: generate the GIF next to the compiled world, start DreamDaemon with `-trusted`, connect.

## Stock

Stock draws the GIF over the whole map and animates it (the grab showed a frame part way through the cycle).
Fugue before the fix drew a black map; after it, the GIF over the whole map, animating.

`AssetRecoveryTest::splashGifTurf` replays the wire and resource through the production scene and checks the turf's
sprite: cell 1,1, 480x480, frame 0's colour, covering the view rectangle, and 80 frames with frame 79's colour.

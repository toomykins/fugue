# Re-armed player glide

`world.dm` moves the player's mob through four northeast steps and three east
steps in `PIXEL_MOVEMENT_MODE`. It uses 32-pixel tiles, `tick_lag = 0.33`, and
`glide_size = 32/3`; the mob has `KEEP_TOGETHER` without `LONG_GLIDE`. The seven
retained move updates arrive at 1188, 1287, 1386, 1485, 1584, 1683 and 1782 ms.

`frames.txt` retains **29 server frames**, spanning **198–1782 ms**. Each line is
`milliseconds type hex-body`, with a four-digit hexadecimal type and no `len=`
field. Equal timestamps preserve file order. The header identifies the sender
only as stock DreamDaemon **516**; the exact build, capture date, timestamp origin
and clock source are not retained. `world.dme` includes the retained world source.

The portable `glidecarry` suite feeds these arrivals through the production
session and samples model positions every millisecond, through 400 ms after the
last frame. Its nanosecond epoch is synthetic. Observations are the map position
and the player's sprite position relative to the view, not rendered pixels.
Replay must decode every frame successfully and observe actual view movement.
The explicit expectations are a map step below two source pixels per sample
(excluding whole-tile origin shifts) and sprite displacement of at most one
source pixel on either axis. A control that discards unfinished glide offsets
must produce a step above eight pixels.

The historical stock behavior is transcribed in `tests/glidecarry_test.cpp`:
re-arming a roughly 141 ms diagonal glide after 99 ms keeps the view and sprite
moving smoothly. No raw DreamSeeker images or motion trace accompany that claim,
and its client version and display conditions are unknown. These retained inputs
and regression assertions support model replay; they do not establish native
presentation timing or pixel parity. Compile `world.dme` in a disposable copy to
repeat the setup, and retain fresh stock observations separately when extending
the claim.

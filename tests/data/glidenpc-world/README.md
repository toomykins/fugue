# Non-player glide catch-up

`world.dm` keeps the player still while two non-player mobs each take six steps
out, rest, then take six steps back. The diagonal mover uses `glide_size = 32/3`;
the cardinal mover uses `glide_size = 4`, slower than its step rate. The source
uses `PIXEL_MOVEMENT_MODE`, 32-pixel tiles and `tick_lag = 0.33`, with
`KEEP_TOGETHER` and no `LONG_GLIDE` on the movers.

`frames.txt` retains **43 server frames**, spanning **100–3729 ms**. Its format is
`milliseconds type hex-body`; four records have an empty body. Preserve records
with equal timestamps in file order. The move updates run from 1254 through
1749 ms and from 3234 through 3729 ms, at 99 ms intervals within each leg. The
header names stock DreamDaemon **516**, but does not retain its exact build,
capture date, timestamp origin or clock source. `world.dme` includes `world.dm`.

The portable `glidecarry` suite replays the recorded arrivals through the
production model, using a synthetic nanosecond epoch. It samples each mover's
offset every millisecond through 1500 ms after the last frame. It requires
successful frame decoding, two legs per mover, an offset no larger than two tiles
on either axis, and settling
within 30 ms of **715 ms** for the diagonal mover and **935 ms** for the cardinal
mover. The latter must approach the two-tile bound; a control without catch-up or
the bound must exceed three tiles.

Those timing expectations are historical transcriptions in
`tests/glidecarry_test.cpp`: stock was last drawn off its tile at 710–720 ms and
930–940 ms, respectively, with a reported 100 fps measurement and 46.4 screen
pixels per tile. The replay uses that scale for its one-screen-pixel settling
threshold.
The comments describe four measured stock legs; this retained world and replay
contain two. No raw stock images, image assets or measurement trace are retained,
nor are the DreamSeeker build, DPI or window geometry. The assertions therefore
check the model against the transcribed expectations, not native rendering or
presentation timing. A new observation requires a fresh stock run and retained
measurement evidence; the current replay cannot supply its own expected output.

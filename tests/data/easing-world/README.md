# animate() easing curves and playback rate

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18501 (fps 10) and 18502 (fps 40).
No game assets or screenshots in Git. `prepare.py` writes the world into a separate runtime directory with
solid-colour 32×32 markers. `stock.json` keeps **146 stock frames × 33 rows**: the linear clock row and the
eased travel fraction of every curve (SINE, CIRCULAR, CUBIC, BOUNCE, ELASTIC, BACK, QUAD, JUMP) with no
direction, `EASE_IN`, `EASE_OUT` and both. `entitymap_test::easingMatchesStock` checks `animation::ease`
against every sample (a pixel plus the local slope times about a pixel of clock error), and
`chainAdmissionAndIngestLifetime` checks that a chain ingested at fps 40 still runs in deciseconds.

## Method

Each white marker animates `pixel_x` 0 → 320 over `time = 100`, then snaps back in 0.5 ds, looping. Red
markers fix both ends of every row, so a row's fraction is read from a frame without knowing the scale. The
linear row in the same frame is the clock, so no wall-clock alignment is needed for curve shapes. Frames are
native-resolution grabs of the MAP's client area every 137 ms with a stopwatch timestamp; `extract.py`
turns them into fractions. Stock DreamSeeker drew at 1:1; Fugue at 1.5× (144 DPI).

## Independent conclusions

- SINE, CIRCULAR, CUBIC, QUAD, BOUNCE and ELASTIC use the conventional Penner ease-in shapes, mirrored for
  `EASE_OUT`, and the half-scaled pair for both. Every sample is within a pixel or two of the model; the
  larger residuals sit where the curve is steep and follow the clock row's one-pixel quantisation.
- With no direction, BOUNCE and ELASTIC ease out (identical to `EASE_OUT` in every frame); every other curve
  eases in and out (identical to `EASE_IN|EASE_OUT`).
- In-out BACK is asymmetric. The first half follows Penner's in-out overshoot constant 1.70158 × 1.525
  (fitted 2.55–2.60, minimum −0.100 at t ≈ 0.24); the second half follows the plain 1.70158 (fitted ≈ 1.70,
  maximum 1.047). Pure `EASE_IN`/`EASE_OUT` BACK use 1.70158.
- JUMP holds one end for the whole step: with no direction or `EASE_OUT` it is at the destination from the
  first sampled frame (t ≈ 0.013); with `EASE_IN` it stays at the start until the step ends; with both it
  jumps halfway.
- Playback is in deciseconds, independent of `world.tick_lag`: the linear row's slope gives a period of
  9985–10000 ms at fps 10 and 9996 ms at fps 40. Before this fixture Fugue scaled the wire time by the tick
  and played the same step in 2.5 s at fps 40.

After the change Fugue's frames of the fps-40 run sit within a median 0.5–1.5 px of the stock samples on
every row (bounce/elastic highest, from interpolating between steep stock samples).

## Not covered

Easing of colour, alpha, transform and multi-step chains is not sampled here; they share the same `ease`
function. Captures are wall-clock grabs, so rates are only known to about 1%.

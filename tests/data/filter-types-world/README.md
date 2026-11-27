# Filter types: wave, ripple, bloom

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18640. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin, a floor, `grid.png` (a 2 px yellow/blue checker with a white
block) and `shape.png` (the filter fixtures' box with a white corner). `tools/stockprobe/stockprobe.py pair` runs dd with
`-params set=<a|b|c>`, stock and Fugue on the same server and grabs both at 1:1; `compare.py <dir> a b c` keeps up to
16 stock pixels per case where the filter changed the plain icon, Fugue's difference from stock per 96 px crop, and
each case's wire records, in `stock.json`. `entitymap_test::filterTypesDecodeLikeStock`,
`entitymap_test::filterParametersTweenLikeStock` and `mapnative_test::filterTypesPixelsLikeStock` (native only) consume it.

## Wire records

Type 5 (wave, mask 0x3d: size, x, y, offset, flags), type 6 (ripple, mask 0x87d: size, x, y, radius, flags, repeat,
falloff) and type 14 (bloom, mask 0x13: size, threshold colour, offset). Floats are f32, the colour is RGBA bytes
whose alpha is the bloom's `alpha`, flags are two bytes. Every parameter is sent even at its default. The record's
own `flags` byte numbers the filter among the list's filters of its type (the bar sign's seven waves arrive as 1..7;
`bloom-outline`'s outline, second in its list, is still 1), so it is not a first/subsequent marker.

## Independent conclusions (stock 516.1688)

A Python model of these rules matched every set a case within 1.5/255 before Fugue changed.

- **Wave**. Consecutive wave records share one pass (at most 10; grouped by flags without
  WAVE_SIDEWAYS; a record with |size| < 0.001 or x²+y² < 1e-6 is consumed without counting). Per wave, with y' the
  screen-down y and L = x²+y'²: `waveDir = (x, y')/L` times the grown surface size, amplitude `sqrt(1/L)*size*(x, y')`
  (WAVE_SIDEWAYS: `(-y', x)`) over the original size, and `fmod(offset, 1)`. The surface grows by the ceiling of the
  summed |amplitude| per axis, at most 32 px; WAVE_BOUNDED grows nothing and clamps sampling to the surface edge
  instead of a transparent border (`wave-bound`, `wave-flags`, `wave-split`: a bounded record starts its own pass).
- **Ripple**. Grouped like waves by all flags. Centre `(W/2 + x, H/2 - y)`, `waveDir2 =
  (|repeat|, falloff*2.5)`, `blurOffset = radius/repeat`, `blurWeight = size`; records with |size| ≤ 0.001,
  |repeat| ≤ 0.001 or falloff < 0 are skipped. The surface grows by the ceiling of the summed size, at most 32 px,
  none with WAVE_BOUNDED.
- **Bloom**. Skipped when alpha is 0 or both size and offset are below 0.001. Keep pixels with a
  straight channel above the threshold; spread them with the outline's 1.75 ladder (nine summed taps,
  renormalized by alpha) for `offset`, blur them with the stock blur on byte surfaces for `size`, subtract the kept
  pixels (RGB reverse-subtract, alpha left alone), then add the result times the alpha byte into the atom's surface
  (RGB ONE/ONE, alpha over). The glow never grows the surface: it is clipped at the icon's box (`bloom-wide`), or at
  whatever earlier filters grew it to (`blur-bloom`).
- **The surface** is the atom's own box (32x32 here), grown by each earlier filter: wave phase and ripple centres are
  relative to it.
- **Animation (set c).** vg's bar sign animates each wave's offset with parallel loops; stock moves the phase
  continuously (fitting the grabs to a modelled phase shift gives about 0.125 per 500 ms for a 4 s step, the same in
  Fugue). Stock draws the same filters on a vis_contents member as on a plain atom.

## Fugue after the change

The decoder reads the three types (any record byte, an instance number); the plan adds a wave/ripple pass (one
per stock group, parameters in a 16-vec4 uniform block) and bloom's threshold, spreadout, blur, subtract and add
passes, all on stock's own surface rectangle inside Fugue's padded capture. Animated filters tween their numeric
parameters and colours when both ends carry the same filter list (parallel steps add their change); other filter
changes still switch at the step's midpoint. Every set a case and set b chain matches stock within 1/255 (bloom
followed by an outline within 3); set c's static seven-wave sign matches within 1, the animated cases differ by phase
only (the clients are grabbed about a second apart).

## Not covered

Displace, rays, motion/radial/angular blur and layer (still skipped, rest of the chain drawn). A negative bloom offset
or negative summed ripple size (stock shrinks its surface; Fugue skips the bloom record, keeps the ripple surface
unshrunk). Wave/ripple/bloom on plane masters with a matrix (refused like the other stock filters there) and mixed with
colour or alpha-mask filters (the whole list is dropped, as before). Whether stock eases filter tweens exactly like
other animated values (only linear steps were checked). `ripple-sign` (the bar sign's ripple at its smallest) and
`vis-ripple` change too little to sample. Clicks on wave/ripple/bloom ink.

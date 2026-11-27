# Parallel steps, followers and offset rounding

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18508. No game assets or screenshots in
Git. `prepare.py` reuses `transform-tween-world`'s arm, clock and skin and adds a corner-dot holder icon.
`stock.json` keeps **147 stock frames** of every case's arm matrix (with offsets in c and f), read with
`transform-tween-world/extract.py`. `entitymap_test::offsetTweensTruncateLikeStock` checks offset tweens
against every usable stock sample.

## Method

Twelve cases on a 3-tile grid animate over a 20 s loop that starts 3 s after the first login, so stock and
Fugue on the same server both see it; each grab reads its own clock row, so frames from the two clients are
paired by equal clock readings. The clock is itself a `pixel_x` tween and shows floor(320 t).

## Independent conclusions

- **Parallel transform steps** compose as Fugue already did (step × base⁻¹ × out): a turn with a parallel 2×
  scale, a parallel quarter turn, a parallel translation matrix, a parallel `pixel_x` move, and a parallel
  1.5× scale started 5 s in with `delay` all match stock within 0.015 in the linear part.
- **Followers**: an image on a target whose offset and transform animate, and a vis copy on a holder whose
  offset animates, follow the holder exactly; an obj with a fixed 90° transform and an offset tween keeps
  its transform.
- **Offset rounding**: stock draws an animated pixel offset at from + trunc((to − from) × f). From 0 this is
  truncation toward zero (−6.6 → −6, 9.6 → 9); from −12 to 12 and from 12 to −12 it truncates the change, not
  the result. Every stock frame fits this except one (clock 0.153), whose clock row is a pixel behind all of
  its tweens. Rounding the result, as Fugue did, fits under half the samples. Fugue's client-offset path
  already truncated this way.

After the change, rebuilt Fugue and stock agree on every pair of frames with equal clock readings
(translation within 0.6 px, linear part within 0.015), apart from that one stale frame.

The earlier `movanim-world` oracle was read at whole ticks; six of its samples lie within half a pixel of a
whole pixel and its rounded values sit one pixel further along each motion, as a frame drawn slightly after
the tick would. `movanim_test` now holds the truncated values for them.

## Not covered

Flicks during a transform tween, colour/alpha rounding during tweens, and vis copies under a holder whose
transform animates (refused by Fugue; see B1).

# Filter reach

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18622. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin, the atom-filter fixture's two solid icons and a small `anim.dmi`
(a still state and a two-frame blink). `tools/stockprobe/stockprobe.py pair` runs dd with `-params set=<a|b|c>`, stock and
Fugue on the same server and grabs both at 1:1; `compare.py <dir> a b c` counts outline and shadow ink around each case
in the stock grab and Fugue's difference from stock per 96 px case crop. `stock.json` keeps both.
`entitymap_test::filterReachLikeStock` and `maptext_test::filteredTextJoinsIconSurface` consume it.

## Independent conclusions

- **Images (set a).** An image's own filters filter the image's own icon: its overlays stay unfiltered
  (`image-overlay`), its alpha and pixel offsets apply as on an atom (`image-alpha`, `image-pixel`). On a transformed
  target the filter runs first and the target's matrix then moves the filtered image (`image-turn`: a drop_shadow at
  x=6 under Turn(90) lands below; `image-scale`). The target's own icon size does not matter: both put a 32 px image on
  a 16 px target.
- **Maptext (set a).** An atom's filters cover its maptext together with its icon, as one shape: the outline runs
  around the union where the text overlaps the box (`maptext`, `maptext-only`, `maptext-overlay`, `maptext-shadow`).
  With KEEP_TOGETHER the text joins the group surface (`kt-maptext`, `kt-maptext-outline`, `kt-maptext-turn`); with a
  matrix it turns with the filtered surface (`maptext-turn-outline`).
- **Animation (set b).** Animating atoms keep their filters mid-animation: pixel offsets, alpha, colour (the outline
  takes the colour too, as for a static colour), transforms (filter first, then the interpolated matrix:
  `anim-shadow-turn`'s shadow turns), flick frames (`flick`) and animated filter parameters (`anim-filter-param`'s
  outline grows).
- **Types not decoded (set c).** Stock draws wave (with and without WAVE_SIDEWAYS/WAVE_BOUNDED), ripple and bloom; Fugue
  drew these atoms unfiltered and kept the rest of the chain (`wave-shadow`). Recorded here before any support; Fugue now
  draws them (see `filter-types-world`).

## Fugue after the change

An image's own filters wrap its icon in a filter surface (the target's matrix moves to that surface); the 32 px target
icon check on transformed targets is gone. Maptext takes the group path, inside the atom's filter or KEEP_TOGETHER
surface. Animating (sampled) and flicking atoms take the group path too. Every set a image case matches stock exactly.
Maptext cases reach the same parts as stock but differ by glyph rasterization (Fugue's text is antialiased and sits
about a pixel off; the same holds without filters, `kt-maptext`). Set b agrees in which parts are filtered; its pixel
differences are animation phase, since the clients are grabbed about a second apart (not paired by the clock marker).

## Not covered

The admin editor's other types (displace, rays, motion/radial/angular blur, layer: still unfiltered). Maptext on a vis copy (refused as
before). Maptext of an ungrouped part with a matrix inside a filtered tree (falls back to the flat path, unfiltered).
An image whose own icon is filtered alongside its own maptext (the text is dropped there as before). Clicks on
filtered images and text (not checked against stock).

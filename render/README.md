# render

Icon decoding, the draw-list IR and scene assembly.

| file | role |
|---|---|
| `dmi.*`, `png.*`, `gif.*`, `jpeg.*`, `inflate.*`, `pngwrite.*` | icon decoding and PNG output (animated GIF plays; JPEG goes through libjpeg) |
| `ir.h`, `executor.cpp` | the draw-list IR (documented in `ir.h`) and its CPU straight-alpha executor |
| `scene.*` | world model and resources to a draw list: grid, line-of-sight cull, plane/layer sort, HUD placement, transforms, glide, `animate()` tweens |
| `xform.*`, `easing.*` | atom transforms and easing curves |
| `pixops.*`, `filters.*`, `goexp.h` | per-layer colour (tint, alpha, colour matrix), blend modes, plane-master filters |
| `ddmi.*` | runtime-icon recipes: the /icon procs DreamDaemon ships for an icon built at runtime, run as DreamSeeker runs them |
| `maptext.*`, `dsmaptext.*`, `font.*`, `maptextpaint.*` | maptext parsing, layout, measurement and drawing |
| `hittest.*`, `mousechain.*`, `screenloc.*` | mouse hit-testing and `screen_loc` placement |

Notes:
- The executor composites each plane on its own transparent surface, then applies per master group, filters, colour and blend, and blends the surfaces down in ascending plane order.
- A colour matrix is 20 floats (rows R, G, B, A, then a constant row in unit scale).
- A plane master runs blur, outline, drop_shadow, an RGB colour matrix and an alpha mask by render_source. Other filters (an alpha mask by icon, displacement, ripple, wave, bloom, and others) are counted in the stats and not drawn.
- A runtime-icon recipe holding an op this client does not run is refused (logged once) and left invisible rather than guessed.
- Maptext glyph shapes come from a host rasterizer; positions follow DreamSeeker's layout.

## Scene preparation

`SceneCache` (optional argument of `assembleMapControl` / `assembleSecondaryMap`, one per session thread) keeps parsed
appearances across frames, keyed by appid and the complete appearance JSON, so a recycled or edited id is a miss. It is an
LRU bounded by entry count and payload bytes. Placement, resources and hit scenes are rebuilt every frame. `scene_cache`
replays recorded worlds with and without it and requires identical draw lists, hit scenes and pixels, also under eviction.

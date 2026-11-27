# Static affine MAP/INFO fixture

No external game checkout or BYOND asset is needed. Generate an isolated runtime:

```
python -B tests/data/transform-world/prepare.py build/static-affine-runtime
DreamMaker build/static-affine-runtime/world.dme
```

From that runtime directory, run DreamDaemon on a checked unused port (the recorded
check used 14523) with `-safe -invisible`, then connect stock DreamSeeker and
native Fugue sequentially. Use a separate Fugue `--state-dir` and close only
processes you started. Leave existing clients/servers, pager/login settings and
local editor settings untouched. See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

The 3×3 MAP arrangement, top to bottom / left to right, is:

1. Identity, rotation, translation.
2. Reflection, shear, positive axis scale.
3. Overlay identity, rotated root with overlay, rotated overlay only.

INFO lists the same atoms in that order. Compare the asymmetric colored source and
yellow overlay marker, not just bounding rectangles. Click colored MAP pixels and
INFO rows: `events.log` records icon/visual/screen coordinates. The `Update` verb
alternates only the first atom's translation between (0,0) and (9,5), retaining the
source image. Allow initial assets to warm before checking uploads; setting
`FUGUE_FRAME_TRACE=1` only in the probe environment records native upload summaries.

Stock applies the transforms on MAP but ignores them on these INFO rows. Native
Fugue intentionally omits the two transformed overlay MAP trees rather than showing
a partial result at this checkpoint. See
[renderer transforms](../../../docs/RENDERING.md#composition-and-transforms)
for the current bounded scope.

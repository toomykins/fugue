# Named-MAP screen isolation

Independent synthetic DreamDaemon/DreamSeeker 516.1688 fixture; no game assets.
The default MAP has a red world object captured on plane 60 into `*plate`,
relayed on plane 70, plus a green HUD marker at `1:8,1:4`.
A second MAP is named `preview`. Foreign masters reuse plane 60 and `*plate`
with alpha zero, optionally KEEP_TOGETHER or unsupported coordinates.

The extended phases additionally create/destroy runtime `embedded`, with its own
green/blue ink, colliding plane-60 master and `*plate` relay. A KEEP_TOGETHER root
has a blue KEEP_APART | KEEP_TOGETHER child escaping to plane 80. The startup
handshake ignores the preceding client's request file; the declared preview is
hidden during runtime-MAP phases so native sibling stacking cannot hide the test.

Stage and compile:

```sh
python -B tests/data/screen-map-scope-world/prepare.py build/screen-map-scope/server
"C:/Program Files (x86)/BYOND/bin/dm.exe" build/screen-map-scope/server/world.dme
```

Check an unused port, then launch the staged DMB manually with DreamDaemon
`-safe -invisible -logself`. Connect one isolated client at a time and drive the
world's `probe-request.json` controls with fresh tokens. Record native geometry,
DPI and the ordered phase/reset history, then close only processes you started.
See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

The later paired observations below used 96 DPI.

Readiness requires the matching request token, a skin-query round trip and five
ticks after applying each phase. Exactly one isolated client is connected at a
time. Both clients are resized to a 740x300 physical client area at (50,50),
DPI 144, with no subsequent resize/refit. Stock uses fixed controls and no
saved-params; global stock preferences remain unchanged. Fugue uses fresh guest
state. Equal windows do **not** imply equal MAP scale: stock rendered 32px
tiles; Qt rendered 48px tiles.

## Separate scene results (2026-11-02)

Final-code comparison `build/probes/20261102T113941Z-compare-3ea5e445` used
stock/server 516.1688, fresh Fugue guest state, **96 DPI**, a 740×300 physical
client rectangle at (50,50), fixed controls, no saved-params and no subsequent
resize/refit. Both completed all 14 phases with token/query/five-tick readiness.
Global stock preferences were not changed. At this DPI both clients' skin units
and Qt logical pixels matched physical pixels. These histories are separate from
the older 144-DPI comparison below.

| Phase | Stock and Fugue green / blue pixels | Main MAP |
| --- | --- | --- |
| named-open | 25600 / 0 | identical to baseline |
| named-update | 0 / 25600 | identical |
| named-group | 12800 / 12800 | identical |
| named-second-tile | 0 / 6400 | identical |
| named-close | 0 / 0 | identical |
| named-reopen | 25600 / 0 | identical |

Counts use exact RGB in screenshot-relative (264,31)–(424,191). Group halves
are 80×160; the second-tile blue rectangle is (344,71)–(424,151). The full
main-MAP rectangle (8,31)–(232,191), not just color counts, is byte-identical
across these six phases. The preview starts at 1×1 tiles and expands to 2×1
for the explicit second tile. Pixel offsets alone do not expand the viewport
(the original preview-marker phase). No remote-turf or cross-z claim follows.

Automated consumers of these expectations: `namedMapCompositionIsolation`,
`namedMapEscapedGroup`, `namedMapLifecycle` (worker), `namedMapRoutingPixels`
(native D3D11/OpenGL routing, pending-frame picking, retirement and warm-upload
checks), `namedRoutes` (mailboxes), `embeddedMapLifecycle` (browser bridge and
popup ownership), plus named HUD image/vis scope regressions. No screenshot
files are retained in Git.

Earlier attempts are not passes: `8d1e32bd` / `1bdb4c96` found missing named
relay admission (placement compared the raw qualified string); `b81f2bc7` exposed
stale request replay and overlapping native controls in the fixture. Both fixture
issues were corrected, not normalized away. `104bb305` passed lifecycle pixels
at 144 DPI (Qt 1.5× stock); `87c1c721` added the independently matching escaped
group at 96 DPI. One DM compile used the game-only `mutable_appearance()` helper;
it failed, and the accidentally launched older owned DMB was stopped without a
comparison. The corrected standalone constructor compiled without warnings.

## Generated-icon work-budget fixture (2026-11-02)

`prepare.py` additionally generates `stress.dmi`: 1184×1184, 341 states / 1366
cells, including an unused three-frame state. No game assets are used. Two
four-direction selections followed by overlay require **25,274,368 conservative
pixel-work units** in the renderer, exceeding its former shared 16M allowance.
`budget-open` selects green, `budget-update` blue, `budget-safety` scales the
four-direction result to 1024×1024 cells, and `budget-reuse` restores green.
The server's independent `fcopy` exports are uniform green/blue, four directions,
one 32×32 frame; these expectations are consumed by `largeSheetSelection` in
`shell/tests/iconcache_test.cpp`. DDMI recipes, not just raster exports, were
confirmed in the Fugue resource cache. No generated files/screenshots are retained.

Paired comparison `build/probes/20261102T123340Z-compare-d994b623` completed
all 18 phases with stock/server 516.1688, isolated Fugue state, 96 DPI, 740×300
physical client area at (50,50), no post-initial resize/refit, token/query/five-tick
readiness and one client at a time. Both use the declared fixed-control state
policy, not inferred equivalence from window size. In screenshot-relative
(264,31)–(424,191), green/blue counts were:

| Phase | Stock | Fugue |
| --- | --- | --- |
| budget-open | 25600 / 0 | 25600 / 0 |
| budget-update | 0 / 25600 | 0 / 25600 |
| budget-safety | 25600 / 0 | 0 / 0 (intentional hard storage refusal) |
| budget-reuse | 25600 / 0 | 25600 / 0 |

Final-code comparison `build/probes/20261102T124228Z-compare-6b09c58b` repeated
baseline, all four budget phases and close with identical counts and declared
conditions, after the last production-code edit. Both clients were stopped by
verified owned identity, followed by the owned synthetic server.

The complete main MAP (8,31)–(232,191) remained byte-identical to each client's
baseline in all four phases. Safety refusal is client policy, **not** claimed stock
parity. The focused tests additionally check stable refusal/no retries, unchanged
warm image keys/no work or reads, memory-yield stack restoration and dependency
remap/eviction. `deferredGeneratedPreview` checks interleaved main-map movement
and authoritative generated playback during multi-preparation work; stock capture
intervals alone are not a worker-latency measurement.

An earlier six-phase comparison `06bf4062` passed the same pixel/safety checks
with 1358 cells / 25,208,832 work units. The unused three-frame state was then
added to reproduce the exact recorded TG recipe cost; that earlier run is not
misreported as the final fixture. All owned synthetic clients/servers were stopped.
See [asset work and storage limits](../../../docs/RENDERING.md#assets-and-playback).

## Original isolation results

Final native comparison: `build/probes/20261102T103849Z-compare-622ed9bc`.
For each row, the default MAP red mask is byte-identical to its client's baseline.
Green is also identical wherever present, not just equal in area.

| Phase | Stock red/green pixels | Fugue red/green pixels |
| --- | --- | --- |
| baseline | 1024 / 1024 | 2304 / 2304 |
| preview-leaf | 1024 / 1024 | 2304 / 2304 |
| preview-group | 1024 / 1024 | 2304 / 2304 |
| preview-invalid | 1024 / 1024 | 2304 / 2304 |
| preview-marker | 1024 / 0 | 2304 / 0 |
| explicit-short | 1024 / 0 | 2304 / 0 |
| explicit-qualified | 1024 / 0 | 2304 / 0 |
| closed | 1024 / 1024 | 2304 / 2304 |

Mask comparisons use exact RGB red/green inside screenshot-relative,
right/bottom-exclusive default-MAP rectangles: stock (11,45,235,205),
Fugue (11,45,347,285). Captures, ownership, binary identities, query responses,
and history remain in the local comparison artifacts; no UI screenshots are
retained in Git. The table proves isolation within each client, not cross-client
pixel scaling or secondary-MAP rendering parity.

An earlier seven-phase comparison (`20261102T103556Z-compare-ba174c11`) found
that an attempted short/full default-control-name alias was wrong: stock hides
the green marker for both `map:1:8,1:4` and `main.map:1:8,1:4`, whereas the
initial Fugue implementation displayed it. That behavior was removed before
the final run. The first server launch also failed because it omitted the
configured build directory; no server was created by that failed attempt.

Automated regressions: `shell_entitymap`'s `screenMapTargets`,
`screenMapMasterIsolation`, `screenMapOrdinaryRouting`, and
`shell_viscontents`'s `namedMapHudVisIsolation`. The retained fixture can
reproduce native semantics independently of the worker's synthetic expectations.
Those original results covered isolation only. Separate scene support and live
follow-up are recorded below.

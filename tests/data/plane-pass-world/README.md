# World/HUD plane PASS_MOUSE input

Independent BYOND516.1688 input receipts consumed by
`MapInputTest::stockWorldPassMouse`; implementation/limits in
[renderer input](../../../docs/RENDERING.md#presentation-and-input).
This fixture adds no pixel goldens or observational screenshot archive.

## Evidence roles

- `prepare.py` stages a separate synthetic world, extending the stock-shadow
  generator without changing that fixture or external game sources.
- `observations.json`: 13 phases, 39 attempted points, 38 Click / 38 Down / 38 Up /
  36 Enter receipts. One turf alpha hole emitted none of those callbacks.
  Exit callbacks and unrelated phase transitions remain in the archived run;
  this is not a complete hover-sequence or gesture claim.
- `frames-*.txt`: real stock-server wire inputs captured in independent fresh
  network guests. Tests ingest them, prepare worker sprites, pick from the
  immutable scene and revalidate against the model. Resources are byte-identical
  to `../stock-shadow-world/res/` and reused, not duplicated.
- `capture-wire.py`: headless reproduction of those wire inputs. It neither
  drives the mouse nor starts/stops servers; use only an owned synthetic server.

The cases distinguish unfiltered and shadow/blur/outline source masters, object
mouse0/1/2, partial/zero source alpha, zero master alpha, mob/turf/HUD leaves and
one/two-hop forwarding. Source coordinates are zero-based top-left 224×224:
(70,104) is outside the source, (104,104) is original ink, and (120,120) is a hole
inside its original 32px icon. Opacity2 includes that hole; opacity1 does not.
When the turf itself is captured, there is no floor behind its hole: stock emits
no click and unified-scene picking must not fabricate a flattened-terrain hit.

The source object, mob and turf retain world location/coordinates; a HUD leaf
retains null location. Partial/zero source and master alpha do not erase source
ownership. Generated filter pixels never add clickable coverage.

## Reproduce

Requires Python/Pillow and BYOND516.1688. Stage a fresh runtime:

```
python -B tests/data/plane-pass-world/prepare.py build/pass-repro
```

Compile its `world.dme` with DM. Launch an owned DreamDaemon on an unused port,
**not 1337/13337**, with `-safe -invisible`, cwd set to the runtime. For native
input, launch a separate DreamSeeker client on that port. Record process identity
and isolated state; never stop by name/port. Close only the clients/server you
started. See [manual collection](../../../docs/BUILDING.md#reproducing-fixtures).

For fresh manual input observations, select each explicit case from `cases.json`
through `phase.txt` and wait for its READY acknowledgement and stable geometry.
Move to source points `(70,104)`, `(104,104)` and `(120,120)`, allow hover to settle,
then click and retain the actual EVENT receipts from `events.log`. Record source
calibration, DPI, phase history and foreground ownership. The old broad matrix
capture driver is no longer retained.

The accepted stock data has exactly one absent point: turf/(120,120). Preserve
absence rather than inventing a callback or expected object; arbitrary missing
callbacks do not count as successful observations.

For wire replay, keep only an owned daemon running, with no native window needed.
Build `relay_wire_probe` explicitly (it is excluded from the default build), or
use an existing compatible executable. Configure Qt DLLs on PATH, then:

```
python -B tests/data/plane-pass-world/capture-wire.py build/pass-repro PORT PATH/TO/relay_wire_probe.exe
```

Each case sets a fresh phase token, connects a fresh guest, waits for the helper's
settling window and requires a matching server READY acknowledgement. Verify
`wire/result.json` is complete. Use a new output label/directory for a retry.
Stop owned clients/servers even on failure. Full screenshots, wire dumps and
client state stay under ignored `build/`, not in this directory.

Build the configured tree, then run CTest with `-R '^shell_mapinput$'` for the
focused regression. Native `fugue_tests mapnative stockShadowPixels` additionally checks the
original target receipt and default/OpenGL pixels; see
[test commands](../../../docs/BUILDING.md#tests). Use a separate build directory
if a running client needs its executable; do not stop the user's client.

## Provenance, failures and limits (2026-11-02)

Accepted native input: `build/plane-pass/second/stock/result.json`, 13/13 phases.
Owned fresh guest, existing stock global UI state retained, one restore/resize,
fresh reset, ordered phases/settling, no refit/zoom. DPI144, physical client448×473,
MAP448×448 →224×224 source. Pointer, foreground and geometry guards passed.
Accepted wire: `build/plane-pass/second/wire-headless/result.json`, 13 fresh network
guests with explicit phase acknowledgements, no native GUI history claim.
The native and wire cases use identical staged DM/DME/DMF/DMI sources. All owned
probe servers/clients were identity-stopped; the running Fugue client and game
ports/settings were left untouched. No new live TG interaction was performed.

The first capture stopped at the turf hole because the old harness required a
Click at every stock point. Those completed observations were not substituted for
the later complete run. The following GUI-wire attempt lacked the Debug helper;
its partial output is not the retained replay. The headless wire run succeeded.
Test development also caught a reference-comparison compile error, missing decoder
registration linkage and the fixture's missing unified-scene visibility marker.
The initial Debug build encountered a preexisting `browsehost.cpp:161` warning;
no warning suppression or unrelated source change was introduced. Full Debug
verification subsequently passed 61/63 CTest targets and all 106 native checks;
the two browser targets aborted in Qt WebEngine's headless GPU-initialization
assertion. An auxiliary Release configuration inherited Debug-only library paths
and failed DLL loading; final Release verification uses a fresh tree. These
failures remain distinct from the passing input regressions.

Not established: translated/affine/filtered-consumer relays, arbitrary attachments
or grouped source combinations, mixed-owner routes, full hover lifecycle/menu/
double-click/wheel/drag behavior, other stock versions, or live-game parity.

# Maptext fixture

Stage synthetic assets, compile, check an unused non-1337 port, and launch an owned
server plus a separate stock client:

```sh
python -B tests/data/maptext-world/prepare.py build/maptext-runtime
# Compile the staged world.dme, then host its DMB with DreamDaemon -safe -invisible.
# Use an unused port and collect native client observations manually.
```

Wait for a **new** `READY` in runtime `events.log` before capture; that file appends
across launches. `Click` logs the specimen name and parameters. All labels have
mouse_opacity=1; the overlay specimen is the only label with a solid icon. Stop
only the clients/server you started. Native Fugue sessions
should use a fresh `--state-dir`.

The 14×10 MAP is 448×320 physical pixels in the retained stock capture. Screen rows
8, 5 and 2 contain default/explicit/bottom-right text; multiline/covered/offset
text; and center/zero-width/legacy-font text. A final blue, alpha-128 label also tests
zero height. All assets are generated solid colors; no game artwork is included.

The former `stock-map.png` was a human-inspection client-area crop of stock
516.1688 at window DPI 144, with no resize/refit. It is not included;
no automated pixel test consumed it. `stock-observations.json` retains crop/ownership provenance, geometry,
readiness and glyph/solid-control click observations. The first capture/click run
preceded addition of the bottom-right tint specimen; the retained image is from the
second run with the final source. This is semantic evidence, not exact Qt glyph or
paired native-client parity. See [maptext limitations](../../../docs/RENDERING.md#maptext-and-info).

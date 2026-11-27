# Client animation timing

This scenario retains DreamDaemon wire for `animate(client)`: overlapping parallel
sequences, a delayed return after a kick, direct pixel-offset changes during a
tween, and repeated directional recoil. The historical test source identifies
BYOND **516.1684**; `manifest.json` records the fixture date and compiled-world hash.

## Inputs and setup

- `world.dm`, `world.dme`, and `mkdmi.py` describe the probe and its flat-colour icons.
- `frames.txt` retains 609 decrypted server-to-client frames. Resource-pump frames
  (`0x009f`) are omitted, as stated in the recording header.
- `res/163d55fd` is the retained `probe.dmi` resource, 320 bytes.
- `manifest.json` supplies the replay origin, tick duration, viewport, frame count,
  and resource inventory consumed by the test. The fixed origin is an arbitrary
  replay epoch, not a timestamp of a stock-rendered frame. The 100 ms tick follows
  `world.dm`'s `fps = 10`.

## Replay and observations

The `clientanim` suite in `shell/tests/clientanim_test.cpp` validates replay setup,
wire/resource inventory and ordered `@N` chat markers before running.
Each named QtTest check starts a fresh `ReplayFixture`. To sample tick T, it
advances that session through frames whose preceding marker is at or before T,
then prepares the production terrain and entity scene at the requested time.
Sampling backward starts a fresh replay. Resource bytes are supplied after wire
ingest; the recording contains no resource-arrival timeline.

`AnimationFixture::raster` paints the resulting flat sprites on the CPU. The test
reads the player's displacement from its unique colour, plus two marker/HUD
pixels, and checks the retained viewport at every sample. The named checks own
their explicit sample times and expected values; there is no second checkpoint
catalog.

The expectations have different sources:

- The numeric offsets and marker/HUD pixels are **historical source-encoded
  expectations**. Earlier comments attributed them to stock behaviour, including
  a 0.49 s kick-to-return measurement. This fixture has no retained stock image,
  trace, or measured checkpoint file with which to independently verify those
  claims. The numeric expectations have been preserved.
- The direct-set case's `viewPixelX` values, 32 before tick 106 and 6 afterward,
  come from the retained WorldState messages.
- The samples at 105.5 and 106.5 check **Fugue invariants**: sampling must neither
  ingest a later event early nor replace the last event's arrival clock with the
  sample clock. They add no inferred stock pixel expectations.

## Scope and follow-up observations

This is a portable temporal replay check. It covers production wire ingestion
and scene preparation with a limited CPU adapter. It does not establish GPU
composition, native presentation timing, or input delivery.

The missing evidence is an independently retained stock observation at the
existing checkpoints, tied to the same world and tick markers. A future stock
capture should retain its version, setup, measured output and timing uncertainty
before replacing any historical expectation. GPU or native checks can then use
that evidence where the behaviour crosses those boundaries.

The separate [direct-set observation](../clientanim-directset-world/README.md)
now retains fresh stock pixels for three checkpoints of that isolated behavior.
The same QtTest suite consumes them in `stockDirectSet`; they do not replace or
establish provenance for this older recording's other expectations.

Build, then select the suite with
`ctest --test-dir build -R '^shell_clientanim$' --output-on-failure` (add
`-C Release` for a Windows multi-config build). Platform setup is in
[test profiles](../../../docs/BUILDING.md#test-profiles).

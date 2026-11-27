# Movable animation chains

This small world exercises `animate()` on objects, mobs and a client image:
sequential and parallel steps, replacement heads, delays, finite and infinite
loops, repeated kicks, colour, alpha and transforms. The historical test source
identifies DreamDaemon **516.1684**. `manifest.json` retains the fixture date and
compiled-world fingerprint.

## Inputs and checkpoints

- `world.dm`, `world.dme` and `mkdmi.py` describe the probe and its flat-colour icons.
- `frames.txt` retains 844 server-to-client frames, excluding resource-pump frames
  (`0x009f`) as its header states.
- `res/9fdb5839` is the retained 493-byte `probe.dmi` resource.
- `manifest.json` supplies the replay epoch, 100 ms tick, suite start at tick 11,
  416x416 viewport and wire/resource inventory consumed by the test. The epoch
  is a deterministic replay input, not a stock presentation timestamp.

Each named QtTest check in `shell/tests/movanim_test.cpp` declares its checkpoints
in seconds after tick 11. The preceding `@N` chat marker assigns tick N to each
wire frame; frames before the first marker arrive at replay tick zero. Fractional
checkpoints sample between arrivals without changing an event's arrival time.
The 100 ms tick follows the world's `fps = 10`.

## Replay and expectations

Each check starts a fresh `ReplayFixture`. Forward samples advance one session
and its production scene caches through the recorded events; sampling backward
resets both before replay. This also keeps checks independent when selected or
reordered. Resource bytes are supplied after wire ingest because their original
arrival timeline was not retained.

The test validates setup, ordered tick markers and wire/resource inventory.
`ReplayFixture` prepares the production terrain and entity scene at each sample;
`AnimationFixture::raster` paints its flat sprites on the CPU. The test observes
each uniquely coloured slot's position, width or centre pixel, checks the viewport,
and compares those observations with the explicit values and tolerances beside
the checkpoints.

Those values are **historical source-encoded expectations**. Earlier source
comments attributed them to stock DreamSeeker pixel observations, but this fixture
retains no stock images, measured checkpoint trace or display/timing conditions
that independently establish those claims. The source also documented six
one-pixel corrections near rounding boundaries: the retained values truncate the
eased change toward zero; the prior rounded values were described as consistent
with a frame drawn slightly after its nominal tick. All existing positions,
colours, widths and tolerances are preserved. These corrections are not newly
captured stock observations.

## Scope and further observation

This is a portable replay of production wire ingestion and scene preparation
through a limited CPU raster adapter. GPU composition, native presentation and
input delivery are outside its claim.

To strengthen stock parity, observe this world at the declared checkpoints and
retain independent outputs with the version, action order, display conditions
and timing uncertainty. Include the rounding-boundary samples. That evidence
should establish any later changes to expectations or additional GPU/native
checks; the existing wire alone cannot establish stock-rendered pixels.

Build, then run `ctest --test-dir build -R '^shell_movanim$' --output-on-failure`
(add `-C Release` for a Windows multi-config build). Platform setup is in
[test profiles](../../../docs/BUILDING.md#test-profiles).

# vis_contents animation

This small world exercises finite and looping icon states shared through turf
`vis_contents`, state changes, additional holders and list ordering. The manifest
identifies stock DreamDaemon **516.1684**, recorded through the Fugue client.
These retained server inputs are independent of Fugue's scene preparation; no
stock rendered output or client presentation timeline accompanies them.

## Inputs and checkpoints

- `world.dm`, `world.dme` and `mkdmi.py` describe the probe and its coloured icons.
- `frames.txt` contains 53 server-to-client frames, excluding resource-pump frames
  (`0x009f`) as stated in its header.
- `res/3b42c8e8` contains the 515-byte `thing.dmi` resource.
- `manifest.json` supplies the replay epoch, tick duration, 288x288 viewport and
  wire/resource inventory consumed by the test. Its date, source and wire hash
  describe the retained recording.

The `PHASE 1` through `PHASE 4` text messages identify the boundaries in the wire.
The test names the resulting checkpoints `Attached`, `IconStateChanged`,
`HolderChanged` and `SecondHolder`. Each checkpoint includes its marker and all
following frames before the next marker, or through the recording's end.

Arrival clocks are deliberately synthetic: setup is tick 0, and the four phases
are ticks 100, 160, 220 and 280. One tick is 100 ms. These preserve the original
test's replay clocks; they are not observed arrival or presentation times. In
particular, the world starts its first phase after a 32-tick sleep, while replay
places that phase at tick 100. The later six-second phase intervals match the
world's sleeps, without claiming that client delivery had those exact timings.

## Replay and expectations

Each named QtTest check loads a fresh `ReplayFixture`, ingests the retained prefix
through its checkpoint at those phase clocks, then supplies the resource bytes.
Resource-arrival timing was not retained. Production terrain and entity
preparation run at each explicit sample time; `AnimationFixture::raster` paints
the resulting flat sprites on the CPU. This adapter supplies observations, not
independent expected output.

The existing colour and timing expectations stay in `visanim_test.cpp`:

- Finite states progress through red, green and blue, then hold blue. A changed
  icon state restarts its sequence.
- Changes to a holder and additional holders leave an already running shared
  state at its existing time.
- Matching looping states share a clock, and the sampled colours actually vary.
- Reversing the `vis_contents` list reverses the visible top colour.

These are historical source-encoded expectations and explicit shared-clock/list
invariants, backed by the retained inputs and generated icon definitions. There
are no retained stock screenshots or measured output checkpoints that establish
their timing as stock parity. A new stock observation should retain those outputs
and timing conditions before strengthening that claim.

## Execution scope

This is a portable wire and scene-preparation check with a limited CPU raster
adapter. GPU composition, native presentation and input delivery are outside its
claim. Build, then select it with
`ctest --test-dir build -R '^shell_visanim$' --output-on-failure` (add `-C Release`
for a Windows multi-config build). See [test profiles](../../../docs/BUILDING.md#test-profiles)
for platform setup.

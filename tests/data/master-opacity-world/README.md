# Unfiltered opacity-2 master fallback

Independent Windows BYOND **516.1688** fixture: one iconless identity/unfiltered
HUD master at `1,1` on plane 1, one ordinary 32px world member at `(2,2)`, and a
plane-0 floor. It covers opacity 0/1/2, not filtered masters or general trees.
Captured player display names use the same-length synthetic `TestGuest`; all other
wire bytes are unchanged. This does not establish guest-account isolation.

The claim is static picking and composition at each of `control`, `two` and
`zero`. Each phase is an independent terminal checkpoint. Original frame arrival
times and stock presentation times were not retained; no timing claim is made.

## Observe

The small world, its generated assets and the retained stock observations define
the scenario. `stock/result.json` records the action order, resize/reset history,
state policy, coordinate units, executable/world fingerprints, 144-DPI window
geometry and physical MAP bounds. The current `world.dm` matches the retained
source fingerprint. The stock capture used existing global state; the synthetic
player name is not evidence of account isolation.

The world's `READY <token>:<phase>` line acknowledges the server's state change.
No READY token, capture delay, or client presentation acknowledgement survives in
this recording. The replay contract added to the record describes how to consume
these static observations; it does not supply missing historical timestamps.

## Retain

- `stock/result.json`: setup, phase checkpoints and ordered actions consumed by
  the `masteropacity` suite, including **45 mouse receipts and nine Menu
  receipts**. Each menu action also retains its expected target stack.
- `stock/frames-{control,two,zero}.txt` and `stock/res/`: independent wire/assets.
- `stock/{control,two,zero}-map.png`: the three stock pixel goldens consumed by
  the GPU and native profiles of the `masteropacity` suite.
- The menu stacks are the existing manual transcription of original popup
  observations, moved from the test source into the action records. The source
  popup images were not retained. These expectations were not newly captured or
  derived from the current renderer or Click callbacks.

The mode-2 iconless master owns alpha holes/empty plane space behind member ink;
member ink remains member-owned. In mode 1 those holes hit the floor. Menu stacks
remain independent: member then floor in the member rectangle, floor elsewhere.
The three stock PNGs are identical: mouse opacity changes picking, not the drawn
member. Replay therefore checks phase identity and receipts as well as pixels.

The stock observer and wire-recording client have separate HUD masters:
`world.dm` creates one on each login. All three wire captures name master object
2; stock receipts name master object 1. `replay.masterRefs` records this
correspondence from the retained wire, receipts and world definition. The test
verifies the wire identity before comparing the corresponding stock reference;
the original receipts and wire remain unchanged.

## Replay

Start a fresh production session for each phase, ingest its complete retained
wire and resources, and prepare the scene at the declared static replay clock.
That clock is a deterministic test input. It is not the time of the stock grab.
The test validates the source size, phase identity and required expectation data
before comparing observations.

Portable replay compares selected targets and mouse fields with stock receipts,
and the production menu stack with the retained stack. A Menu receipt identifies
the selected target's `inspect` verb; it is checked separately from mouse callback
fields. GPU replay compares every RGBA pixel with the phase's independent stock
PNG, with no tolerance.

Stale lifetimes, fallback refusal, pending-versus-rendered ownership and zero
warm uploads are explicit Fugue engineering invariants. They do not acquire stock
timing provenance merely by sharing the fixture.

The `masteropacity` suite in `shell/tests/masteropacity_test.cpp` runs in `fugue_tests` through three CTest entries:

- `shell_masteropacity_portable`: 45 stock mouse receipts and nine menu stacks, plus fallback bounds and stale lifetimes.
- `shell_masteropacity_gpu`: production QRhi output compared exactly with each stock PNG, without a MapWidget.
- `shell_masteropacity_native`: MapWidget pixels and Qt-delivered input on the native default backend and OpenGL,
  including pending versus rendered publication and zero warm uploads.

All profiles use the scenario's `MasterOpacityFixture` in the same test file,
which builds the scene through the shared `ReplayFixture`.

Build first, then run `ctest --test-dir build -L master-opacity --output-on-failure`, or select one entry with `-R`.
Windows multi-config builds also need `-C Release` and the configured build directory. Required capability failures
are failures, not passing skips. See [profile setup and commands](../../../docs/BUILDING.md#test-profiles).

The GPU profile uses software D3D11 on Windows and OpenGL on Linux/macOS. The native profile requires a window-system
connection and checks both the platform's default backend and OpenGL. Linux can use Mesa with Xvfb.

## Check native integration

Native replay publishes each phase into a real MapWidget, waits for frame
submission and reads back its framebuffer before delivering input. It samples
the 224x224 source pixels from the scaled widget at the current device pixel
ratio. This establishes Fugue's source-pixel and input mapping; it does not
reconstruct the original stock desktop's resize/DPI history.

After submission and framebuffer readback, a newer pending empty scene must not change input ownership.
An actually rendered replacement must retire the old target. Both native-default
and OpenGL runs compare against the same retained pixels and actions.

General affine/effected masters, image opacity-2, hover/drag, desktop mouse
automation, visible popup UX and live server callbacks are outside this claim.
In particular, inspecting the production menu candidates does not demonstrate a
native popup being opened or its verb being executed on a server.

## Reproduce the observation

Requires BYOND and Python/Pillow:

```sh
python -B tests/data/master-opacity-world/prepare.py build/master-opacity-repro
```

Compile the staged `world.dme` and host it with DreamDaemon `-safe -invisible` on
an unused isolated port. For each of `control`, `two` and `zero`:

1. Write a fresh `<token>:<phase>` to `phase.txt` and wait for the matching READY
   line. With `popup.txt` absent, collect the listed left-click actions and their
   Down/Up/Click receipts.
2. Observe the stock MAP after the phase has been applied and retain its source
   pixels. Record the actual capture/completion condition and delay in the new
   observation; do not treat READY alone as proof of client presentation.
3. Create `popup.txt`, write a new token for the same phase, and wait for its
   matching READY. At the retained menu points, record the displayed target
   stack and invoke `inspect` on the selected target to collect its Menu receipt.
   Remove `popup.txt` before the next phase's left-click actions.

Keep a new capture's version, action/checkpoint order, display geometry and
resize/reset history with its independent expectations. Unknown old timing stays
unknown; a fresh capture must not silently rewrite that history. Use separate
state and matched display conditions for Fugue comparisons. Close only processes
you started. Normal automated replay needs neither BYOND nor a live server.

See [renderer input contracts](../../../docs/RENDERING.md#presentation-and-input).

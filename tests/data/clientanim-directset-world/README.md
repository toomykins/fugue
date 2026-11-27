# Client pixel-offset assignment during a tween

This observation world isolates the direct-set case from `clientanim-world`.
Both clients start `animate(client, pixel_x = 32, time = 20)` together, then set
`client.pixel_x = 6` five ticks later. It leaves the older animation recording and
its historical expectations unchanged.

## Setup and checkpoints

`prepare.py` stages the world, an 11x11 map skin and a single generated DMI in a
separate runtime directory. The source map is 352x352 pixels. Each client sees
the red player at source position (160,160) before camera movement, a small white
world-space clock initially at (0,0), and a 32x32 screen phase cue at the lower
left. The clock is at world (6,16,1), and the player is at (11,11,1). Camera
movement shifts both together, so their relative positions isolate clock motion.
Only the phase cue belongs to `client.screen` and stays fixed in the viewport.

The world logs `READY client-N` for each login without retaining account names.
It waits for two clients and a fresh `capture.txt` trigger from the recorder.
One global sequence then drives both clients in the same server ticks:

- Tick 0: start the client tween and the world clock; the phase cue becomes yellow
  `(220,180,20)`.
- Tick 5: assign the client offset to 6; the cue becomes purple `(180,20,220)`.
- Tick 20: mark the nominal tween endpoint with a green cue `(20,220,80)`.
- Tick 30: finish the clock.

Setup uses a cyan cue `(20,180,220)`. Every update batch is preceded by an `@N`
text marker identifying its server tick. `CAPTURE start`, `CAPTURE assigned`,
`CAPTURE settled` and `CAPTURE end` identify the four action checkpoints; the
same actions and server times are retained in `events.log`. These messages mark
server actions, not proof that either client has presented them.

## Reading observations

The separate world clock animates its white 2x6 marker by 300 pixels over 30 ticks,
with `world.fps = 10`. Read its motion as `clock_left - player_left + 160` to
cancel the camera offset. This relative position identifies elapsed rendering
time in the same image as the player and phase cue. One source pixel corresponds
to 10 ms; pixel quantization alone therefore spans a 10 ms interval.
Capture-call start/end times additionally describe when the host obtained an
image, not the instant when the window presented it. Record any further timing
uncertainty or inconsistent clock/phase samples explicitly.

Read the player's red `(200,40,40)` rectangle to obtain camera displacement:
`pixel_x = 160 - player_left`. The phase cue distinguishes images before and
after the assignment. Keep raw stock images and their clock/phase readings so
expectations can be checked independently; Fugue output must not define them.

This world supports a timed scene-preparation claim. GPU composition, desktop
presentation latency and native input delivery require their own observations.
The capture metadata must retain the actual BYOND version, source/world hashes,
display geometry and timing conditions for each new run.

## Retained observation and replay

`stock/` retains three unmodified stock client-area images from BYOND **516.1689**
at 144 DPI, the corresponding capture metadata, all 101 server frames and their
recorded arrival times, generated DMI/skin resources, and action log. The images
are 352x377 captured pixels; the top-left 352x352 rectangle is the map. No image
resizing or normalization was applied. The selected images are the last running
sample before assignment, the first assigned sample and the first settled sample.
`capture.json` identifies this selection from the larger recording.

`stock/observations.json` records the independently measured clock and player
positions. The three clock positions are 44, 50 and 206 pixels; the observed
client offsets are respectively 7, -18 and 6 pixels. `ClientAnim::stockDirectSet`
verifies those measurements against the retained PNGs, replays the retained wire
at its `@N` arrival ticks, and samples production scene preparation at the midpoint
of each clock-pixel interval. Moving offsets allow one source pixel for clock
quantization and raster rounding; the settled endpoint must match exactly.
This checks camera offsets, phase cues and clock positions through the CPU
adapter; it does not claim whole-image GPU equivalence or presentation latency.

The older `clientanim-world` expectations remain unchanged. This new observation
establishes these three checkpoints for the isolated direct-set case; it does not
retroactively establish the other historical animation expectations.

## Reproduce

Use the shared [stock observation tools](../../../tools/stockprobe/README.md):

```powershell
python -B tools/stockprobe/stockprobe.py --world tests/data/clientanim-directset-world --client build/windows/release/app/fugue-qt.exe --work build/directset-runtime --out build/observations --port 18791 --map 352x352 --settle 1 --duration 4 --interval 0.025 --trigger-file capture.txt capture directset
python -B tools/stockprobe/measure.py build/observations/directset/capture.json --crop 0 0 352 352 --colour player=200,40,40 --colour clock=255,255,255 --colour running=220,180,20 --colour assigned=180,20,220 --colour settled=20,220,80 --output build/observations/directset/measurements.json
```

Use fresh runtime and output paths for each capture. Inspect actual geometry
before choosing a crop. Keep the raw selected images and their metadata when
updating expectations. Automated replay needs no BYOND installation:
`ctest --test-dir build -R '^shell_clientanim$' --output-on-failure` (add
`-C Release` for a Windows multi-config build).

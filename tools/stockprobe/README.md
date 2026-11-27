# Stock observation tools

`stockprobe.py` prepares a small world, starts its DreamDaemon and two clients,
then records observations. It requires Windows, BYOND, Python/Pillow and a built
Fugue with its runtime dependencies on `PATH`. Automated replay tests do not need
these tools or an installed BYOND.

Existing `record`, `pair` and `click` commands handle settled snapshots and input
receipts. `capture` adds a reusable timed recording, independent of a particular
world's actions or expected values:

```powershell
python -B tools/stockprobe/stockprobe.py --world tests/data/clientanim-directset-world --client build/windows/release/app/fugue-qt.exe --work build/directset-runtime --out build/observations --map 352x352 --settle 1 --duration 4 --interval 0.025 --trigger-file capture.txt capture directset-01
```

`record`, `pair` and `click @script` take any number of cases in one run and
compile the world once. Options that are off by default make a batch faster:
`--still` (observe as soon as the drawn scene stops changing), `--live` (one
server and one pair of clients for all cases; the world reboots into each case)
and `--jobs` (several servers at once). `stockprobe.py --help` says what each
relies on; [Batches](#batches) below has measurements. `capture` takes none of
them.

The work directory must be new or empty; the named output directory must not
exist. Change the output name and work directory for another observation. Only
processes started by the tool are stopped. Capture failures retain partial
observations and logs with `complete: false` and an error in `capture.json`.

`hoverprobe.py` drives stock alone with the real pointer for hover: it keeps stock's window on top, moves the
pointer (SetCursorPos), presses real buttons (left, right, middle) and keys (ctrl, shift, alt, Escape), posts moves
and sweeps of moves, grabs the screen around the window on request, and keeps per action what the world logged
(`tests/data/hover-world`). `--timer1` holds a 1 ms system timer resolution in the probe's process while it runs; every
run records the system timer's resolution. It moves the
user's pointer and presses keys in the foreground, puts the pointer back at the end and leaves no key or button held.

The fixture supplies `prepare.py` when its assets or skin need special setup.
It logs `READY <key>` to `events.log` once for each client. After two READY lines
and `--settle`, the optional trigger file receives a fresh token inside the work
directory. The world can use this to start its actions. The trigger is generic;
the world owns what it means and should log its own action/checkpoint markers.

## Batches

One `pair` case run by itself takes 7.7 s on the machine these were measured on:
6.0 s of it is `--settle`, 0.4 s preparing and compiling the world, 0.1 s until
DreamDaemon has opened it, 1.0 s until both clients have logged in, and 0.2 s
grabbing and stopping. A `click` case adds 0.74 s per action and window (0.24 s
posting the messages, 0.5 s for the world to answer). The settle is therefore
what `--still` removes, the start-up what `--live` removes, and `--jobs` divides
whatever is left, the actions included.

Measured 2026-11-25 with BYOND 516.1689 and a debug Fugue, while the machine was
also building; wall time of the whole run, each figure one run:

| | 30 `pair` cases | 20 `click` cases, 44 clicks |
|---|---|---|
| one run per case, as before | 228 s | 216 s |
| `--jobs 4` | 59 s | 59 s |
| `--still 0.5` | 73 s | 114 s |
| `--still 0.5 --jobs 4` | 22 s, 23 s | |
| `--still 0.5 --live` | 43 s, 43 s, 44 s | 99 s, 99 s |
| `--still 0.5 --live --jobs 4` | 14 s, 14 s | 28 s, 29 s |
| the same with `--stock-only` | 12 s | |

The cases were `tests/data/master-transform-world`'s (`--map 224x224 --params
set={}`). In every row all thirty stock grabs were pixel-identical to the ones
kept in that fixture's `filters/`, the client grabs to those of the one-run-per-
case row, and each click's printed lines to that row's and to `clicks.json`.
`tests/data/stock-parity-world` shows what the options cost a scene that moves:
of its thirty sets `--live --jobs 4` reproduced all but `animated-groups` (which
no two runs reproduce), `--jobs 4` alone also missed `sweep`, whose objects step
every 2.5 s, and `--still 0.5` grabbed `sweep` and `animated-render-target`
early, with other pixels than after the whole settle.

`--live` keeps what it shows honest by observing its last case a second time on a
fresh server. A copy of the world changed to read `world.params` in a global's
initial value drew the first case for every later one, and was reported.

A starting client takes the foreground (a `STARTUPINFO` asking for an inactive
window is ignored by both clients), so a batch disturbs whoever is typing once
per case, or once per server with `--live`. Grabs and clicks do not: maps were
read unchanged from windows that overlapped, lay on top of each other, or had
been moved off the screen.

## Retained evidence

Each output directory contains:

- Full client-area images under `stock/` and `client/`, without resizing or pixel
  normalization. A sample index groups successive captures, not simultaneous frames.
- `capture.json`: actual capture-call start/end bounds, requested sampling times,
  missed sample ranges, trigger bounds, per-image window/client geometry and DPI,
  executable versions and hashes, prepared-world hashes and state policy.
- `wire/`: Fugue's existing recorder output and resources. This is the separate
  client's received wire, not a trace of DreamSeeker's presentation.
- Prepared DM/skin sources under `world/`, plus compilation, server, client and
  event logs. Compiled worlds and executables are fingerprinted rather than copied.

Times are nanoseconds relative to `monotonic_origin_ns`. Capture bounds enclose
the window-read call; they do not reveal when its pixels were presented. READY
and the trigger also do not prove client presentation. Use a visible clock or
other same-image cue when a scenario needs to associate pixels with a checkpoint,
and retain the resulting timing uncertainty. Slow capture skips scheduled samples
and records the gaps. Stock and Fugue are read sequentially.

`PrintWindow` asks each window to draw its client area. It does not observe desktop
occlusion or compositor presentation. `--map` is a configured top-left crop for
the snapshot commands; timed capture retains the whole client area and records
that configuration separately from measured geometry. Stock keeps its existing
global configuration; Fugue uses a fresh state directory.

## Measuring observations

`measure.py` finds exact named RGB colour bounds in retained images:

```powershell
python -B tools/stockprobe/measure.py build/observations/directset-01/capture.json --crop 0 0 352 352 --colour player=200,40,40 --colour clock=255,255,255 --output build/observations/directset-01/measurements.json
```

It defaults to stock images and records image hashes and pixel coordinates.
The optional crop is checked against each image; bounds are relative to that crop.
Scenarios interpret these measurements into expectations; the capture and
measurement tools do not consult Fugue output to define a stock expectation.
Retain the source images and provenance with any published measurements.

The scheduling/error-retention checks run without Windows or BYOND:

```sh
python -B -m unittest discover -s tools/stockprobe -p test_capture.py
```

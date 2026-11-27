# Nested CHILD / button sizing: DreamSeeker 516.1688

This synthetic fixture reproduces the relevant *structure*, not game assets:
main vertical CHILD → nested horizontal CHILD → BUTTON pane / label pane.
The nested CHILD has design height 477 in a 480-high pane, `splitter=2`,
`show-splitter=false`; the button pane is 676×30, with 88×27 buttons at y=5,
anchored vertically 0–100%. No saved parameters are declared.

## Reproduction

Stage rather than compile in the source tree:

```
python -B tests/data/splitter-world/prepare.py build/splitter-parity/base
```

Compile the staged `world.dme` with Dream Maker, then launch a separate
DreamDaemon on an unused port with `-safe -invisible` and DreamSeeker
`byond://localhost:<port> -new -guest`. `world.dm` writes `measurements.jsonl`
and finishes with `SPLITTER_DONE`. A second client appends another sequence: do
not merge different clients' replies into one stock corpus.

`prepare.py OUTPUT 60 'can-resize = false'` creates the second observed variant.
A third `prepare.py OUTPUT 60` run independently varied only design height; its
retained file-phase reply again gives a 317×9 pane and 34×0 Menu, eliminating the
resize-property change as a confound.
The old native-window enumerator is not retained. If inspecting native parent/
child rectangles, record them separately from server skin-query replies and
capture only the known client's foreground window under stable geometry.

## Observed rules, not inferred minimums

`dreamseeker-observations.json` retains all 25 phases for each sweep and the
independent height-only file phase, including unmodified per-property replies
and process provenance. These runs were on a
144-DPI Windows desktop; stock `winget(...,"dpi")` answered `1.500000`.

For the baseline 640×440 skin client:

| Hidden splitter % | Button pane height | Menu HWND / winget height |
| --- | ---: | ---: |
| 0 | 0 | 0 |
| 1 | 4 | 1 |
| 2 (file and winset) | 9 | 6 |
| 3 | 13 | 10 |
| 5 | 22 | 19 |
| 10 | 44 | 41 |
| 50 | 219 | 216 |
| 98 | 428 | 425 |
| 100 | 437 | 434 |

- Hidden splitter: `round((client_height - 3) * percent / 100)` in these cases.
  There is **no 10–90% clamp and no default 30px pane/control minimum**.
- Visible splitter at this desktop DPI consumes **5 physical pixels** first:
  the tested pane heights follow `round((437 - 5) * percent / 100)`.
  Fugue still uses a fixed 4 logical pixels; this is a known DPI discrepancy,
  not evidence for changing its constant to 5 universally.
- At splitter 2, changing the main client to 640×240, 640×880, or 1000×440
  gives button-pane heights 5, 18, 9 and Menu heights 2, 15, 6 respectively.
- Changing the button-pane design height to 60 does **not** enlarge its slot.
  Small anchored buttons instead collapse to height zero, rather than reversing
  their corners. At pane height 219 that variant's button is 186 high.
- `can-resize=false` on the pane MAIN was ignored (`winget` still returned true).
  This agrees with the installed reference's “Main (window only)” applicability.
  `min-size` was an exploratory unsupported key: stock always returned empty and
  writes had no effect. It is **not** evidence for a supported minimum-size API.
- Stock native enumeration confirms a 317×9 pane containing a Menu HWND at y=5
  with size **34×6**. Only four rows lie inside its parent. Shrinking the HWND to
  34×4 is not equivalent: it changes text/border layout before clipping.

Native baseline evidence:
`build/probes/20261025T082402Z-0d8cd498/final-native.json` and
`capture-20261025T082802Z-3e932dc2.png` in the same directory.
The initial throwaway attempt lacked an explicit outer `is-vert=true` and a
file log; it was stopped and excluded. The retained fixture declares the axis.

## Fugue regression scope and known differences

`splitter_test` consumes the 32 recorded hidden-divider phases (both design
heights), checking pane and button heights at matching **skin-coordinate** client
sizes. Additional cases cover zero/negative extents, resizing, raw percentage
reporting and file/winset/GUI-setter paths. Those broader combinations are
implementation invariants, not claims of stock mouse-drag verification.

`ShellTest::paneClipsButtonWithoutResizingIt` checks full button geometry, the
paint/input mask, hit-test rejection beyond the pane, zero-pane hiding, growth,
and removal of a stale mask. Only BUTTON geometry changes; other control types
retain their legacy clipped-rectangle behavior.

Native Fugue replay:
`build/probes/20261025T083138Z-80b4606a/`, with replies extracted to
`build/splitter-parity/fugue-observations.json`. File and runtime splitter 2 both
report a 318×9 pane and 34×6 Menu. This is **not** full fixture parity:

- Fugue uses Qt logical pixels: a 640×440 skin client occupies 960×660 physical
  pixels at this DPI; stock's 640×440 skin client occupies 640×440 physical pixels.
  Stock's status strip adds 25 physical pixels; Fugue's adds 18 logical pixels.
- Visible divider widths, some fractional horizontal anchor rounding, and zero
  pane window-size queries differ (Fugue falls back to its design size at zero).
- Runtime `winset(main,"size=...")` did not resize this Fugue main window; native
  replay resize phases therefore do not match. C++ resize regressions explicitly
  call `setClient`, and stock runtime resizing is retained as evidence.
- Native stock mouse-drag endpoints, other DPI modes, other control clipping,
  and Linux/macOS native behavior were not verified here.

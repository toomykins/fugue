# Bottom-left plane-source aliases and CENTER negative control

Handwritten stock DreamDaemon / DreamSeeker **516.1688** fixture, Windows guest,
2026-10-25. Use `prepare.py <disposable-directory>`, compile its `world.dme`, host on
an unused port, then connect a separate `-new -guest` stock client. Capture only the
owned foreground window at each `PHASE` line; finish at `PLANE_SOURCE_PROBE_DONE`.

A 5x3 viewport distinguishes these placements (unlike a 1x1 viewport):

- A magenta marker is shown directly, while a green marker is captured on a hidden
  plane and consumed via a screen object.
- `SOUTHWEST` and `1,1` yield identical green-pixel masks, including with the relay's
  `PASS_MOUSE | NO_CLIENT_COLOR | KEEP_TOGETHER` flags.
- `CENTER` shifts the green source into the upper-right tile. **It is not equivalent
  to bottom-left placement.** Fugue still refuses this case; it is a negative
  control, not a full-image parity pass. Its center pixel alone is insufficient to
  detect that difference.

`dreamseeker-observations.json` stores the recorded central colors and marks the
unsupported source phase. Full screenshots and within-client green-pixel mask
comparisons are retained under `build/plane-source-center-probe/` and referenced
owned probe directories. No game code/assets are required.

The admitted increment is only the zero-offset, leaf `1,1` alias. Offset, transformed,
child-tree, alternate-anchor and prefixed source placement are not newly supported.
Regression: `EntityMapTest::planeSourceBottomLeftSpellings`, run directly with
`fugue_tests entitymap planeSourceBottomLeftSpellings`.

# Plane-target reachability fixture

Stock DreamDaemon / DreamSeeker **516.1688**, Windows guest, 2026-10-25.
`dreamseeker-observations.json` records the central MAP pixel in four phases:
ordinary magenta HUD, 40 unconsumed hidden plane targets, a consumed green target,
and removal of that consumer. The expected colors are magenta, magenta, green,
magenta. Images are generated independently; no external game assets are used.

```sh
python -B tests/data/plane-target-budget-world/prepare.py build/my-plane-probe
# Compile build/my-plane-probe/world.dme with Dream Maker.
# Host its DMB on a checked unused port; connect a separate -new -guest stock client.
```

The server logs `PHASE <name>` and `PLANE_TARGET_PROBE_DONE`. Capture the foreground
client window at each phase, recording its process identity and stable geometry
manually. This fixture uses a real 11x11 terrain grid and a 1x1 viewport to
isolate composition from empty-world admission. Stop only processes you created.

Local evidence: `build/plane-target-budget-probe/stock-observations.json`,
`fugue-before-observations.json`, `fugue-final-observations.json` and their referenced
owned screenshots. Before the fix, the baseline rendered but adding unused targets
blacked out the entire frame. Stock and the fixed client match all four phases.

`MapNativeTest::planeCompositingPixels` additionally checks 80 unused targets, reachable
sources, 32 identity captures, refusal of a genuinely oversized reachable graph,
missing/cyclic sources, premultiplied alpha, and no texture uploads for changes
confined to unused targets, on native-default and OpenGL backends.

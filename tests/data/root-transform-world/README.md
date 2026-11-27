# Ordinary root-transform tree fixture

Generate into a separate runtime, compile with BYOND 516.1688, and check an unused
port before launching an owned server (retained runs used 14567). Use separate
guest state for native; do not change external checkouts or persistent settings.

```
python -B tests/data/root-transform-world/prepare.py build/root-transform-runtime
"C:/Program Files (x86)/BYOND/bin/dm.exe" build/root-transform-runtime/world.dme
```

The 480x480 MAP contains an absent/explicit-identity control, quarter-turns, half
size, unequal/rectangular icons, underlay spill, a nested **mob**, a rectangular
root, and a RESET_TRANSFORM exclusion probe. All offsets and dimensions are in
`world.dm`/`prepare.py`. Write `absent`, `identity`, `rotate`, `reverse`, `shrink`, or
`translate` (without a newline) to the runtime's `phase.txt` to change the second
object and the nested mob together. Wait for `PHASE` in `events.log`; clicks log target names and parameters.
The initial nested specimen was an object; the final paired run repeats it as a
mob with identical pixels.

`stock-*.png` are native-resolution 120x104 MAP crops, starting 32 pixels left and
40 pixels above the unoffset root cell. `stock-observations.json` retains capture
provenance, root-local integer click pixels, stock callbacks (null means no
callback), and the independently captured packed RESET_TRANSFORM flag. No external
game artwork is included. Native MAP tests consume these crops/callbacks directly.

`compare.py` compares eight supported regions of owned stock/native captures. It
does not assert whole-window equality or parity for the ninth, explicitly refused
reset case. The native window must be 496x555 for a 480x480 MAP; stock is 496x538.
See [renderer transforms](../../../docs/RENDERING.md#composition-and-transforms)
for the current bounded scope.

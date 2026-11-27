# MAP stylesheet, nested alignment and outline probe

Stage generated solid-gray terrain, compile, check an unused non-1337 port, and
launch a separate fixture server:

```sh
python -B tests/data/maptext-style-world/prepare.py build/maptext-style-runtime
```

Compile the staged `world.dme` and launch its DMB with DreamDaemon
`-safe -invisible` on an unused port. Connect stock and Fugue sequentially.
Write `probe-request.json` with a fresh token/phase and `action` of `reset`,
`fit`, then `measure`; inspect each matching PROBE response and collect pixels
manually. See [fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

The handshake resets CSS to green, changes only the stylesheet to red (`fit`), then
changes the text by appending whitespace (`measure`). It completes a winget round
trip and waits three ticks after the action before acknowledging; readiness is not
a GPU fence. Recorded comparisons used the same 768×512 physical client size and
144-DPI resize/reset/keep history. Do not modify stock global state or an external
game; close only the clients/server you started.

The view is 384×256 native MAP pixels, magnified 2× in retained captures. Specimens:

- Row 6: plain 24px X, 1px-black outlined 24px X, class-styled nested middle/center text.
- Row 4: 24pt and 18pt controls (stock 24px and 24pt have identical glyph bounds).
- Row 2: unitless line-height 1.0 and 1.5; aligned sibling spans with different sizes.

`observations.json` retains the final green phase's semantic measurements and
summaries. The former `stock-map.png`/`fugue-map.png` human-inspection captures are
not included; no automated pixel regression consumed them. The old comparison analyzer is not
retained; fresh collection is manual. Both clients keep green on a style-only write and show red
when text changes. Initial comparisons exposed a Qt inherited-point-size override
bug and an eager stylesheet-invalidation mismatch; these were fixed, not counted
as pixel-parity passes.

**This is not glyph/outline/line-layout pixel parity.** Recorded checks covered
visible class text, one-pixel outline presence, increasing line spacing, px/pt
equivalence and the invalidation sequence. The retained measurements show that
stock and Qt differ in antialiasing, outline coverage and line spacing despite
matching the 24px X's white bounds. See [maptext limitations](../../../docs/RENDERING.md#maptext-and-info).

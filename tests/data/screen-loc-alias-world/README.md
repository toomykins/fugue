# Screen-location edge and corner aliases

Independent BYOND **516.1688** alias probes with synthetic tile-sized markers:
30 ordinary/corner pairs, 22 range pairs and four top-row boundary pairs on a
7x5 view with 32-pixel tiles. The original native captures compared exact-green
masks within each client, not equal whole-window stock/Fugue screenshots.

Expected ordinary/corner equivalences and exclusions are transcribed in
`EntityMapTest::screenLocationEdgeAliases` and related limits/publication tests.
The source pair definitions remain in `pairs.json`, `range-pairs.json` and
`top-row-pairs.json`. Unconsumed capture-path/mask reports are not retained.

## Observed distinctions

- Ordinary LEFT/WEST, RIGHT/EAST and BOTTOM/SOUTH placements match for the tested
  tile/pixel offsets. TOP/NORTH has its own [fixture](../screen-loc-top-world/README.md).
- Named vertical anchors may precede horizontal anchors, including BOTTOM.
  CENTER,RIGHT and numeric,LEFT are not generic axis swaps.
- Bare TOPLEFT, TOPRIGHT, BOTTOMLEFT and BOTTOMRIGHT match compass corners;
  LEFTTOP, RIGHTTOP, LEFTBOTTOM and RIGHTBOTTOM do not.
- TOP/RIGHT are not generally NORTH/EAST aliases in ranges. For example,
  `WEST,SOUTH TO EAST,TOP` is a bottom row, not the full rectangle.

Early range refusals are historical, not current admission rules. The separate
[range corpus](../screen-loc-range-world/README.md) provides measured endpoint
semantics and 1,904 stock observations. Neither fixture proves automatic view
expansion, arbitrary icon dimensions, lowercase spellings or general anchoring.

## Reproduction

```sh
python -B tests/data/screen-loc-alias-world/prepare.py build/alias-repro
python -B tests/data/screen-loc-alias-world/prepare.py build/range-alias-repro range-pairs.json
python -B tests/data/screen-loc-alias-world/prepare.py build/top-row-repro top-row-pairs.json
```

Compile the staged `world.dme` and launch an owned server on an unused port with
local logging enabled. Connect a separate client. A `PHASE` line follows 0.8 seconds
settling and leaves 1.2 seconds for a foreground/geometry-checked owned capture.
Pair/member indices are one-based. Stock rounded window corners can clip markers;
compare complete paired masks within the same client and unchanged geometry.
Do not interpret failed or unsupported candidates as passing stock parity.

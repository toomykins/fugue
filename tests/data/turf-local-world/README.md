# Turf local-capture stock fixture

Synthetic DreamDaemon/DreamSeeker **516.1688**, no game assets or renderer-made
oracles. Stage `world.dm`, `skin.dmf`, and `res/9c04de04` renamed `z.dmi` under
ignored build; compile a DME including skin then world. The 160×32 DMI contains
floor, red upper-left root, green/blue lower-left underlay frames (10,10 ds), and
yellow upper-right overlay. `frames.txt` retains only normal server/model frames
from `build/captain-local/fields-04.txt.wire`, excluding login/resource payloads.
The captured player display name uses the same-length synthetic `TestGuest`; all
other wire bytes are unchanged. This does not establish guest-account isolation.
The screen master is installed for **each** connecting client; geometry is built
once. Tests assert the master exists, not merely that its neutral pixels match.

## Independent stock evidence

Ignored artifacts: `build/captain-local/`. Final oracle stock server owner:
`build/probes/20261102T230344Z-ff3ec23b`; client:
`build/probes/20261102T230345Z-49c0d6f1`. Port14683, launched with `-new -guest`;
stock preferences unchanged, account isolation not established. Startup layout, no resize/refit, DPI144: window598×657,
client576×601 **physical**. MAP576×576, exactly two physical pixels/source pixel.
Crop (11,45)..(587,621), nearest source-pixel centers, yields288×288:

- green: `capture-20261102T230359Z-889c5a51.png`;
- blue: `capture-20261102T230359Z-32218b59.png`.

Both registered PNGs are consumed by
`CompositionPlanTest::contributorPrecisionNative` on D3D11 and OpenGL. It checks
all RGBA pixels at both animation frames, exact reference/bounded equality,
effective opposite plan flags, and zero uploads after both frames are resident.
The worker regression consumes the same independent wire/assets and receipts.

Four turf producers compare hidden/visible × root-only/neutral KEEP_TOGETHER.
Root-only underlays/overlays remain independent; grouped ones enter the target.
An offset self-source overlay appears to the right of its root-only producer:
that is **not a capture edge/cycle**. Ordinary VIS_INHERIT_ID recovers vars bit8192
`2000`, without changing turf identity/material. On actual vis copies, flag0x20
names the holder, while flag0 names the child.

The reduced arrival tree uses absolute/FLOAT planes, an invisible source child,
a saturated matrix MULTIPLY leaf, and an independent tinted ADD KEEP_TOGETHER
subtree with an alpha-only MULTIPLY matrix child and ordinary `screen_loc`.
The matrix blend bytes are **04**, ADD is **02**, recovered from wire. The source
leaf has KEEP_TOGETHER|KEEP_APART|RESET_COLOR|RESET_TRANSFORM, no enclosing capture
or children; it does not introduce a capture edge. Crucially, stock **draws its
invisibility=101 ordinary overlay**, rather than applying root/vis admission.

`controls.json` and `controls-world.dm` retain the independent component controls:
none, matrix, group, both, and both without the plane master. Omitting the source
overlay restores red root pixels; including it makes them black. These controls
identified the missing ordinary-child visibility behavior, not an inferred graph
cycle or a material approximation.

`input-03.json` / `observations.jsonl` retain six Click and six Enter callbacks:

| Top-origin source pixel | stock owner | icon | vis |
| --- | --- | --- | --- |
| 40,136 | producer1 (turf) | 9,24 | 9,-40 |
| 104,152 | producer2 (grouped turf) | 9,8 | 9,-56 |
| 168,72 | producer3 (ordinary turf) | 9,24 | absent |
| 72,200 | selfproducer (source child) | 41,24 | absent |
| 152,232 | holder1 (inherited-ID vis) | 25,24 | absent |
| 216,232 | child2 (default vis) | 25,24 | absent |

## Histories and limitations

The first exploratory wire connection duplicated geometry on Login; not retained.
A later recorder lacked the first client's screen master. Per-client setup and an
explicit worker assertion correct that gap; `fields-04` is the retained replay.
Two initial native failures used a mistaken SUBTRACT/byte03 reduction, not the
captured tree's MULTIPLY/04. The corrected reduction still failed 256 pixels on
both backends until ordinary overlay invisibility was implemented. The initial
subtraction mismatch therefore does **not** establish a subtraction arithmetic
bug. Its sources/images remain in `subtract-counterexample/`; local-tree
SUBTRACT remains outside this increment's demonstrated subset.

`build/probes/20261102T232206Z-compare-27eb678c` is a separate sequential stock/
Fugue normal-MAP comparison, same explicit restore/resize to576×601 physical,
DPI144, fixture readiness replies, fresh Fugue state, browser GPU enabled. Its
stock crop exactly matches the blue oracle. It is **not a whole-application pixel
pass**: using identical576px crops produced1873 mismatches (mostly presentation
edges). A diagnostic575px Fugue crop still differed at592 pixels; that is not an
accepted recalibration/oracle. Source-texture native tests and matched window
conditions do not erase this presentation gap. Both runner-owned clients exited.

Follow-up`build/probes/20261102T235802Z-compare-442ce09e` isolates this as the
existing layout/presentation limitation, not a local-capture pixel defect.
Measured stock MAP/view is576²physical; Fugue MAP is384×383logical at DPR1.5,
framebuffer576×575, centered destination(0.75,0,574.5,575)physical. The original
576²crop includes an extra row and uses the wrong pixel-center mapping for Fugue.
Current-source normal-MAP scene readbacks match the unchanged green/blue oracles
exactly; the measured settled desktop MAP matches its presentation texture exactly.
Phase-labelled new blue screenshots reproduce both old576²rectangles exactly,
including the1873normalized mismatches. No best-fit crop, tolerance change or
replacement golden is accepted. `build/local-followup/paired-analysis.json` and
`old-new-paired.json` retain the exact comparisons. See
[presentation limitations](../../../docs/RENDERING.md#presentation-and-input)
for the distinction between native scene pixels and displayed MAP rectangles.

Initial launch calls omitted `--build-dir` (no process started); an input helper
misread `focus()`'s tuple (no click sent). Compile/test, capture-ownership and
retention failures remain in ignored verification/probe artifacts.

Not established: nonneutral named-group material, filtered/affine local capture,
named child producers, cross-plane captured groups, arbitrary vis flags, local
SUBTRACT, grouped/affine vis input, or relayed drag/drop. Ordinary source-child
input is admitted only for default opacity/no child PASS_MOUSE; other child mouse
modes/flags remain visually admitted but explicitly noninteractive. No game or
Captain/observer-respawn parity follows from this synthetic fixture.

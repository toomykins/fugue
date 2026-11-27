# Unequal HUD anchors / plane PASS_MOUSE / wide mouse slots

Independent stock DreamSeeker/DreamDaemon **516.1688**, Windows DPI **144**.
`stock.json` contains measured source-pixel bounds and actual Click coordinates,
consumed by `MapInputTest::stockHudAnchorsAndPassMouse` and native
`MapNativeTest::hudRelayPixelsAndInput` (default/OpenGL, pending-frame input and zero
warm uploads). Generated icons have an alpha hole; no game assets or screenshots
are exported. `wide-mouse.json` retains independent plaintext stock packets for
Click/DoubleClick/Down/Up/Hover/Wheel/Drag/Drop, consumed by
`MapInputTest::stockWideMousePackets`. The proxy retained only synthetic sessions.

## Results and scope

- TOP/RIGHT align the **root icon's** edge, not a 32px tile or overlay union.
  45x21 and 75x53 roots at TOP:-13 both start at y=13; RIGHT:-13 ends at x=211
  in a 224x160 view. Oversized overlays follow the root and clip at the viewport.
- Neutral viewport-sized one/two-hop plane relays with PASS_MOUSE name the original
  HUD button and report its original icon coordinates, with no invented vis pair.
  Source mouse0 passes through, mouse2 includes the alpha hole; source-master
  mouse0 still allows hits, but relay mouse0 blocks them. Ordinary non-PASS
  consumer ownership is unchanged.
- The implementation bounds neutral object-HUD traversal to 8 hops / 65,536 work
  units, uses the submitted immutable plan, and worker-revalidates every
  relay/master/leaf lifetime and binding. Click/Down/Up/Hover only: no new drag,
  menu, wheel, double-click, transformed/filtered/materialized or mixed-owner
  chain semantics. Wide **encoding** support does not expand picking semantics.
- Wide negotiation also widens mouse subtypes 3..8's slot: `08 00 ff ff 00 00`,
  not narrow `08 00 ff ff`. Sub-7 event IDs are zero-extended in the same slot.
  The worker finalizes canonical mouse bodies using the session handshake; MAP
  hover/click/gesture and INFO sends use this path, not a game-specific exception.

## Native evidence / reproduction

```
python -B tests/data/hud-relay-world/prepare.py build/NEW
# Add --wide for 66,000 generated dummy procedures (not retained in Git).
# Compile world.dme; launch a separate DreamDaemon on an unused port with -safe -invisible.
# Connect one stock client or Fugue with a fresh --state-dir; collect observations manually.
```

Remove the staged `phase.txt` between **fresh client sessions**, not during a run.
The recorded sequence restored/resized once to **672x480 physical**, then waited
for server acknowledgement and a unique rendered phase badge. Reproduce that
history manually with stable foreground/geometry and retain each server Click receipt. No Fit/zoom changes. Stock
saved settings are not cleared; Fugue uses separate guest directories. The generic
compare runner has no mouse-script action, so this bounded observer drives the
same phase/input history sequentially. This is source-pixel/input parity, not a
claim that skin/DPI policies match.

Raw artifacts: `build/hud-relay-runtime/stock3`, `fugue-default` (12 bounds / 24
Clicks each); `build/hud-relay-wide/stock-hooks`, `fugue-fixed` repeat the same
24 Clicks with Down/Up enabled and wide negotiation. All bounds and Click
coordinates agree. The non-PASS control still lacks Fugue Enter/Exit (2 callbacks);
other settled phase callback streams agree, aside from the initial pre-phase hover.
Stock wide wire: `build/hud-relay-wide/stock-hooks-wire.jsonl`; extra ordinary
floor Wheel/Drag/Drop/DoubleClick packets were recorded in that owned window.

Failures remain explicit: two stock attempts (`stock`, `stock2`) stopped at a
NORTH/oversized-icon automatic-view-expansion control; those first two TOP phases
passed. NORTH expansion is outside this fix and was removed from the settled
phase list. Initial verification stopped at the former repository-budget check.
A test build initially lacked the data-directory definition; a later application
link failed while our owned client held the executable open. Both were corrected,
not counted as initial passes. The pre-fix regression failed at `top-small`.

Live unchanged TG on **13337**, fresh owned guests: the relay/alignment-only build
picked/sent correct button IDs but no Observe dialog appeared. The independent
wide fixture identified the missing slot bytes. With the final wire fix, owner
`build/probes/20261101T225521Z-880013cb` logged Observe alert receipt/opening and
Character Setup's `tgui-window-1` popup. No Start Now/readiness/Join confirmation
was activated. Only owned clients and synthetic servers were stopped; the user's
server and external checkout were untouched. Browser contents, whole-window
layout, collapse animation and all other lobby workflows are not parity claims.

Final verification: `build/dev-runs/20261101T225843Z-verify-f2378193` built with
zero project warnings, passed all 62 CTest targets (3163 QtTest passes, 94
native-only skips), then native MAP 97 / OpenGL overlay 3 passes with no skips.
No automatic retries. Retention passed; earlier failures above remain recorded.

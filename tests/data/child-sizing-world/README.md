# Single-pane CHILD sizing (2026-11-02)

Minimal independent DreamSeeker/DreamDaemon **516.1688** fixture for the
bottom-right TG chat layout bug. No game assets, names or saved parameters.

## Stock rules and automated oracle

- One occupied side fills its CHILD, with **no divider**, whether the other side
  is omitted in the DMF or cleared by winset. Left-only/right-only, nested,
  hidden/visible divider settings and side swaps are covered.
- Omitted `is-vert` means **false** (top/bottom panes).
- DMF `splitter=97.5` is retained, not parsed as an integer/defaulted to 50.
  A hidden-divider 300×160 CHILD yields 300×156 and 300×4 panes; winset to the
  same percentage does not change geometry. `winget` returns `97.500000`.
- The nested browser fills 300×150; before the fix Fugue gave it **148×120**.
  The fractional fixture's upper pane was **150×160**, or **293×160** after winset.

`stock.json` retains selected **unmodified stock winget values**, not a client
baseline. `child_sizing_test` consumes all seven rows (startup plus six phases).
It also protects offline on-show inference versus live empty-side behavior.
`ShellTest::singleChildBrowserFillsPane` checks the actual browser widget,
parent-size changes, second-pane attachment and hiding the retained divider.

## Reproduce

Stage `world.dm`, `world.dme` and `skin.dmf` under a new ignored `build/`
directory. Compile with BYOND's command-line `dm.exe` (not the GUI).
Check an unused port and launch the staged DMB manually with DreamDaemon
`-safe -invisible`. Connect one isolated client at a time. Drive the phases through
`probe-request.json` using fresh `token`, `phase` and `action` fields; start with
`action=reset`, then measure `both`, `clear`, `swap` and `fraction-winset`.
Record versions and measured native DPI, then close only the clients/server you
started. See [manual fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

Readiness is a completed set of skin-query round trips with the exact request
token, not merely a visible window. Controls have fixed design rectangles; native
resize does not anchor them to the main window. Reset starts at 960×720 physical,
then attach both sides → clear → swap → fractional winset → resize to 1200×900.
Fugue uses a fresh guest state directory; stock has no fixture saved-params and
gets the same explicit reset. Stock global settings are not changed.

## Native evidence and limits

At **96 DPI**, both clients followed all six phases at equal physical client
sizes. Post-fix comparison of five controls' pos/size and fractional CHILD
splitter/axis reports matched **84/84 values**, including startup.
No runtime errors were found in the synthetic server log.

- Before: `build/probes/20261102T014625Z-compare-3e3c8a8f/comparison.json`.
- After: `build/probes/20261102T015221Z-compare-d50c1e82/comparison.json`.
- An initial run declared historical 144 DPI and failed the native geometry/DPI
  gate; stock actually reported 96. A separate per-client attempt also assumed
  144 for Fugue and failed. Neither is a matched comparison. The later runs
  measured 96 in both clients; no global DPI/settings were edited.
- Initial C++ regression: eight expected failures before the fix. Subsequent
  test corrections removed an incidental suffix collision in an offline-preview
  test and changed a GUI expectation from destroyed to hidden retained divider;
  that early GUI assertion also prevented focus restoration, failing a focus case.

This is CHILD sizing evidence, not full browser content, DPI, fractional-anchor,
divider-width, zero-pane, main-window sizing or renderer parity.

# Viewport fit fixture

Synthetic reproduction of TG's `fit_viewport` calculation from
`code/modules/client/verbs/ooc.dm`: a 19x15 view with a 300-skin-unit right-panel
reservation. This is a server-command rule, not a CHILD minimum or client layout
policy.

```
desired_width = min(round(map_height * 19 / 15), split_width - 300)
```

The server estimates a splitter percentage with a four-unit divider allowance,
then queries/adjusts until the MAP width matches. There is no on-size callback or
saved-params declaration: resizing alone retains the preceding splitter fraction.

## Manual reproduction

1. Copy `world.dm`, `world.dme` and `skin.dmf` into a fresh `build/` directory;
   compile the staged DME with DreamMaker.
2. Launch a separate DreamDaemon on an unused port with `-safe -invisible` and
   connect one isolated client at a time.
3. Write a fresh label to `request.txt`: `fit-*` invokes fit then measurement;
   other labels only measure. Inspect the matching FIT/MEASURE records in
   `observations.jsonl` before continuing.
4. Record native DPI, physical client geometry and explicit resize/refit history.
   Compare startup fit, maximize without fit, explicit maximized fit, resize back
   without fit, then explicit smaller-window fit.
5. Close only the clients/server you started. See
   [fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

The optional `probe-request.json` control in the world accepts fresh token/phase
values and `reset`, `fit` or `measure` actions. Its reply completes skin queries;
it does not acknowledge GPU presentation. No process/comparison runner is retained.

## Recorded sizing observations

Windows, BYOND **516.1688**, native desktop DPI **144**. Stock reported `winget dpi`
as 1.5; Fugue reported 1 because geometry was already in Qt logical pixels.
These exploratory queries describe historical client states, not a current test
verdict or uniformly matched physical geometry.

| Phase | Fugue MAP / skin client | Stock MAP / skin client |
| --- | --- | --- |
| Initial fit | 340x440 / 640x440 | 340x440 / 640x440 |
| Maximize without refit | 910x996 / 1707x996 | 1369x1494 / 2560x1494 |
| Explicit maximized fit | 1261x996 / 1707x996 | 1892x1494 / 2560x1494 |
| Resize to 960x660 physical without refit | 471x440 / 640x440 | 707x660 / 960x660 |
| Explicit smaller fit | 340x440 / 640x440 | 660x660 / 960x660 |

At the matched maximized client size, Fugue's MAP was approximately 1892 physical
pixels wide, matching stock. At the smaller matched 960x660 physical client size,
explicit fit produced 510 physical MAP pixels in Fugue versus 660 in stock: the
300-unit reservation costs 450 physical pixels under Qt scaling but 300 in that
stock state. This is not evidence of wrong splitter arithmetic or full DPI parity.

Raw process/server reports are not retained. Stock reused an authenticated account
despite its guest-mode request; global state was not isolated. No account identity
is needed for these sizing observations. The fixture measures sizing/history, not
command dispatch, live-game refitting or general renderer parity.

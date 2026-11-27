# Runtime winsets of window and control geometry (2026-11-26)

What stock DreamSeeker **516.1689** (DreamDaemon 516.1689) does with `winset` of `size`, `pos` and `is-maximized` on the
main window, its controls, a control created at runtime, a pane and a floating window, and what `winget` reads back.
`stock.log` was recorded with the display at **96 dpi** (100 %), where stock's pixels are this client's logical
pixels. An earlier recording of the first twelve steps at 144 dpi is quoted below where it differs.

The skin (`world.dmf`): a 224x224 main window holding a map anchored `0,0` to `100,100`, a 32x32 label `fixed` with no
anchors, a 64x32 label `half` at `0,192` anchored `0,100` to `100,100`, and a 64x64 CHILD `kid` at `160,0` showing the
pane `pane` (64x64, a 64x32 label `pl` at `0,32` anchored `0,0` to `100,100`). Two more windows are shown nowhere: the
pane `loose` (50x50, label `ll`) and the floating window `flt` (100x100, `is-visible = false`, label `fl` anchored
`0,0` to `100,100`).

`mob/Login()` sends `main.size=240x240` at once. A click on the map runs `client/steps()` in `world.dm`: each step sends
one winset, waits a second, then logs `winget` of `size` and `pos` of `main`, `map`, `fixed`, `half`, `kid`, `pane`,
`pl` and the runtime control `rt`, the window's `inner-size`, `outer-size` and `is-maximized`, the map's `view-size`,
and the sizes of `loose`, `ll`, `flt` and `fl`. `stock.log` is what stock logged, one line per step. The window's client
and frame rectangles, its place, its dpi and its child windows were read with `GetClientRect` / `GetWindowRect` while it
ran, and agreed with every winget answer below.

## What stock does

The main window:

- `main.size=WxH` sets the window's client area, the status strip below it not counted, and `size` and `inner-size`
  answer it: the 224x224 window's frame is 240x282 and its client area 224x243 with the strip. The controls follow
  their anchors as for a drag of the frame (`main.size=288x288`: the map 288x288, `half` 128x32 at 0,256).
- A `size` sent from `Login()` is the size the window opens at (`start`: 240x240, the map 240x240).
- The status strip is the system's status bar: **19 px at 96 dpi**, 25 px at 144 dpi (not in proportion: it follows the
  system font). No winget answer counts it except the zero-extent fill below and `outer-size`.
- `main.outer-size` is the frame: 256x298 for 240x240 at 96 dpi (8-pixel borders and a 31-pixel caption, plus the
  strip); 246x305 for 224x224 at 144 dpi.
- `main.pos=100,120` moves the frame's top-left to 100,120 on the screen and `pos` answers it.
- `main.is-maximized=true` maximizes it. While maximized, `size` answers the size it is restored to (256x256) and
  `inner-size` the real client area (2560x1350); the controls follow the real client area. `main.size=300x300` then
  leaves it maximized, `size` answers 300x300 at once, and `is-maximized=false` restores it at 300x300. `pos` also
  stayed 100,120 while the frame was at -8,-8.

Controls (a winset `size` or `pos` is where the control is NOW, in the box it has now, and its anchors move it from there):

- `map.size=160x160` in the 288x288 window is 160x160 there and 96x96 back at 224x224.
- A zero extent fills to the window's far edge **including the status strip**: `map.size=0x0` in the 288x288 window gives
  288x307 (288x313 at 144 dpi), `map.size=100x0` 100x307, and `fixed.size=0x0` (no anchors) 256x275 in the 256x256
  window, which it keeps after a resize. Inside the pane, `pl.size=0x0` filled to the pane's edge (96x64 at 0,32 in a
  96x96 pane, as it was).
- `pos` alone moves the control and keeps its size (`fixed.pos=10,20`; `half.pos=8,100` keeps 36x20). `pos` with `size`
  on the anchored `half` (`4,150` and `50x30` at 224x224) is 114x30 at 4,214 at 288x288.
- `map.view-size` stays 224x224 throughout: the control's size does not change it.

A control created at runtime (`rt`: `parent=main;type=map;pos=20,40;size=100x100;anchor1=0,0;anchor2=100,100` in the
288x288 window) reads its pos and size the same way: 100x100 at 20,40 there, 36x36 at 20,40 at 224x224. Then
`rt.size=80x60` and `rt.pos=10,10` at 224x224 give 112x92 at 10,10 at 256x256.

Panes and windows:

- `kid.size=96x96` (the CHILD) makes `pane` answer 96x96 and `pl` 96x64 (its anchors in the pane's 64x64 design).
- `pane.size=40x40` on the shown pane changes nothing: `pane.size` still answers 96x96, the box it is shown in.
- `loose.size=70x30` on the pane shown nowhere answers 70x30 (`ll` stays 50x50).
- `flt.size=120x80` on the unshown floating window answers 120x80, and `fl` follows to 120x80. Before it, `flt.size`
  answered 120x100 for the .dmf's 100x100: the window is made no narrower than the system lets a captioned window be.

## Fugue

`ShellTest::runtimeSizeWinsets` (shell/tests/shell_test.cpp, offscreen) replays the Login winset and every step on a
`SkinWindow` with this skin and checks every `size`, `pos`, `inner-size` and `is-maximized` answer of `stock.log`, the
sizes of the panes and windows, and the window and widget geometry they describe (a widget is what shows of its control:
clipped to the client area and the status strip). `outer-size` is checked against the window's own frame. Not compared:
`main.pos` before the window is moved (a screen place), the controls while maximized (the offscreen screen is not
stock's), `flt` and `fl` before their first winset (stock's 120-pixel minimum width), and `map.view-size`.

`stockprobe.py click` of this world (below, 96 dpi) gave the same answer from both clients for every step but these:

- `main.pos` before `mainpos`: the window's place on the screen, which differs by nature.
- `flt.size` and `fl.size` before `fltsize`: stock's 120x100 against this client's 100x100 (see above).
- `main.outer-size`: this client's own frame. On Windows it is read as stock reads its own (the window rectangle with
  its invisible resize borders), so it equals stock's where the menu bar and status strip are the same height, as here
  at 96 dpi; with Qt's frame geometry it would be 16x8 smaller.

The status strip is 19 logical pixels at every scale: stock's height at 96 dpi. At 144 dpi stock's is 25 device
pixels, this client's 28.5.

## Reproduction

Requires BYOND and Python/Pillow. `stockprobe.py`'s own `prepare.py` stages this world.

```sh
python -B tools/stockprobe/stockprobe.py --world tests/data/winset-size-world click winset-size 100,100 wait:150
```

prints, step by step, what stock and Fugue logged (`--stock-only` for stock alone). `click` clicks one window and
waits before it clicks the other, so each client's steps run and are read on their own. The steps take about two
minutes per client.

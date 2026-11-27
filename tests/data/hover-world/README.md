# Pointer hover: MouseEntered, MouseExited and MouseMove

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-26, Windows 11 with the display at 144 dpi; stock drew the
map one screen pixel per icon pixel, so map pixels below are window pixels. No game assets in Git.
`prepare.py <dir> [CLIENT_ENTER] [CLIENT_MOVE] [EXTRA]` writes the world, a 288x224 skin (a 7x7 view of 32 px tiles
between 32 px of letterbox left and right) and solid full-tile icons. `CLIENT_ENTER` adds client/MouseEntered and
client/MouseExited, `CLIENT_MOVE` client/MouseMove; each calls `..()`. `world.params` `lag=<ds>` sets `world.tick_lag`.

`EXTRA` (the second round, below) widens the window to 610x336 and adds, at window pixels: a second map `map2`
(300..395 x 0..31) showing `hud2` (all three procs) at 1,1, nothing at 2,1 and the world's `both` at 3,1 through the
vis_contents of an iconless holder; the stat panel `info` (300..599 x 40..223) whose tab "Probe" lists `statrow` (all
three; its icon at 338..370 x 63..95, the row's label "row" at 320,75) and `both` (icon at 338..370 x 96..128); and a
grid (0..287 x 232..327) with `gridrow` (all three) in cell 1,1 and `both` in 1,2. Turfs 6,6 6,7 7,6 7,7 have no icon
(type `void`, all three procs, so that a report naming one would show), and `island` (all three) stands on 7,6. Every
obj has the verb poke and right-click menus are on (`client.show_popup_menus`), so a right click on an obj opens a menu
whose corner is at the pointer.

Every mouse proc logs `world.time`, the proc, the atom, its location, control and params; presses, releases, clicks and
drops are logged by the client procs, so that no atom gets flags from them. The map shows the whole 7x7 world: tile
x,y is at map pixels 32+32(x-1) .. and 32(7-y) .. (the letterbox is 0..31 and 256..287).

| atom | where | procs |
|---|---|---|
| `enter`, `enter2` | 1,7 and 5,7 | MouseEntered, MouseExited |
| `move` | 2,7 | MouseMove |
| `both` | 3,7 | all three |
| `none` | 4,7 | none |
| `big` | 1,4, a 64x64 icon over four tiles | all three |
| `slider` | 5,4, pixel_x stepped 0..6 by 2 every half second on the server | all three |
| `glider` | 6,5, pixel_x animated 0..6 and back, looped | all three |
| `hopper` | stepping between 5,2 and 5,3 every two seconds (and gliding) | all three |
| `exitonly`, `dragonly`, `downonly`, `wheelonly`, `moveexit`, `clickonly` | row 1 | MouseExited; MouseDrag; MouseDown; MouseWheel; MouseMove and MouseExited; Click |
| `enteronly` | 6,2 | MouseEntered |

## How the data was taken

`tools/stockprobe/hoverprobe.py --world tests/data/hover-world [--switches ..] [--params lag=..] --out <file> <script>`
moves the real pointer over stock's map (the window kept on top), presses and releases real buttons and posts moves to
the window; per action it keeps what the world logged. `hover.py hover.json 2026-11-26 <run>=<output>[@<replay>]`
keeps the runs in `hover.json`: `base` and `base2` (the plain world), `lag10` (tick_lag 10), `center`, `cmove` and
`cboth` (the client procs), and `pace`, `pace10` and `pace025` (one-pixel sweeps at tick_lag 0.5, 10 and 0.25).
`pace/p33` was left out: the user's own pointer moved during it. `stockprobe.py --world tests/data/hover-world --map
288x224 --settle 4 record base` (with `HOVER_SWITCHES=CLIENT_ENTER` and so on for `center`, `cmove`, `cboth`) wrote
`replay/<name>.txt`, what DreamDaemon sent Fugue for each compile, and `replay/<name>.png`, stock's map.

Second round, same day and version: `hoverprobe.py` also presses keys (`kdown:ctrl`, `key:esc`, ..). `extra` (empty
space, map2, the stat panel, the grid, modifiers) and `extramenu` (right-click menus) are the `EXTRA` compile, `xboth`
the same with both client switches; their replays are `replay/extra.txt` and `replay/xboth.txt`. The `extra` cases
were run twice, with the same procs both times. The menu cases were first tried with the menu opened at 112,16, where
some of the probe's own moves and presses landed on the open menu; they were rewritten (menu at 100,10, the other
points clear of it) and only the rewritten run is kept. `xboth` was run once.

Third round, same day and version: `hoverprobe.py` also presses the middle button (`mdown`, `mup`), posts sweeps
(`psweep:`, the moves of `sweep:` posted to the window) and with `--timer1` holds a 1 ms system timer resolution in its
own process; each run records the system timer's resolution (`timer_ms`). `round3` (the `EXTRA` compile: a middle
button and menus, menus closed off every map, drops over nothing) and `round3mid` (the middle button and menus again,
one menu per case) use `replay/extra.txt`, `xround3` (some of both with the client procs) `replay/xboth.txt`;
`pacepost` (posted and real one-pixel sweeps, tick_lag 0.5) and `pacepost1ms` (the same with `--timer1`) the plain
world. Five middle-button cases of `round3` were left out: a middle press does not close the menu, so it stayed open
into the following cases, and in one the user's own pointer moved; `round3mid` repeats them.

`mapinput_test::hoverLikeStock` replays the runs in order through the real hit test (a second `NamedMap` for map2),
`MapHover`, the rules of `mapMenuClosed` and `mapReleaseForgetsHover`, and a model of the server (below), and compares
the procs with stock's, action by action (784 actions, 467 procs); cases with posted moves, sweeps or a moving
scene are left out of it, and so are those over the stat panel and the grid (`st1`..`st3`, `grid1`, `mstat*`, `dstat`),
on which Fugue reports no hover. `hoverPaceLikeStock` replays the real pointer's sweeps (posted sweeps are left out:
Fugue cannot tell them from the pointer's own); `hoverMergesPicksInATick`, `hoverTransitionsAndStaleIdentities` and
`releaseForgetsHover` hold the rules below on hand-made picks.

## What stock does

Each point says whether it confirms or contradicts the static reading of the client this measurement was planned from.

- **Appearance flags on the wire** (from `replay/base.txt`): MouseEntered or MouseExited give 0x800; MouseMove gives
  0x180800; MouseWheel alone 0x100800; MouseDrag, MouseDown and Click nothing. So 0x80000 is MouseMove's own bit, and an
  atom with only MouseMove has 0x800 too. Client hover procs: client/MouseEntered sets MouseSubs bit 0x02000000,
  client/MouseMove sets that bit and extra bit 0x01.
- **The record and its two reports** (confirmed): one record of what the pointer is over. A pick naming another atom or
  control (or map) is an over report, sent when a client hover proc exists, or the new atom has 0x800, or the atom the
  last report sent named has 0x800. A pick that only moves the pixel inside the screen tile, over an atom, is a move
  report, sent when client/MouseMove exists or the atom has 0x80000; client/MouseEntered alone reports no moves
  (`center` `leave` at 145,16). Every one-pixel step over `both` gives MouseMove (`base` `steps`).
- **No report for icon-x/y or tile changes alone** (confirmed): a whole-tile jump inside `big` at the same pixel of the
  tile (41,102 to 41,70 and 73,70) reports nothing; `slider` and `glider` moving under a still pointer give no MouseMove
  (`still`, `glidestill`).
- **The server's side**: it runs MouseExited (with the params it was entered with, not those of later moves) and
  MouseEntered when a report names another atom than the last one it got, and MouseMove when the same. Entering
  `move` therefore runs nothing (it has 0x800, so the report goes out); its first pixel step runs MouseMove.
- **A still pointer is picked again as the map changes** (contradicts "no per-frame re-pick"): `hopper` gliding onto
  the pointer is entered at icon-y 4 or 32, the first glide frame that covers the pointer, not when the server moved it;
  stepping away it is exited at once and, with client/MouseEntered, the floor entered. This stops after a release until
  the pointer moves again: no report at a click's own point while `slider` and `glider` move (`click`, `clickleave`).
- **Leaving** (confirmed): to the letterbox, `floor` or outside the window, an over report naming nothing (or the floor)
  goes out when the last report's atom had 0x800 or a client hover proc exists; MouseExited gets the params it entered
  with. Over the letterbox no client/MouseEntered runs (no object). Going from `enter` to `move` or `none` names the new
  atom; the old one is exited either way.
- **Releases** (confirmed for clicks, contradicts the old Fugue rule for drops): any release, click or drop, of the left
  or the middle button, clears the record (with menus off; for a right release with menus on see the second round).
  After a click the next step on the same atom is an over report, which the server, getting the same atom,
  turns into MouseMove; leaving the window before any move sends nothing (`clickleave`, `round3` midclick: no
  MouseExited; back on the same pixel, MouseMove). A drop makes the server enter the atom dropped on, with the drop's
  params and without exiting the last one; leaving right after it sends nothing (`dropleave`), after a move it exits
  with the drop's params (`dropmoveleave`). A drop over nothing, see the third round.
- **Button held** (confirmed): no hover at all while a left, right or middle button is down (`held`, `rheld`, `round3`
  mid); the drag path takes the moves, and a middle drag drops as a left one does. (With menus on, a right press that closes a menu is the exception: second round.)
- **Posted moves** (contradicts the plane-click-world note): a WM_MOUSEMOVE posted to the window is taken at its own
  point (`posted`, `parkpost`, `parkpost2`). With the real pointer outside the window the leave follows at once
  (MouseEntered then MouseExited); with it parked over the map, the next map change picks the real pointer again
  (MouseMove back to its pixel, or MouseExited if it is over something else).
- **Pacing** (contradicts "one server tick"): at most about one report per 1/64 s of real pointer motion, at tick_lag
  0.25, 0.5 and 10 alike (20 one-pixel steps 4, 8, 12, 14 and 16 ms apart reach the world as 5-7, 10-11, 14-15, 17-19
  and 19 MouseMoves, 18 ms and more apart all 20; two steps a millisecond apart, one). Sweeping from `none` onto `both`
  in 10 ms steps, `both` is entered once, with the last step's params (`base2` `enterquick`). It is Windows that merges
  the moves, not stock that holds reports: see the third round.

### Second round

- **Empty space** (`extra` void1, void2; `xboth` void1, void3): a pixel of the map where nothing is drawn (a turf
  without an icon) is what the letterbox is: no turf is named (the `void` turfs' procs never run, and
  client/MouseEntered gets no object), the atom left is exited with the params it entered with, steps within it report nothing, and
  stepping back onto an atom enters it. From `island` (standing on such a turf) onto empty space and back is the same.
- **One record for every control** (`extra` m2jump, m2gap, m2vis, m2out, st1..st3, grid1): the second map, the stat
  panel and the grid share the main map's record. Their reports name the control and, as location, `map2`, the stat
  tab's name ("Probe") or the grid cell ("1,2"); stat params are `icon-x=13;icon-y=16` alone, grid params
  `icon-x=17;icon-y=17;vis-x=0;vis-y=0;cell=1,2`. Pixel steps within a stat row or a grid cell report nothing, nor does
  moving from a row's icon onto its name text (it is the same row); the stat label "row" and map2's empty tile are empty
  space. The same atom shown in two controls is one atom: from `both` in map2 onto `both` in the map the server gets the
  same atom and runs MouseMove.
- **A control's leave comes late**: going straight from one control onto another (the probe's jump; any adjacent
  controls), the control reached reports first and the control left behind then sends its leave, which clears the
  shared record whatever control it names: `EXIT both, ENTER hud2, EXIT hud2`, and the next pixel step enters `hud2`
  again. Across window background between the two (m2gap) the leave comes at the background, as it should.
- **Modifiers** (`extra` mod1..mod3): pressing or releasing ctrl or shift with the pointer at rest reports nothing; the
  record does not hold them. A report made while one is held carries it (`icon-x=18;icon-y=16;ctrl=1;screen-loc=..`),
  and MouseExited gets those the atom was entered with. Alt was tried once (not kept): its release put stock's window
  into Windows' keyboard menu mode, and no hover was reported until the run ended.
- **Menus** (`extramenu`, `xboth` mescsame, mescvoid, rfloor*): the press pixel itself is covered by the menu's corner,
  so a menu cannot be closed by a press there. When the menu closes the pointer is picked again at once: after Escape
  at the press pixel nothing (mescsame), elsewhere on the same atom MouseMove, over another atom EXIT and ENTER, over
  empty space EXIT; after picking an entry the same, at the pointer (mpick: POKE, then EXIT, the entry lay over the
  letterbox). A right press off the menu closes it with that report carrying `right=1` (mrother, mrpix, mrvoid; it
  opens a new menu at its release when there is an obj). A left press off the menu closes it with no report: the press
  reaches the map (DOWN, UP, CLICK; on empty space only an UP naming nothing) and the next move reports after the
  release cleared the record (mclickother, mclickpix, mclickvoid).
- **Presses and the record**: a right release with menus on does not clear the record, whether a menu opens or not
  (mescleave and mescsame: the pre-menu record survives; xboth rfloorleave: a right click on the verbless floor, no
  DOWN/UP/CLICK, and leaving afterwards exits the floor; a left click there clears it, lfloorleave). A press does not
  write the record: after a drop cleared it, a right click and Escape at the same pixel report the atom again (mdrop:
  MouseMove), where without the drop nothing is reported. This agrees with popupmenu-world part C (516.1684, wine): the
  stale atom is exited and the one under the pointer entered as soon as the menu closes, with right=1 while the right
  button is held.

### Third round

- **The middle button and menus** (`round3mid`, `xround3` mmidheld*): a middle press off an open verb menu neither
  closes it nor reaches the map (no MouseDown), nor does its release while the menu is open (mmidother: the menu stays
  until Escape, which then reports as usual). When Escape closes the menu with the middle button held, the pointer is
  picked again at once and the report carries `middle=1` (mmidheld: `EXIT both`, `ENTER enter` with
  `icon-x=9;icon-y=16;middle=1;screen-loc=..`; mmidheldpix: MouseMove with middle=1; mmidheldvoid: EXIT; mmidheldsame,
  at the record's pixel: nothing). The middle release then reaches the map as MouseUp and Click on what is under the
  pointer, with `middle=1;button=middle` and no MouseDown before it (on empty space only an UP naming nothing, as a left
  click there). A right press while the middle button is held does nothing at all: no MouseDown, no menu, nothing at
  its release (mmidbefore, mmidbeforeother; the middle release is then an ordinary click or drop).
- **A menu closing off every map** (`round3`, `xround3` moutesc, moutescother, mbgesc): with the pointer outside the
  window or over its background, Escape reports nothing and the record is kept: back at the pixel the record has,
  nothing; a pixel further on the same atom, MouseMove; onto another atom, EXIT and ENTER; leaving, MouseExited with the
  params it was entered with before the menu opened. Over the stat panel's empty part the panel reports at once (a
  report naming nothing, so `EXIT both`), and so it does at a left or right press there that closes the menu
  (mstatesc, mstatclick, mstatright; the left press sends no MouseDown, its release an UP naming nothing with
  `loc=Probe ctl=main.info`); the next move onto the map gives ENTER and, the panel's leave coming late, EXIT, and the
  step after that ENTER again.
- **Drops over nothing** (`round3`, `xround3` dvoid, dvoidleave, dletter, dm2empty, dstat, dbg, dout, douten,
  dvoiden): the client sends no hover report at the release, wherever it is; the record is cleared as at any release.
  What the server does depends on the drop: over a map where nothing is (empty map space, the letterbox, map2's empty
  tile; MouseDrop with `over=` empty and `over_control` that map) it runs MouseExited (and client/MouseExited) on the
  atom last entered, with the params it was entered with, right after MouseDrop; over the window background or outside
  the window (`over_control` empty) it runs nothing, so the first move back onto the same atom gives MouseMove
  (dbg, dout) where after a drop on empty map space it gives MouseEntered (dvoid). Over the stat panel the server runs
  nothing at the drop either; the panel's own report naming nothing exits the atom half a tick later (dstat). No
  MouseEntered runs for any drop over nothing.
- **Pacing** (`pacepost`, `pacepost1ms`): 20 one-pixel moves posted to the window over `both`, 1, 2, 4 and 8 ms apart
  (each twice, the real pointer parked on `both` at another pixel), all reach the world: every distinct pixel gives its
  own MouseMove (19 of 20 steps land on a new pixel), with at most two MouseMoves back to the parked pointer's pixel when
  the map changed. Stock therefore makes a report for every move it reads and holds none; the 1/64 s of the real
  pointer's sweeps (the same run: 5, 10, 15 and 19 MouseMoves for 4, 8, 12 and 16 ms) is Windows merging the pointer's
  moves between the times stock reads them, so a world sees the pointer's last position in each tick, picked against
  the record as the tick found it, and no report from a position in between. The system timer stood at 0.5 ms before
  and during both runs (another process held it there), and holding it at 1 ms from the probe changed nothing: stock's
  1/64 s does not follow the system timer resolution here. Not separated: Windows 11 grants timer resolution per
  process, so whether stock would read faster if it raised its own resolution, or reads on a fixed 1/64 s tick, could
  not be told apart from outside.

## Fugue

Before: the pick was redone after every frame and any change of the whole report (icon-x/y, the tile, modifiers, the
plane path) sent MouseMove, so `glider` under a still pointer got a MouseMove every frame; the move bit tested was
0x100000 (so `wheelonly` was sent moves); a step to an unflagged atom sent the null leave instead of naming it; the
leave went out whenever anything was recorded; a drop adopted its destination and a click kept the record, so leaving
after either sent MouseExited; reports were not paced. After: `MapHover` (`shell/mapinput.h`) keeps stock's record and
gates, clears it at every release, paces reports to 1/64 s with replacement; `MapWidget` re-picks a still pointer only
after the pointer has moved since the last release, and names nothing (with control and screen-loc) over an unrendered
pixel instead of leaving. The replay matches stock in every compared action; the sweeps within one MouseMove.

Second round. Already as stock: empty space (with the live map's visibility frame a turf without an icon is not hit),
one record for the session's maps, modifiers, a press not writing the record, re-picking when a menu closes. Changed:
a leave replaced a report still waiting, so the posted cases' MouseEntered and the second map's ENTER/EXIT pair were
lost; now it goes out after it (`MapHover::queue`, `due` returns the waiting reports one by one, `last()` gates the
leave). A map's leave reached the session before the pick of the map the pointer went to; `MapWidget::leaveEvent` now
sends it after the event in hand. The right release of a menu gesture cleared the record, so Escape at the press pixel
gave MouseMove; `mapReleaseForgetsHover` keeps it for a release the menu path takes. A menu closed by a left press was
re-picked; `mapMenuClosed` reports nothing then (`MapWidget::popupClosed`), and only the map under the pointer reports
for all the maps told of the close. Not done: Fugue has no hover on stat panel rows, and no grid control at all.

Third round. Already as stock: drops over nothing (no hover report at the release, the record cleared; what the
server runs is the server's), the middle button's releases clearing the record. Changed: a menu closed off every map
sent a leave; `MapWidget::popupClosed` now sends nothing then, and the record is kept. A middle press closed the menu
and reached the map; the menu now ignores the middle button (`SkinWindow::showMapMenu`), a menu closed with it held
reports with `middle=1` (`mapMenuClosed`, `proto::enc::hoverParams`), and its release is a click where it is released
(`MapWidget::middleFromMenu_`). A right press while the middle button is held is ignored (`MapWidget::mousePressEvent`).
`MapHover` replaced a waiting report with a later pick whatever its gate said; it now merges the picks of a tick as
Windows merges the moves: the last pick of the tick is weighed with its own gate against the record as the tick found
it, and the earlier picks and their report are undone. Moves posted to Fugue's window are merged too, where stock
reports each: Qt cannot tell them from the pointer's own.

## Not measured

Whether stock reads the pointer faster when it holds a finer timer resolution itself (above), a middle press on the
menu itself (tried once, disturbed), a right or left press while another button than the middle is held, hover while
the middle button stays held after Escape closed the menu, a menu closing with the pointer over the stat panel (the
panel reports at once in stock; Fugue's panel does not pick then), and alt, whose release stock's window took as
Windows' menu key.

## Stat panel hover

Third round, same day and version, the bytes on the wire as well. `hoverprobe.py --wire` puts a relay between stock
and DreamDaemon and keeps, per action, the plaintext of what stock sent (input commands, c2s 0x0002, and stat tab
selections, 0x006b), read with the session key both ends derive from the handshake; `--replay` writes what DreamDaemon
sent stock before the first case. The `PANEL` compile (with `EXTRA`; `prepare.py` then makes the panel 300x290)
lists on "Probe", after `statrow` and `both`, `plain` (no mouse procs, icon), `bare` (all three, no icon), a text row
and `swap` (`swapa`, or `swapb` after F3; both all three), and on a second tab "Other" `other` (all three); F2 switches
the tab (`client.statpanel`), F4 adds a row at the end whose text changes every tick. `panel.json` keeps the runs
`panel1`, `panel`, `panel2` (`EXTRA PANEL`, replay `replay/panel.txt`) and `pboth` (with both client switches,
`replay/pboth.txt`). With the wider label column ("plain") the value column starts at x 346: rows `statrow` 63..96,
`both` 97..129, `plain` 130..162, `bare` 163..181, the text row 182..199, `swap` 200..233; icons are 32 pixels from
x 346. The whole set was run a second time: it agreed everywhere except where that run's F2 and F3 presses did not
reach stock (`swap`, `tabempty`) and where its first move into the grid reported nothing (`grid`, `gridempty`; the
next move did); only the first run is kept.

The wire, in the notation of `proto/enc_c2s_0002_input.h`:

| pointer | body |
|---|---|
| a stat row naming an atom | `[08 00 ff ff][over][03][02 "Probe\0"][02 "main.info\0"][02 "icon-x=5;icon-y=16\0"]` |
| a stat row whose atom draws no icon | the same with params `""` |
| the panel's empty space | `[08 00 ff ff][00 ff ff 00 00][03][02 "Probe\0"][02 "main.info\0"][02 "\0"]` |
| leaving the panel, the grid or a map | `[08 00 ff ff][00 null][03][00 null][00 null][02 "\0"]` (the map's leave) |
| a grid cell | `[08 00 ff ff][over][03][02 "1,1\0"][02 "main.grid\0"][02 "icon-x=17;icon-y=16;vis-x=0;vis-y=0;cell=1,1\0"]` |
| the grid's empty space (below its cells) | `[08 00 ff ff][00 ff ff 00 00][03][02 "\0"][02 "main.grid\0"][02 "icon-x=0;icon-y=0\0"]` |

That is the INFO click's shape (`encodeInfoClick`) with sub 0x08: the location is the selected tab's name, a string;
`over` a row's atom as the click names it (`statrow` `[01 00000000]`, `both` `[01 09000000]`, the same id the map
names it by). icon-x/icon-y count as for a click: x+1 from the icon cell's left edge, y+1 from its bottom, unclamped
on the name text (`icon-x=55;icon-y=21` at 400,75).

What stock does:

- **What a row is**: an atom row takes the pointer on its icon and its name text, all of the row's height (350,96 is
  still `statrow`). Its label, the gap between label and icon, and past the text are the panel's empty space (statrow:
  400 is the text, 500 empty), and so is a text row and the space below the rows. A row drawn without an icon takes
  the pointer some way past its text (`bare`, text 346..369: 420 is the row, 440 empty). The tab strip is not part of
  the panel: going there from a row sends the leave (`parts` 360,50), and back onto the row is entered afresh.
- **Gates**: those of the map. A row naming another atom is an over report when a client hover proc exists, the atom
  has 0x800, or the atom the last report named had it: from `both` onto `plain` the report names `plain` (the server
  exits `both`), from outside onto `plain` and about it nothing is sent (`plainonly`), with client/MouseEntered
  `C-ENTER plain`. The empty space is an over report naming nothing, under the same gate (`parts`, `text`; `pboth`
  sends it with client procs, and the server runs no client/MouseEntered for it).
- **No move reports**: no pixel step within a row reports, and none within the empty space, with client/MouseMove too
  (`pboth steps`). Moving from the icon onto the name text is the same row.
- **Another tab under a still pointer** (F2, `tabempty`, `pboth tabs`): the pointer is picked again with the new tab's
  rows: over `other` a report naming it with location "Other" (EXIT `statrow`, ENTER `other`), over empty space a
  report naming nothing with location "Other". A tab chosen by a click (`tabclick`) sends `006b [u16 index][token]
  [name\0]` (`01 00 01 "Other"`), as Fugue's `encodeStatTabBody`; the pointer was on the tab strip then.
- **Rows changing under a still pointer**: not picked again. `swap` showing `swapb` under the pointer reports
  nothing; the next step names `swapb` (EXIT `swapa`, ENTER `swapb`, `swap2`, `pboth swap`); a row added below
  (F4) and the panel refreshing every tick report nothing (`tick`).
- **Clicks**: a left click on a row sends `[06 ..]` MouseDown, `[05 ..]` MouseUp and `[03 ..]` Click, each with
  `icon-x=5;icon-y=16;left=1;button=left`, and clears the record: the next step is an over report, MouseMove at the
  server (`pclick`).
- **Leaving** (`leaves`): onto the window background the leave; onto a map, the second map or the grid the control
  reached reports first and then the panel's leave clears the record (EXIT `statrow`, ENTER `hud2`, EXIT `hud2`), and
  the same from them onto the panel (ENTER `statrow`, EXIT `statrow`), as between maps.
- **The grid**, for a later implementation: a cell takes the pointer across its whole width (`icon-x=197` at x 200)
  and reports no pixel step, client/MouseMove or not; its empty space below the cells has its own body (above); the
  leave and the gates are the map's, and from the map onto a cell the cell reports first, then the map's leave.

`stathover_test::statHoverLikeStock` replays the runs through statHoverPointer at the rows stock drew under the
pointer, the real map hit test, one MapHover and the server's rule of `hoverLikeStock`, and compares both the reports,
byte for byte, and the procs with stock's, action by action; the cases over the grid and those whose F2, F3 or tab
click change the panel are left out of it. `statPanelHoverPicks` holds the widget's picks, its late leave and when it
picks again.

### Fugue

Before: no hover report over the stat panel at all. After: `StatPanelWidget` (`shell/statpanelwidget.cpp`) picks the
pointer over its rows (`rowHovered`, row -1 for empty space; `hoverLeft` after the event in hand) and picks again when
another tab is shown with its rows; `SessionWorker` turns each pick into a `MapPointer` with `info` set
(`statHoverPointer`, `shell/statview.cpp`) and feeds it to the session's one `MapHover`, with the row's appearance
flags for the gate; a stat panel pick never makes a move report, its body is the INFO click's shape
(`mapPointerBody`), and a row click clears the record. Not done: a stat row click still sends Click alone, without
stock's MouseDown and MouseUp; modifiers held over the panel are sent as on the map, which was not measured; a verb
tab is treated as empty space naming the tab "verbs", not measured; the widget's row geometry is Qt's, not stock's
(the hit area follows the same parts of a row). Fugue still has no grid control.

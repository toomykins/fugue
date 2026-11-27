# The map's right-click menu: pictures and text

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-26, Windows 11 with the display at 150 % (144 dpi). No game
assets in Git; the pictures in `icons/` are cut from grabs of this world's own menus.

`world.dm` puts one probe atom on its own cell for each case, over a verbless floor. Each has the verb `poke` (`set src
in world`); the player is an invisible observer at 9,6 under an 18x12 view, whose verb `examine(obj/O as obj in
view())` is offered for every obj by its argument. `prepare.py <dir>` writes the runtime directory: the world, the shared
576x384 skin and icons made to be told apart in a grab (its docstring lists them; "a" is four coloured quadrants).

`menuprobe.py --out <dir> [CASE...]` right-clicks each case in stock with the real pointer and keeps, per case, the menu
as Windows has it (`<case>.json`: per entry its type, text, whether it has a submenu, its rectangle, and the items of
each submenu) and grabs of the menu and of each submenu. `--client <fugue-qt.exe>` does the same in Fugue (its entries
then come from the recorder's menu marker), `--keep` keeps the runtime directory with Fugue's recording. `extract.py
<dir>...` turns stock runs into `stock.tsv` (per case and entry: the atom's name as the grab reads, the verbs' text,
the picture's file) and `icons/` (the 16x16 picture as the screen showed it, over the menu's background 240,240,240).
`frames.txt` and `res/` are a Fugue recording of a session in this world, which `shell/tests/mapmenu_test.cpp`
replays: it right-clicks every case where the probe clicked stock and compares the entries, their verbs' labels and the
pictures with `stock.tsv` and `icons/` (each channel within 1).

## What stock showed

Structure:

- One entry per listed atom, topmost first, **always with a submenu** of its verbs, also when it is the only entry.
  The atom entries are drawn by DreamSeeker itself (owner-drawn: Windows has no text for them), each 30 px high at
  150 %. The verbs are plain Windows menu items (35 px high), in the system's menu font; the atoms' names are drawn
  in the same font. No separators, anywhere.
- Two atoms of the same name are two entries (`twins`: the white one, created last, first).
- An atom with an empty name has no entry: with nothing else under the pointer no menu opened (`empty`).

The picture beside an atom's name:

- Every atom entry has one, at its left, **16x16 pixels** (at 150 % too, so not scaled with the display), 2 px from
  the entry's left edge and centred in its height. Verb items have none.
- It is the atom's icon in its direction (`dir-east` shows the east frame) and its icon_state, a missing state falling
  back to the icon's "" state (`nostate`), with its **overlays and underlays** (`overlay`, `underlay`), its **client
  images** (`imaged`: an image attached to it in client.images) and its **vis_contents members** (`holder`'s picture
  has the member's white square; the member's own entry shows the member alone).
- Not applied: **color** (`colored` is white), **alpha** (`alpha` is opaque), **transform** (`scaled`, `turned`
  show the icon as it is), **filters** (`outlined` has no outline), the atom's **pixel offset** (`offset`, also
  right-clicked where only its offset part is, `offset-up`).
- The whole icon is scaled to 16x16, each pixel the **average** of the pixels it covers: a 64x64 icon becomes 16x16
  (`big`, clicked on either of its cells), a 16x16 one stays as it is (`small`), and the 3 px black diagonal of the ""
  state comes out as a black diagonal with grey (96,96,96) beside it (`nostate`).
- An atom with no icon gets a blank space, and its name stays where the others' are (`iconless`).
- The same for a turf (`stack`'s verbed-turf), a mob (`mob`) and a HUD object (`hud`, client.screen at 18,1).
- **The appearance at the moment the menu opened**, kept while it stays open: an animated state shows the frame it
  had then, each time another one (`anim`, `anim-b`, `anim-c`: red, blue, green; the map had moved on to the next
  frame by the time of the first grab), and does not move on; an atom whose icon_state the world flips every second
  kept the state it had (`changer`, grabbed three times 1.1 s apart while the map flipped).

The text of an atom's entry: its name without text macros (`\improper the Rock` is "the Rock", `\proper rock` is
"rock"), `&` and HTML shown as they are ("Tom & Jerry", "<b>bold</b> name", the last letter of which is under the
submenu arrow), non-ASCII letters as they are ("café ünï"), a long name whole, however wide the menu gets, and no
suffix (`suffix`).

The verbs (`verbs`, one obj with every variant), in this order:

    nullcat | acat |  under | <b>Bold</b> | Capital | Cut & Paste | Look At | a-b | described | do thing | examine |
    near | poke | withargs | x10 | zcat

- An item is the verb's **name alone**: no category. `set name` replaces the proc's name ("Look At", "a-b", "Cut &
  Paste", which Windows has as "Cut && Paste", "<b>Bold</b>"). The proc's underscores are spaces (`do_thing` is "do
  thing", `_under` " under"); the server sends them so. Case stays. `set desc` is not shown.
- Not listed: `set hidden = 1`, `set popup_menu = 0`, and `set src in usr` for an obj on the floor. Listed: a verb with
  an argument (`withargs`), `set src in oview()` (`near`) and the player's own verb by its argument (`examine`).
- Order: by category, then by name, both compared as raw bytes (" " before "<" before capitals before small letters).
  A verb that sets no category sorts as "Commands"; `set category = null` sorts first, as an empty category (on the
  wire: an empty category with verb flag 0x20, where a verb that sets none has an empty category without it). The
  player's argument verbs are sorted in with the atom's own.

Which verbs are listed, seen in passing: examine is offered for `changer` at 18,9, nine cells east of the player, on the
view's east edge; for `holder`'s member (a vis_contents member with no loc) only poke is, not examine.

## Fugue before and after

Before: an entry had no picture; a verb with a category read "Zeta / zcat"; `set category = null` sorted among the
verbs with none ("nullcat" between "near" and "poke"); the atoms' names were in the menu bar's font (Tahoma 11 px), the
verbs in the system's, so the two differed in size; examine was missing for `changer` (the menu's view range was
`(18-1)/2 = 8` cells every way) and offered for `holder`'s member.

After: the picture is drawn from the sprites of the scene the right click was made on (`shell/mapmenu.h`
mapMenuIcon), the verbs are their names in stock's order (`model::menuVerbLabel`, atomMenu), the menu uses the system's
font throughout, the menu's default view range is the cells the map shows (`model::menuViewReach`), and a member with
no loc is reached by no range clause. All the cases above compare equal with stock, the pictures within one level per
channel.

## Differences left

- The menu's look is Qt's: item heights, the picture's distance from the edge, the highlight (stock leaves the
  picture's cell unhighlighted) and the background (Qt's Windows 11 style draws the menu white, where stock's is
  240,240,240, so a white picture is hard to see). The html entry's clipped last letter is not copied.
- Not measured: a picture from an icon that is not square, an icon smaller than 16x16, overlays with pixel offsets of
  their own, a world whose `icon_size` is not 32, a display at 100 %, and the north edge of an even view (only the east
  edge was clicked). The vis_contents member placed elsewhere on the map (vismenu-world walker) was not looked at here.

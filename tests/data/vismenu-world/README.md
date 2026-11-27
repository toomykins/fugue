# Right-click menus on vis members, and a z change under a held button

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-24, port 18790. No game assets or screenshots in Git.
The world needs nothing of its own: `tools/stockprobe/prepare.py <this directory> <dir>` writes it with the shared
576x384 skin and solid icons. Four iconless holders on row 10 each show one
vis_contents member: a mob with no loc (`ghost`), a mob standing elsewhere on the map (`walker`), a turf of the same z
(`copied-turf`, 18,1,1) and a turf of another z (`far-turf`, 3,3,2). Every turf and `/mob/dummy` has two verbs, `near`
(`set src in view()`) and `anywhere` (`set src in world`); a verb logs the atom it ran on and where that atom is.
`jumper` moves the clicking mob to z 2 three ticks after its MouseDown.

`tools/stockprobe/stockprobe.py --world <this directory> click <name> ACTION...` does the same right clicks, menu picks
and press/release in the stock window and in Fugue's and prints what the world logged for each. The actions recorded:

    right:48,80 menu:1/anywhere right:112,80 menu:1/near right:112,80 menu:1/anywhere right:176,80 menu:1/anywhere
    right:176,80 menu:2/anywhere right:240,80 menu:1/anywhere right:240,80 menu:2/anywhere down:336,80 wait:2 up:336,80

`stock.tsv` is what stock did. `viscontents_test::menuTargetsLikeStock` consumes its part B.

## Independent conclusions

- **A mob member's verbs run on the mob**, whether it has a loc or not. One with no loc is offered only verbs it is
  reachable for without one (`anywhere`, not `near`).
- **A turf member's verbs run on that turf**, at its own cell, not on the turf the holder stands on (which is the
  next entry of the same menu).
- **A turf member from another z has an entry** (with `anywhere` only), but picking its verb ran nothing.
- **A release after the map changed z** is an ordinary release on the new scene: MouseUp and Click of what is under
  the pointer there, though the press was on an atom of the old z.

## Fugue after the change

Picks on the mob members and on the same-z turf member match stock (before, a pick on a mob member sent nothing and a
pick on a turf member ran on the holder's turf). The release after a z change matches stock (before, nothing was sent).

## Differences left

- Fugue offered `near` for the mob with no loc; stock does not. Since tests/data/menu-world a member with no loc is
  reached by no range clause in Fugue's menu (measured there on an obj member; this world was not run again).
- Fugue lists no entry for the turf from another z; stock lists one whose pick does nothing. Before the change Fugue
  listed the turf of the same cell on the map's own z instead.
- Fugue does not offer `near` for `copied-turf` (the view's edge cell 18,1); stock does, and picking it ran nothing.

## Not covered

What stock sends for the pick that ran nothing (only the world's log was read). Members under a HUD holder, nested
members, and drags across a z change.

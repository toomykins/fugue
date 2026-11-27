# Synthetic MAP visibility fixture

See [renderer visibility](../../../docs/RENDERING.md#visibility-and-lighting) for the
supported scope and remaining limitations.

Generate into a **separate** disposable directory:

```sh
python -B tests/data/visibility-world/prepare.py build/visibility-runtime
'C:/Program Files (x86)/BYOND/bin/dm.exe' build/visibility-runtime/world.dme
```

The generator makes all PNGs and an initial `phase.txt`. Check an unused port, then
launch a separate safe/invisible server, then stock and Fugue sequentially with
separate client state. Collect observations manually and close only processes you
started; see [fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

The initial observer is at (5,6,1), wall at X=6, target object at (8,6,1), target
mob at (8,7,1), foreground object at (4,6,1). A yellow HUD square and independent
INFO target rows must survive occlusion. INFO can click the hidden object.
Targets retain their identities across phases; movable blockers can be changed
in place with action commands.

Write a query string **without duplicate keys or a trailing newline** to runtime
`phase.txt`. Available controls:

- `shape=wall|point|corner|diagonal|corridor|custom`; custom `cells=6:5,5:6`.
- `opaque=0|1`, `dense=0|1`, `movable=1`, `alpha=0`, `door=open`.
- `x`, `y`, `z`, `view`; `eye=object` separates the eye from the player.
- `sight=objs|mobs|turfs|through|self`; omit for normal sight.
- `invisible=1`, `see=50`; `self_invisible=1`, `self_overlay=0|50`.
- `large=1`, `offset=-96`, `affine=1`, `overlay=1`.
- `image=1` with optional `image_offset=-96`.
- `vis=1` with optional `vis_offset=96`.
- `action=toggle|move|remove` mutates the existing movable blockers. Add an unused
  `run=<nonce>` parameter when repeating a command.

Wait for a new matching `PHASE` or `ACTION` line in `events.log` before capture.
The log contains synthetic visibility fields and mouse callbacks, not game data.
`stock-masks.json` retains 80 seeded stock `view()` fields for the default 11×11
world with eye (5,6), radius 5. It is supplementary server-proc evidence, not a
substitute for stock MAP screenshots or proof of appearance/picking behavior.

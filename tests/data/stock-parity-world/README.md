# Stock parity sets

Probe sets where the old renderer and the rewrite disagreed, checked against independent DreamSeeker/DreamDaemon
**516.1689** (2026-11-23/24). No game assets in Git; the only screenshots are stock's maps of this world under
`replay/`.

One world holds every set; DreamDaemon runs one per start with `-params "set=<name>"` (the names are in `world/New()`
in `world.dm`). `prepare.py <dir>` writes the world, a 576x384 skin and solid icons. `tools/stockprobe/stockprobe.py`
runs DreamDaemon, stock and Fugue on the same server: `pair` grabs both maps at 1:1 with a picture of where they
differ, `click` posts the same clicks to both and prints what the world logged, `record` writes a set's files under
`replay/` (Windows, with BYOND; `--help` has the rest). `compare.py <dir> <set>...` reads stock's pixel at a few points
of each 96 px case crop and Fugue's difference from stock, from grabs named `<set>-stock/f0000.png` and
`<set>-fugue/f0000.png`.

`replay/` lets 30 sets be checked with no server and no DreamSeeker, on any host
(`compositiongpu` suite, `stockParityReplay` check, CTest `shell_composition_stock_gpu`): `<set>.txt` is what
DreamDaemon sent this client for the set (`FUGUE_RECORD_DIR=<dir>` on a client connected to the world, its `frames.txt`),
`res/` the resources of all sets,
`<set>.png` stock's 576x384 map of the same world, grabbed six seconds after login, and `recorded.tsv` the BYOND
version and display scale of each recording. `tools/stockprobe/stockprobe.py record <set>...` writes them all; a test that
fails saves what it drew in the temporary directory and names the file. The test replays the frames, draws the map
offscreen with the real renderer and allows no channel of any pixel more than 8 levels from stock's. It passes on
Direct3D 11 (hardware and Windows' software device, which the test uses), on OpenGL with an NVIDIA driver, and on Linux
with Mesa's llvmpipe (Ubuntu 24.04 under WSL, Qt 6.8.3, xvfb); Qt's offscreen platform has no OpenGL on Linux, so CI runs
the `gpu` CTest profile under Xvfb. Stock's images of these sets did not change between a display at 150 % and at 100 %.

Run `ctest --test-dir build -R '^shell_composition_stock_gpu$' --output-on-failure` after building, with the
configured build directory and `-C Release` for Windows multi-config builds. A missing GPU fails the selected check.
The test requires the wire/image inventory to match `recorded.tsv`; it does not silently drop missing screenshots.
See [test profiles](../../../docs/BUILDING.md#test-profiles) for platform setup.

The other sets are compared as whole grabs or by eye and have no replay: `plane-alpha-mask*`, `lighting-*`,
`client-*`, `client-dir-*`, `screen-loc-*` and `moving-atoms`. Sets that drew maptext were removed; the maptext rules
the renderer follows are stated, with the stock version, in the code that applies them.

DreamSeeker's Direct3D 9 map stays blank while the display is idle: wake it (a mouse move) before starting stock.

## Independent conclusions

`images-groups`, images and groups:

- An image with `FLOAT_LAYER` (an image's default) or any other negative layer draws at layer 5, on an obj, a turf or
  a screen object, whatever its target's layer, and under objects at exactly 5 (`img-default`, `img-float`,
  `img-neg2`, `img-float-parent7`, `img-float-tie5`, `turf-img-float`, `hud-img-float`). An explicit layer is kept
  (`img-explicit`).
- An image on a `KEEP_TOGETHER` target is drawn inside the target's surface: the target's alpha applies once to both
  (`img-kt`). `KEEP_APART` or another plane keep it outside, with the target's alpha (`img-kt-apart`,
  `img-kt-plane`); `override` replaces the surface and takes neither its colour nor its alpha (`img-kt-override`).
- A target's colour matrix colours its image (`img-matrix-parent`).
- An overlay on another plane inside a `KEEP_TOGETHER` group is drawn on its plane, outside the surface, with the
  group's colour and alpha (`kt-overlay-plane`).
- Layer ties inside a group keep the order the overlays were added in, `BLEND_ADD` ones included (`kt-add-first`,
  `kt-add-last`).
- `BLEND_INSET_OVERLAY` draws over the group's surface, masked by its alpha (`kt-inset`).
- A screen object's `vis_contents` draw at every position of a `screen_loc` range, follow a `screen_loc` pixel offset
  and not the holder's `pixel_x`, and draw under an iconless holder (`hud-vis*`).

`images-groups-more`, more of the same:

- On a plain translucent target an image takes the alpha, `override` or not, and the colour (`img-alpha`,
  `img-alpha-override`, `img-color-override`, `img-alpha-apart`).
- Inside the surface the image sorts by its layer (`img-kt-layer2`: under the target), turns with the target's
  matrix (`img-kt-turn`), and several images share it (`img-kt-two`). A screen object's image joins too (`hud-img-kt`).
- An overlay that leaves two nested groups takes the innermost group's alpha only (`kt-nested-plane`).
- Outside a group `BLEND_INSET_OVERLAY` draws like an ordinary overlay over what is under it (`inset-flat`).

`group-blend-add`, `BLEND_ADD` inside a group:

- An additive overlay adds onto the group's surface; an additive underlay is under the root's icon like any other,
  in a group or not, opaque or translucent; an ordinary overlay added after an additive one draws over it
  (`kt-add*`).
- A group whose root is additive draws its contents normally and adds the surface to the map
  (`kt-root-add-under`, `kt-root-add-over`, `kt-root-add-under-add*`).

`plane-mask-filters`, a plane master's alpha-mask and colour filters (the mask is another plane's master,
`render_target = "*mask"`):

- `x`, `y` move the mask (y up); where it does not reach, it masks as transparent (`mask-x8`, `mask-y8`).
  `MASK_INVERSE` masks by 1 - alpha (`mask-inverse`, `mask-x8-inverse`). `MASK_SWAP` draws the mask's pixels, masked
  by the plane's alpha (`mask-swap`).
- The filter runs wherever it stands in a chain with blur, outline and drop_shadow (`mask-blur`, `blur-mask`,
  `mask-outline`, `mask-shadow`), twice (`mask-twice`), and with a colour filter (`mask-colorfilter`,
  `colorfilter-blur`).
- The master's own colour, colour matrix and alpha apply after its filters (`mask-color`, `mask-matrix`,
  `mask-alpha`, `colorfilter-color`).

A 32 px `render_target` used as a mask sits at the middle of the view, not on the atom that made it (first run of
`plane-mask-filters`, not kept).

`color-matrix-alpha`, a colour matrix and alpha:

- Once an atom has a colour matrix its alpha lives in the matrix: `alpha` set afterwards replaces the matrix's own
  alpha factor, and the scalar colour and alpha sent next to the matrix are whatever was set before it. Stock draws
  the matrix alone, flat or in a group (`base`, `matrix-rgb-only`, `flat-alpha-before`, `flat-rgba-before`,
  `flat-alpha-after`, `kt-alpha-after`); a constant alpha term still applies (`const-alpha`).

`animated-groups`, animating `KEEP_TOGETHER` targets (animations of 10 and 100 minutes, so a grab is their first
frames):

- An image stays composed in its target's surface while the target animates its offset, alpha, colour or transform
  (`anim-pixel`, `anim-alpha`, `anim-color`, `anim-transform`, `anim-turn45-image`, `anim-scale-image`).
- An image that was already animating when the client was first sent it runs its animation: the animate message
  comes just before the image (`anim-image`, `anim-image-flat`).
- A group draws through a transform animation like any other (`anim-transform-noimg`, `anim-turn-overlay`,
  `anim-turn45-overlay`).

`plane-alpha-mask`, `-offset`, `-large`, `-screen-source`, `-visible-source`, `-inverse`, `-large-shifted` (one
view-wide case per run, compared as whole grabs) and `alpha-masks`, alpha masks that are not a plane:

- A mask that is an atom's `render_target` or an `icon` is that icon at its own size, centred on the surface the
  filter is on and then moved by x, y: the view for a plane master (`plane-alpha-mask*`, `plane-icon`), the atom's
  icon for an atom's own filter (`atom-icon*`, `atom-source*`, `atom-icon96`). Where the source atom stands, its pixel
  offsets and its `screen_loc` do not matter.
- `MASK_INVERSE` and `MASK_SWAP` work the same with these masks; the mask runs in list order with other filters
  (`atom-icon-outline`, `atom-outline-icon`), on a group's surface (`kt-icon`) and before the atom's transform
  (`atom-icon-turn`).
- The record: x, y (f32), flags (u16), the icon's resource ordinal (u32, 0xffff for none), the source name and a NUL.

`repeated-filters`, several filters of one type:

- A filter record's byte numbers the filter among those of its type on the list, so a second outline or blur is 2.
  Each runs in turn, on an atom and on a plane master (`two-outlines`, `three-outlines`, `two-blurs`,
  `blur-outline-blur`, `two-shadows`, `two-colors`, `three-colors`, `two-masks`, `plane-*`).

`sweep`, combinations of groups, images, overlays, filters, transforms, render targets and animations, and nine
`relay-*` sets of one relay each (`relay-hud`, `-turn`, `-color`, `-outline`, `-noicon` on screen objects;
`relay-obj`, `-turn`, `-color`, `-outline` on objs), looking for atoms Fugue does not draw:

- An atom with a visible `render_target` draws as itself when it is turned, filtered or a translucent group
  (`target-turn`, `target-outline`, `kt-target`).
- An image that is `KEEP_TOGETHER` and has overlays is one surface of its own parts, which takes the image's alpha
  once, on a plain target and inside a `KEEP_TOGETHER` target's surface (`image-kt-own`, `image-kt-own-on-kt`). An
  image's `vis_contents` member is drawn, with `VIS_INHERIT_ID` too (`image-vis`).
- A screen object or an obj whose `render_source` is an atom's render target draws that atom's surface like an icon,
  at any `screen_loc`, with the relay's own colour and alpha (`relay-hud`, `relay-hud-noicon`, `relay-hud-color`,
  `relay-obj`, `relay-obj-color`).

`lighting-multiply`, `lighting-emissive-mask`, `lighting-relay`, `lighting-matrix-blur`, `lighting-game-relays`, five
lighting-style plane setups compared as whole grabs (a multiply plane, an emissive plane masking it, plane relays with
colour and blend, a blurred game plane):

- The five plane setups are identical in both clients.

`blend-reset-layers`, blend modes, RESET flags, 64 px icons, layer ties and overlay order (24 cases), and
`lighting-translucent`, `lighting-translucent-tinted`, a translucent multiply plane:

- `BLEND_MULTIPLY` of an icon multiplies by the icon's straight colour and keeps the destination by 1 - alpha, so a
  half-transparent yellow over cyan gets brighter (`overlay-multiply`); a group's or a plane's surface multiplies
  premultiplied (`attachment-tree-world`, `lighting-translucent*`). Everything else in the set already matched.

`client-*`, client settings, compared as whole grabs (`client-color-lighting-relays`, `client-matrix-lighting-mask`,
`client-color-plane-mask` and `client-color-alpha-masks` put a client colour on `lighting-game-relays`,
`lighting-emissive-mask`, `plane-mask-filters` and `alpha-masks`; `client-color-tween` and `client-matrix-tween` hold a
colour and a matrix mid-animation):

- `client.color`, a plain colour or a matrix, tints everything the map draws, screen objects included, except atoms
  and plane masters with `NO_CLIENT_COLOR`; the colour's alpha is applied as alpha (`client-color`,
  `client-color-matrix`, `client-color-exempt`, `client-color-alpha`).
- Where it is applied: to an icon once; to a `KEEP_TOGETHER` group or an atom with filters on its surface, after the
  atom's own colour and filters (an outline is tinted too); to a relay of an atom's render target twice, around the
  relay's own colour, because the target is drawn tinted; to a relay of a plane not at all; and once more to the
  surface of a plane master that has filters.
- `client.pixel_x/y/w/z`, a smaller `client.view` and `client.eye` already matched (`client-pixel-xy`,
  `client-pixel-wz`, `client-view-small`, `client-eye`).

`directions`, `icon-states` and `layers-planes`, directions, icon states, layer and plane edge values, and
`moving-atoms`, atoms that keep moving (several grabs, read by eye, stamped with wall-clock times):

- A four-direction state draws east for any `dir` with the EAST bit, else west, else north, else south: diagonals
  take their horizontal part, `NORTH|SOUTH` is north, `EAST|WEST` east, 7, 13, 14, 15 and `UP|EAST` east, 11 west, 0
  and `UP` south (`d4-*`).
- An icon-less overlay with a named state draws that state of its parent's icon (`overlay-no-icon`).
- A name that only has a `movement = 1` state draws it at rest, before the unnamed default (`run-still`). A name with
  both draws the ordinary one at rest (`walk-still`) and the movement one for as long as the atom glides, on a mob, an
  obj and an overlay alike (`moving-atoms`).
- A looping state counts its frames from about the moment the client connected: with 8 s frames stock changed frame
  16.9 s after the client was started whether the world was 3 s or 14 s old, and an atom made later is in phase
  (`moving-atoms` `slow`; `anim4-*` with 300 s frames show the first frame).
- Already the same in both: a missing state falls back to the unnamed one and draws nothing without one, state names
  are case sensitive, `loop = 1` ends on its last frame, 64 px states, `step_x/y`, overlays, underlays, images and
  `VIS_INHERIT_DIR` members take the parent's direction unless they set one, and every layer and plane value of
  `layers-planes` (`BACKGROUND_LAYER`, `EFFECTS_LAYER`, `TOPDOWN_LAYER`, negative and float layers and planes).

`screen-loc-forms`, `-forms-64px`, `-forms-small-view`, `-percent-ranges` and `screen-loc-off-view-1` to `-23`,
`screen_loc` forms, compared as whole grabs:

- Anchors are read in any case (`center-6,center`, `left+2,bottom+1`, `northeast`, `bottomleft`, `top,CENTER`).
- A percent is a share of the view less one tile, from the anchor it follows or from LEFT / BOTTOM by itself:
  `LEFT+50%`, `50%,50%`, `RIGHT-10%`, `CENTER+10%`, `3+50%`, `WEST+100%`, `BOTTOM+33%`, with a pixel offset after it
  (`LEFT+25%:8`). Between pixels it goes left and up.
- A range starts at its lower corner wherever that is, on a half tile too, and covers the other corner:
  `CENTER-4,CENTER-1 to CENTER-3,CENTER+1` on the 18x12 view is 2x3 boxes from 5.5,5.5; `2.5,11 to 4.5,11` three;
  `CENTER-1,CENTER to 12,CENTER` five (8.5 to 12.5); `1.5,3 TO 2,4` four.
- A range that runs down to a CENTER or from a half tile (`NORTH,EAST TO CENTER,CENTER`, `12,8 to CENTER+1,9`)
  fills the view or more in stock; Fugue does not draw it.
- A screen object on a tile outside the view makes the map larger by whole tiles on that side, black where there is
  no map, and the larger scene is centred in the control: `0,6` moves the map 16 px right, `-1,6` 32, `6,15` 48 down,
  `0,6` with `19,6` not at all (both columns half cut), `0:16,6` like `0,6`, and `18:8,6` (a pixel offset past the
  edge) not at all (`screen-loc-off-view-1` to `-20`). A 64 px icon counts as two tiles: on `18,6` or `3,12` it adds a
  column or a row, also with a pixel offset that pulls it back inside (`screen-loc-off-view-21` to `-23`).
- In the larger scene stock leaves a strip at the left and top of the control without map, 16 px times (near +
  max(near, far)) for the tiles added on the near (left, top) and far side: 32 px for `0,6`, 16 for `19,6`, 32 for
  both, 64 for `-1,6`, 96 for `-2,6` and for `6,15`. Screen objects are drawn there.
- With a larger scene `LEFT`, `RIGHT`, `TOP`, `BOTTOM` and percents stand on the edges of the control, not of the
  view: with 64 px icons `RIGHT,5` ends at the control's right edge and `LEFT+25%` is a quarter of the control's
  width less the icon (`screen-loc-forms-64px`).

`client-dir-north`, `-south`, `-east`, `-west`, `-northeast` and `client-dir-east-scene`, `client-dir-south-pixel`,
compared as whole grabs:

- SOUTH, EAST and WEST turn the map so that direction is up; NORTHEAST draws like NORTH. The server sends the
  direction in its own message (S2C 0x70, one byte).
- Tiles and pixel offsets turn, an overlay's too. Icons stay upright on the bottom-left of their turned tile (a 64 px
  icon still grows right and up), and a transform is not turned (`Translate(10, 0)` still moves right).
- An atom shows its direction turned with the map: under EAST an EAST-facing atom shows its NORTH picture, on the HUD
  too. An overlay with a direction of its own keeps it.
- The HUD is not turned. It is laid out on the turned view: an 18x12 view under EAST is 12x18, centred in the
  control and cut off where it does not fit.
- Clicks report the world tile, `icon-x`/`icon-y` and `screen-loc` of the turned picture, and the movement keys go by
  the screen: under EAST up, right, down, left move east, south, west, north (clicks posted to both clients).

`overlay-relays`, relays and render targets on overlays and `vis_contents` members (24 cases; tgstation's eyelids are
this shape: hidden target objects in a `KEEP_TOGETHER` mob's `vis_contents`, relayed by an icon-less overlay):

- An overlay with `render_source` draws that target where the overlay stands, with the overlay's own pixel offset,
  colour, alpha and matrix, as an underlay too, on a screen object and on an image, and inside its atom's
  `KEEP_TOGETHER` surface, whose alpha then covers it. A name nothing produces draws nothing.
- What is captured includes the producer's own colour and alpha; a producer with alpha 0 relays nothing.
- A `vis_contents` member is a producer like any atom, with the direction it inherits from its holder.
- An overlay can be a render target itself: hidden when its name starts with `*`, drawn where it stands otherwise,
  and relayed either way.

`screen-loc-signs`, a signed number after the operator in `screen_loc`: the number's own sign counts. `CENTER+-1` is
one tile left, `CENTER-+2` two right, `CENTER--3` three left, `5+-1` is 4 (tgstation writes its hand slots as
`CENTER+-1:16,SOUTH+0:5`).

`tgstation-refusals`, what tgstation does that the rewrite refused to draw (23 cases):

- `KEEP_APART` with no `KEEP_TOGETHER` surface above it changes nothing, on a `vis_contents` member's overlay as
  anywhere (`rf-vis-blocker*`: tgstation shows a worn item in its inventory slot's `vis_contents`, and nearly every
  item has an emissive blocker overlay with `KEEP_APART | KEEP_TOGETHER | RESET_COLOR`). Such an overlay keeps its own
  filters, and its atom's (`rf-ka-filter*`, `rf-blocker-root-filter`: the hunger bar's outlined burger, an item's
  hover outline).
- A hidden render target with `BLEND_ADD` captures as it would without, colour included, also when it is a
  `KEEP_TOGETHER` screen object without children; its relays blend by their own mode (`rf-target-add*`,
  `rf-target-star*`: tgstation's starlight). With `BLEND_MULTIPLY` it captures nothing (`rf-target-mul*`).

`relay-overlay-matrix`, filtered overlays in groups, a hidden target's overlays, `BLEND_ADD` order, and relay overlays
on transformed atoms (24 cases, all exact):

- A blurred, outlined or drop-shadowed overlay is filtered by itself and, in a translucent `KEEP_TOGETHER` group,
  composed in the group's surface, whose alpha then applies once, also where the overlay reaches past the root's
  picture (`rj-blur-overlay*`, `rj-kt-*-overlay*`). Fugue already drew all of these.
- A hidden render target's overlays, without `KEEP_TOGETHER`, are drawn on the map where the target stands, additive
  or not (`rj-hidden-*-floor`, `rj-hidden-add-child-after`).
- On one tile and layer `BLEND_ADD` takes no turn of its own: an additive atom or overlay made before an opaque atom
  is drawn under it (`rj-add-obj-before`, `rj-add-overlay-before`, `rj-add-overlay-before-kt`,
  `rj-hidden-add-child-before-kt`; Fugue drew additive parts after the others).
- A relay overlay on a turned, scaled or moved atom takes the atom's matrix after its own, as any overlay does: about
  the relayed icon's centre, its pixel offset unturned, its own translation turned (`rj-turn45-relay-overlay`,
  `rj-scale-relay-overlay`, `rj-turn-relay-own-scale`, `rj-scale-relay-own-turn`, `rj-turn-relay-own-move`,
  `rj-move-relay-overlay`), as an underlay too, filtered before the matrix, with the atom's colour, unsmoothed with
  `PIXEL_SCALE`, and upright with `RESET_TRANSFORM` (`rj-turn-relay-*`).

`animated-render-target`, an animated render target (11 cases, all exact in each of six grabs half a second apart,
the animation in step in both clients; tgstation's blink is a hidden-target eyelid in the mob's `vis_contents` whose
alpha is animated between 0 and 255 with zero-time steps):

- An atom that is a render target or a relay animates like any other, as a `vis_contents` member too: its relays show
  it while its alpha is up and nothing while it is 0 (`rk-blink*`). Fugue did not draw an animating target or relay at
  all, so the eyelids never closed.
- A layer set after `animate()` began is the one drawn (`rk-blink-layer-late`).

`turned-mob-images`, images on a turned `KEEP_TOGETHER` atom, a lying tgstation mob (14 cases, all exact):

- An image that does not join the atom's surface (another plane, `KEEP_APART`, or `RESET_TRANSFORM`: progress bars,
  HUD icons, runechat) is drawn as on a plain turned atom, turned with it unless it resets the transform, and with
  the atom's colour and alpha unless it resets those (`rl-kt-image-*`). Fugue drew none of them.
- An `override` image on such an atom stands upright in the atom's place: it replaces the surface and takes neither
  its colour and alpha nor its matrix (`rl-kt-image-override`; Fugue drew the atom and no image).

`surface-bounds`, where a turned or scaled surface ends (before, every `KEEP_TOGETHER` group turned by other than a
quarter turn, or scaled, differed from stock by a ring of pixels):

- A `KEEP_TOGETHER` group with more in it than one icon is drawn on a surface as large as the box of what is in it.
  An icon counts by its pixels that are not fully transparent: one of alpha 1 widens the box (`ta-kt-faint-turn`).
  An overlay, an image and a `vis_contents` member count by their own such box (`ta-kt-overlay-out-turn`,
  `ta-kt-image-out-turn`, `ta-kt-vis-out-turn`), also at alpha 0 (`ta-kt-hidden-overlay-turn`).
- The group's matrix samples that surface smoothed and premultiplied, repeats its edge texel at the box, and draws
  nothing beyond the turned box. An edge of the picture that lies on the box is therefore hard at any angle or scale
  (`ta-kt-turn`, `ta-kt-turn10`, `-turn30`, `-turn60`, `ta-kt-scale2`, `ta-kt-full-turn`, `ta-kt-odd-turn`,
  `ta-kt-big64-turn`), one inside the box is soft (the notch of `ta-kt-ell-turn`, the far side of an overlay that
  sticks out), and a single pixel scaled by 4 is a 4 px block that fades only towards the next pixel in the surface
  (`ta-kt-dot-scale4`). `PIXEL_SCALE` on the group samples it unsmoothed (`ta-kt-turn-pixelscale`). An opaque group is
  drawn the same way (`ta-kt-opaque-turn`), and a nested one too (`ta-kt-nested-turn`).
- A group with nothing in it but one icon has no surface: the icon is drawn like any turned icon, its edge smoothed
  towards transparent black (`ta-kt-alone-turn`, `ta-kt-alone-opaque-turn`, and `sweep` `overlay-apart-in-kt-turn`,
  whose only overlay leaves the group).
- A filter on the group keeps a soft edge all round: its surface is the parts' whole icons and the filter's reach
  (`ta-kt-color-filter-turn`, `sweep` `kt-outline-anim-turn`).

Fugue after that: every `surface-bounds` case matches, with the cases of `animated-groups`, `sweep` and
`turned-mob-images` that differed only at such an edge.

`shared-render-target`, several atoms with one `render_target` name (12 cases):

- A relay of the name shows one of them: the southmost, among those the westmost, and on one tile the one made
  first. Their layers and the order they were made in on different tiles do not matter.
- Each of them still draws itself when the name has no `*`.

`render-target-relays`, relays of an atom's render target with a matrix or filters (24 cases, on the map and on screen
objects):

- The relay's filters run on the captured surface, then its matrix turns the result, then its colour and alpha apply;
  blend modes, pixel offsets and overlays of the relay work as on an icon.
- The matrix pivots on the centre of the producer's own icon: a 64 px producer relayed onto a 32 px object turns
  about the 64 px icon's centre, and a producer whose overlay sticks out still turns about its 32 px icon.
- `PIXEL_SCALE` on the relay samples the surface unsmoothed.

## Fugue after the change

`images-groups` to `repeated-filters` (137 cases, and the seven `plane-alpha-mask*` grabs) match stock exactly or
within one channel level, the three surfaces of `animated-groups` that are caught mid-turn or mid-scale included
(`surface-bounds` above).

In `sweep` and the `relay-*` sets every case matches. Before, atoms with a visible render target and a transform,
filter or group, and relays of an atom's target on a screen object or with a colour were not drawn at all. An image
that is itself `KEEP_TOGETHER` was not drawn either; it is now, as one surface of its parts (`image-kt-own*`), and so
is an image's `VIS_INHERIT_ID` member (`image-vis`).

`client-dir-*` and `client-dir-east-scene`, `client-dir-south-pixel`: all seven grabs are the same pixels, and the
clicks and keys the same log lines in all four directions. Before, the map was drawn unturned (by the old renderer
too).

`overlay-relays` and `screen-loc-signs`: all 24 cases and all nine forms match. Before, an atom with a relay overlay
inside a group was not drawn at all (a tgstation human), nor a screen object placed with a doubled sign (one of its
hand slots).

`shared-render-target`: all 12 match. Before, a name with two producers was refused and its relays drew nothing.

`relay-overlay-matrix`: all 24 match. Before, a relay overlay on a turned, scaled or moved atom was drawn upright and
in place, and additive parts were drawn after every other atom on their tile and layer.

`render-target-relays` and `relay-hud-turn`, `relay-hud-outline`, `relay-obj-turn`, `relay-obj-outline`: all 28
match. Before, a relay of an atom's target with a matrix or a filter was not drawn at all.

Neither client colour nor its animation was drawn before (nor by the old renderer). The grabs now differ from stock
only where their base sets do, except `mask-swap` under a client colour (stock tints the swapped-in mask pixels a
third time).

`directions`, `icon-states` and `layers-planes`: every case matches. Before, the odd directions, the movement states
and the frame clock differed, in the old renderer too.

`screen-loc-*`: Fugue grows the scene by the same tiles and centres it, so every box and the map are where stock has
them, except those below. Before, a screen object outside the view was not drawn.

## Not covered

- Stock's strip without map at the left and top of a larger scene: Fugue draws the map there.
- `LEFT`, `RIGHT`, `TOP`, `BOTTOM` and percents in a larger scene stand on the view, 16 px per added tile away from
  stock's. A scene that grows by more than fits a 3840 px surface is not grown.

- A `client.view` larger than the map control is placed differently (`client-view-large`).
- Under `client.dir`: a glide, the darkness of an unlit area and draw order between equal layers were not probed.

- A mask on a surface whose parts reach outside the root icon is centred on the parts' bounds, which was not
  compared; clicks on any of these cases.

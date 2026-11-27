// vis_contents fixture: turfs and mobs as vis_contents members (set a), vis_flags (set b), transformed holders
// (set c), set a's remote turfs out of view (set d), and VIS_UNDERLAY under turf and nested holders, vis_flags on
// animating members and animating holders (set e). Cases sit on a 3-tile grid; run with -params "set=a" (default) to "set=e".
world
    name = "Vis probe"
    maxx = 18
    maxy = 12
    maxz = 2
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
var/remote_z = 1   // set d: the remote turfs of set a on z 2, out of view
/obj/holder
    icon = 'holder.dmi'
    icon_state = "a"
    layer = 5
/obj/kid
    icon = 'kid.dmi'
    icon_state = "b"
    layer = 5
    pixel_x = 8
    pixel_y = 4
/obj/wall
    icon = 'wall.png'
    layer = 4
/obj/thing
    icon = 'thing.png'
    layer = 3
/mob/npc
    icon = 'npc.png'
    layer = 4
/turf/floor
    icon = 'floor.png'
/turf/remote
    icon = 'remote.png'
/proc/T(a)
    var/matrix/M = matrix()
    M.Turn(a)
    return M
/proc/cell(i)
    return locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1)
/proc/slot(label)
    var/i = length(cases)
    var/obj/holder/O = new(cell(i))
    O.name = label
    cases += O
    world.log << "CASE [label]"
    return O
/proc/turfslot(label)
    var/i = length(cases)
    var/turf/t = cell(i)
    cases += t
    world.log << "CASE [label]"
    return t
/proc/kid(px = 8, py = 4, flags = 0)
    var/obj/kid/K = new()
    K.pixel_x = px; K.pixel_y = py; K.vis_flags = flags
    return K
// Remote turfs live on the top row (y = 12), each with its own contents.
/proc/remote(x, what = "")
    var/turf/remote/R = new(locate(x, 12, remote_z))
    if(findtext(what, "o")) new /obj/thing(R)
    if(findtext(what, "m")) new /mob/npc(R)
    if(findtext(what, "v")) R.vis_contents += kid(-6, -6)
    return R
/proc/set_a()  // turfs and mobs as members
    var/obj/holder/O
    var/turf/t
    O = slot("obj-turf"); O.vis_contents += remote(2)
    O = slot("obj-turf-om"); O.vis_contents += remote(4, "om")
    O = slot("obj-turf-px"); O.pixel_x = 6; O.pixel_y = -4; O.vis_contents += remote(6, "o")
    t = turfslot("turf-turf"); t.vis_contents += remote(8, "om")
    O = slot("obj-mob"); var/mob/npc/M = new(locate(17, 1, 1)); O.vis_contents += M
    t = turfslot("turf-mob"); M = new(locate(18, 1, 1)); M.pixel_x = 4; t.vis_contents += M
    O = slot("obj-turf-vis"); O.vis_contents += remote(10, "v")
    O = slot("obj-mix"); O.vis_contents += remote(12, "o"); O.vis_contents += kid()
    O = slot("obj-kid-turf"); O.vis_contents += kid(); O.vis_contents += remote(14, "o")
    O = slot("obj-turf-layer"); O.layer = 1; O.vis_contents += remote(16, "o")
    O = slot("obj-mob-turf"); M = new(locate(16, 1, 1)); M.vis_contents += remote(18); O.vis_contents += M
    O = slot("turf-plain"); O.icon = null; O.vis_contents += remote(1)
    O = slot("obj-turf-bare"); var/turf/b = O.loc; b.icon = null; O.vis_contents += remote(3, "o")
    O = slot("obj-turf-high"); var/turf/remote/R = remote(5, "o"); R.layer = 6; O.vis_contents += R
    O = slot("obj-turf-plane"); R = remote(7, "o"); R.plane = 1; O.vis_contents += R
    O = slot("obj-turf-south"); R = new(locate(13, 1, 1)); O.vis_contents += R
    O = slot("obj-l2-north"); var/obj/kid/K = kid(-8, -8); K.layer = 2; K.loc = locate(9, 12, 1); O.vis_contents += K
    O = slot("obj-l2-south"); K = kid(-8, -8); K.layer = 2; K.loc = locate(14, 1, 1); O.vis_contents += K
    O = slot("obj-l2-null"); K = kid(-8, -8); K.layer = 2; O.vis_contents += K
    t = turfslot("mob-holder"); M = new(t); M.vis_contents += kid()
    t = turfslot("mob-holder-id"); M = new(t); M.vis_contents += kid(8, 4, VIS_INHERIT_ID | VIS_INHERIT_PLANE | VIS_INHERIT_LAYER)
/proc/set_b()  // vis_flags. The kid sticks out down-right of the holder (layer 5) and a wall bar (layer 4) crosses it.
    var/obj/holder/O
    var/list/cases = list(
        list("none", 0, 3, 0), list("icon", VIS_INHERIT_ICON, 6, 0), list("state", VIS_INHERIT_ICON_STATE, 6, 0),
        list("dir", VIS_INHERIT_DIR, 6, 0), list("layer", VIS_INHERIT_LAYER, 3, 0), list("plane", VIS_INHERIT_PLANE, 3, 1),
        list("plane+layer", VIS_INHERIT_PLANE | VIS_INHERIT_LAYER, 3, 1), list("id", VIS_INHERIT_ID, 6, 0),
        list("underlay", VIS_UNDERLAY, 6, 0), list("underlay-l3", VIS_UNDERLAY, 3, 0),
        list("underlay+layer", VIS_UNDERLAY | VIS_INHERIT_LAYER, 6, 0), list("float", 0, FLOAT_LAYER, 0),
        list("underlay-float", VIS_UNDERLAY, FLOAT_LAYER, 0), list("hide", VIS_HIDE, 6, 0), list("all-inherit", 63, 3, 1),
        list("plane-holder", VIS_INHERIT_PLANE, 3, 0))
    for(var/list/c in cases)
        O = slot(c[1]); O.dir = EAST
        if(c[1] == "plane-holder") O.plane = 1
        new /obj/wall(O.loc)
        var/obj/kid/K = kid(12, -12, c[2]); K.layer = c[3]; K.plane = c[4]; K.dir = SOUTH
        if(c[1] == "hide") K.vis_contents += kid(-12, 12)
        O.vis_contents += K
    O = slot("hide-grand"); O.dir = EAST
    var/obj/kid/K = kid(12, -12); K.vis_contents += kid(-24, 24, VIS_HIDE); O.vis_contents += K
/proc/set_c()  // transformed holders
    var/obj/holder/O
    var/obj/kid/K
    O = slot("rot90"); O.transform = T(90); O.vis_contents += kid()
    O = slot("scale2"); O.transform = matrix(2,0,0,0,2,0); O.vis_contents += kid()
    O = slot("rot45"); O.transform = T(45); O.vis_contents += kid()
    O = slot("rot90-px"); O.transform = T(90); O.pixel_x = 6; O.vis_contents += kid()
    O = slot("kid-own"); O.transform = T(90); K = kid(); K.transform = matrix(0.5,0,0,0,0.5,0); O.vis_contents += K
    O = slot("shift"); O.transform = matrix(1,0,8,0,1,4); O.vis_contents += kid()
    O = slot("nested"); O.transform = T(90); K = kid(); K.vis_contents += kid(4, -10); O.vis_contents += K
    O = slot("kid-reset"); O.transform = T(90); K = kid(); K.appearance_flags = RESET_TRANSFORM; O.vis_contents += K
    O = slot("kid-ps"); O.transform = T(30); K = kid(); K.appearance_flags = PIXEL_SCALE; O.vis_contents += K
    O = slot("holder-ps"); O.transform = T(30); O.appearance_flags = PIXEL_SCALE; O.vis_contents += kid()
    O = slot("turf-rot"); O.transform = T(90); O.vis_contents += remote(2, "o")
    O = slot("scale-half"); O.transform = matrix(0.5,0,0,0,0.5,0); O.vis_contents += kid(16, 0)
/proc/set_e()  // VIS_UNDERLAY under turf and nested holders; animating members and holders
    var/obj/holder/O
    var/obj/kid/K
    var/turf/t
    // A turf holder (layer 2) with a VIS_UNDERLAY kid at the holder's layer, beside a layer-2 and a layer-3 obj.
    t = turfslot("turf-underlay-float"); t.vis_contents += kid(8, 4, VIS_UNDERLAY); t.vis_contents[1]:layer = FLOAT_LAYER
    t = turfslot("turf-underlay-inherit"); K = kid(8, 4, VIS_UNDERLAY | VIS_INHERIT_LAYER); t.vis_contents += K
    t = turfslot("turf-underlay-obj"); K = kid(8, 4, VIS_UNDERLAY | VIS_INHERIT_LAYER); t.vis_contents += K
    var/obj/thing/L2 = new(t); L2.layer = 2; L2.pixel_x = 12; L2.pixel_y = 6
    t = turfslot("turf-float"); K = kid(8, 4); K.layer = FLOAT_LAYER; t.vis_contents += K
    // A nested parent: holder -> kid -> grandkid with VIS_UNDERLAY at the kid's layer.
    O = slot("nested-underlay"); K = kid(8, 4); var/obj/kid/G = kid(4, -6, VIS_UNDERLAY); G.layer = FLOAT_LAYER; K.vis_contents += G; O.vis_contents += K
    O = slot("nested-float"); K = kid(8, 4); G = kid(4, -6); G.layer = FLOAT_LAYER; K.vis_contents += G; O.vis_contents += K
    // vis_flags on animating members, and a holder whose transform animates. A clock marker on (1,1) runs 0..320 px over the 20 s.
    var/obj/kid/C = new(locate(1, 1, 1)); C.pixel_x = 0; C.pixel_y = 0; C.layer = 9; animate(C, pixel_x = 320, time = 200)
    O = slot("anim-kid-layer"); K = kid(12, -12, VIS_INHERIT_LAYER); K.layer = 3; O.vis_contents += K; animate(K, pixel_y = 0, time = 200)
    O = slot("anim-kid-id"); O.dir = EAST; K = kid(12, -12, VIS_INHERIT_ID); O.vis_contents += K; animate(K, alpha = 0, time = 200)
    O = slot("anim-kid-underlay"); K = kid(12, -12, VIS_UNDERLAY); K.layer = FLOAT_LAYER; O.vis_contents += K; animate(K, pixel_x = 0, time = 200)
    O = slot("anim-kid-icon"); O.dir = EAST; K = kid(12, -12, VIS_INHERIT_ICON | VIS_INHERIT_DIR); O.vis_contents += K; animate(K, pixel_x = 0, time = 200)
    O = slot("anim-holder-turn"); O.vis_contents += kid(); animate(O, transform = T(90), time = 200)
    O = slot("anim-holder-scale"); O.vis_contents += kid(); animate(O, transform = matrix(2,0,0,0,2,0), time = 200)
    O = slot("anim-holder-pixel"); O.vis_contents += kid(); animate(O, pixel_x = 16, time = 200)
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
    if(params["set"] == "b") set_b()
    else if(params["set"] == "c") set_c()
    else if(params["set"] == "e") set_e()
    else if(params["set"] == "d")
        remote_z = 2
        set_a()
    else set_a()
/mob/observer
    invisibility = 101
/mob/observer/Login()
    ..()
    loc = locate(9,6,1)
    world.log << "READY [key]"

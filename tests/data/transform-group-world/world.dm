// Transforms combined with group effects: KEEP_TOGETHER, KEEP_APART and atom filters under a matrix (set a), and
// filtered or grouped children inside transformed trees (set b). Cases sit on a 3-tile grid; run with -params "set=a"
// (default) or "set=b".
world
    name = "Transform group probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
/obj/thing
    icon = 'shape.png'
    layer = 5
/proc/slot(label)
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    cases += O
    world.log << "CASE [label]"
    return O
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/scaled(x, y)
    var/matrix/M = matrix()
    M.Scale(x, y)
    return M
/proc/small(px, py)
    var/mutable_appearance/A = new()
    A.icon = 'small.png'
    A.pixel_x = px
    A.pixel_y = py
    return A
/proc/set_a()
    var/obj/thing/O
    var/red = filter(type = "outline", size = 1, color = "#ff0000")
    // Group pivot or per-part pivot: the overlay sits 14 px right of the root.
    O = slot("turn-overlay"); O.overlays += small(14, 0); O.transform = turned(90)
    O = slot("kt-turn"); O.appearance_flags = KEEP_TOGETHER; O.overlays += small(14, 0); O.transform = turned(90)
    O = slot("kt-scale"); O.appearance_flags = KEEP_TOGETHER; O.overlays += small(14, 0); O.transform = scaled(0.5, 1)
    // Filter then matrix or matrix then filter: a 1 px outline stretched 2x across is 2 px wide on the sides.
    O = slot("outline-scale"); O.filters = red; O.transform = scaled(2, 1)
    O = slot("shadow-turn"); O.filters = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ff00ff"); O.transform = turned(90)
    O = slot("blur-scale"); O.filters = filter(type = "blur", size = 1); O.transform = scaled(2, 1)
    O = slot("kt-outline-turn"); O.appearance_flags = KEEP_TOGETHER; O.overlays += small(14, 0); O.filters = red; O.transform = turned(90)
    O = slot("outline-turn-overlay"); O.overlays += small(14, 0); O.filters = red; O.transform = turned(90)
    O = slot("kt-alpha-turn"); O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.overlays += small(4, 4); O.transform = turned(90)
    O = slot("outline-scale-x"); O.filters = red; O.transform = scaled(1, 2)
    O = slot("shadow-scale"); O.filters = filter(type = "drop_shadow", x = 4, y = 0, size = 0, color = "#ff00ff"); O.transform = scaled(2, 1)
    O = slot("plain-turn"); O.transform = turned(90)
/proc/set_b()
    var/obj/thing/O
    var/mutable_appearance/A
    var/red = filter(type = "outline", size = 1, color = "#ff0000")
    // An overlay's own filter under a turned root.
    O = slot("overlay-own-turn"); A = small(14, 0); A.filters = filter(type = "outline", size = 1, color = "#00ff00"); O.overlays += A; O.transform = turned(90)
    // KEEP_APART overlay inside a turned KEEP_TOGETHER root.
    O = slot("ka-in-kt-turn"); O.appearance_flags = KEEP_TOGETHER; A = small(14, 0); A.appearance_flags = KEEP_APART; O.overlays += A; O.transform = turned(90)
    // KEEP_APART overlay on a turned root without KEEP_TOGETHER.
    O = slot("ka-turn"); A = small(14, 0); A.appearance_flags = KEEP_APART; O.overlays += A; O.transform = turned(90)
    // A KEEP_TOGETHER overlay with its own child, under a turned root.
    O = slot("kt-child-turn"); A = small(14, 0); A.appearance_flags = KEEP_TOGETHER; A.overlays += small(4, 4); O.overlays += A; O.transform = turned(90)
    // An overlay with its own matrix inside a KEEP_TOGETHER root with a matrix.
    O = slot("kt-overlay-own-matrix"); O.appearance_flags = KEEP_TOGETHER; A = small(14, 0); A.transform = scaled(2, 1); O.overlays += A; O.transform = turned(90)
    // KEEP_TOGETHER with outline and a 45 degree turn: sampling of the group surface.
    O = slot("kt-outline-45"); O.appearance_flags = KEEP_TOGETHER; O.overlays += small(14, 0); O.filters = red; O.transform = turned(45)
    O = slot("outline-45"); O.filters = red; O.transform = turned(45)
    O = slot("kt-45"); O.appearance_flags = KEEP_TOGETHER; O.overlays += small(14, 0); O.transform = turned(45)
    O = slot("turn-45-overlay"); O.overlays += small(14, 0); O.transform = turned(45)
    // Filtered overlay with its own matrix on a plain root.
    O = slot("overlay-own-scale"); A = small(14, 0); A.filters = filter(type = "outline", size = 1, color = "#00ff00"); A.transform = scaled(2, 1); O.overlays += A
    // An underlay under a turned KEEP_TOGETHER root.
    O = slot("kt-underlay-turn"); O.appearance_flags = KEEP_TOGETHER; O.underlays += small(-10, -10); O.transform = turned(90)
    O = slot("kt-pixel-turn"); O.appearance_flags = KEEP_TOGETHER; O.pixel_x = 6; O.overlays += small(14, 0); O.transform = turned(90)
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
    if(params["set"] == "b") set_b()
    else set_a()
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9,6,1)
        world.log << "READY [key]"

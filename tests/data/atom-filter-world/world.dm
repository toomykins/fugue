// Atom filters: outline, drop_shadow and blur on ordinary atoms, an atom with an overlay, a chain and a filtered vis
// copy (set a); combinations with underlays, KEEP_TOGETHER, images, maptext, colour and transforms (set b).
// Cases sit on a 3-tile grid; run with -params "set=a" (default) or "set=b".
world
    name = "Atom filter probe"
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
/proc/set_a()
    var/obj/thing/O
    O = slot("outline1"); O.filters = filter(type = "outline", size = 1, color = "#ff0000")
    O = slot("outline2"); O.filters = filter(type = "outline", size = 2, color = "#ff0000")
    O = slot("outline-sharp"); O.filters = filter(type = "outline", size = 2, color = "#ff0000", flags = OUTLINE_SHARP)
    O = slot("outline-square"); O.filters = filter(type = "outline", size = 2, color = "#ff0000", flags = OUTLINE_SQUARE)
    O = slot("outline-alpha"); O.filters = filter(type = "outline", size = 1, color = "#00ff0080")
    O = slot("shadow-default"); O.filters = filter(type = "drop_shadow")
    O = slot("shadow-offset"); O.filters = filter(type = "drop_shadow", x = 4, y = -4, size = 0, color = "#ff00ff")
    O = slot("shadow-barsign"); O.filters = filter(type = "drop_shadow", x = 0, y = 0, size = 3, offset = 2, color = "#00ffff")
    O = slot("blur1"); O.filters = filter(type = "blur", size = 1)
    O = slot("overlay"); O.overlays += image('small.png', pixel_x = 14); O.filters = filter(type = "outline", size = 1, color = "#ff0000")
    O = slot("chain"); O.filters = list(filter(type = "outline", size = 1, color = "#ff0000"), filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#0000ff"))
    O = slot("pixel-x"); O.pixel_x = 6; O.filters = filter(type = "outline", size = 1, color = "#ff0000")
    O = slot("vis-shadow"); O.icon = null
    var/obj/thing/K = new(); K.filters = filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff00ff"); O.vis_contents += K
    O = slot("plain")
var/list/pictures = list()
/proc/set_b()
    var/obj/thing/O
    var/red = filter(type = "outline", size = 1, color = "#ff0000")
    O = slot("underlay"); O.underlays += image('small.png', pixel_x = -10, pixel_y = -10); O.filters = red
    O = slot("kt-overlay"); O.appearance_flags = KEEP_TOGETHER; O.overlays += image('small.png', pixel_x = 14); O.filters = red
    O = slot("overlay-own"); var/mutable_appearance/A = new(); A.icon = 'small.png'; A.pixel_x = 14; A.filters = filter(type = "outline", size = 1, color = "#00ff00"); O.overlays += A
    O = slot("image"); O.icon = null; var/image/I = image('shape.png', O); I.filters = red; pictures += I
    O = slot("maptext"); O.maptext = "<span style='color:#ffffff'>Ab</span>"; O.filters = red
    O = slot("color"); O.color = "#808080"; O.filters = red
    O = slot("alpha"); O.alpha = 128; O.filters = red
    O = slot("wave-shadow"); O.filters = list(filter(type = "wave", x = 2, size = 2), filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff00ff"))
    O = slot("outline-blur"); O.filters = list(filter(type = "outline", size = 1, color = "#ff0000"), filter(type = "blur", size = 1))
    O = slot("turn"); var/matrix/M = matrix(); M.Turn(45); O.transform = M; O.filters = red
    O = slot("kt-underlay"); O.appearance_flags = KEEP_TOGETHER; O.underlays += image('small.png', pixel_x = -10, pixel_y = -10); O.filters = red
    O = slot("image-target"); I = image('small.png', O); I.pixel_x = 14; pictures += I; O.filters = red
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
        client.images += pictures
        world.log << "READY [key]"

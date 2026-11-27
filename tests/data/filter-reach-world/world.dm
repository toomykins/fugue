// Filters the atom-filter fixture left unfiltered: an image's own filters (also on transformed targets and with
// overlays), maptext with filters and KEEP_TOGETHER (set a); animating and flicking atoms with filters (set b); the
// wave, ripple and bloom types vg uses (set c). Cases sit on a 3-tile grid; run with -params "set=a|b|c".
world
    name = "Filter reach probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
var/list/pictures = list()
/obj/thing
    icon = 'shape.png'
    layer = 5
/obj/clock
    icon = 'small.png'
    layer = 6
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
/proc/picture(obj/O, f)
    var/image/I = image('shape.png', O)
    I.filters = f
    pictures += I
    return I
/proc/set_a()
    var/obj/thing/O
    var/image/I
    var/text = "<span style='color:#ffffff'>Ab</span>"
    var/red = filter(type = "outline", size = 1, color = "#ff0000")
    var/shadow = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ff00ff")
    O = slot("image"); O.icon = null; picture(O, red)
    O = slot("image-shadow"); O.icon = null; picture(O, shadow)
    O = slot("image-turn"); O.icon = 'small.png'; O.transform = turned(90); picture(O, shadow)
    O = slot("image-scale"); O.icon = 'small.png'; O.transform = scaled(2, 1); picture(O, red)
    O = slot("image-overlay"); O.icon = null; I = picture(O, red); I.overlays += image('small.png', pixel_x = 14)
    O = slot("image-alpha"); O.icon = null; I = picture(O, red); I.alpha = 128
    O = slot("image-pixel"); O.icon = null; I = picture(O, red); I.pixel_x = 6; I.pixel_y = -4
    O = slot("maptext"); O.maptext = text; O.filters = red
    O = slot("maptext-only"); O.icon = null; O.maptext = text; O.filters = red
    O = slot("kt-maptext"); O.appearance_flags = KEEP_TOGETHER; O.maptext = text; O.overlays += image('small.png', pixel_x = 14)
    O = slot("kt-maptext-outline"); O.appearance_flags = KEEP_TOGETHER; O.maptext = text; O.overlays += image('small.png', pixel_x = 14); O.filters = red
    O = slot("maptext-overlay"); O.maptext = text; O.overlays += image('small.png', pixel_x = 14); O.filters = red
    O = slot("maptext-shadow"); O.maptext = text; O.filters = shadow
    O = slot("kt-maptext-turn"); O.appearance_flags = KEEP_TOGETHER; O.maptext = text; O.transform = turned(90)
    O = slot("maptext-turn-outline"); O.maptext = text; O.filters = red; O.transform = turned(90)
/proc/set_b()
    var/obj/thing/O
    var/red = filter(type = "outline", size = 1, color = "#ff0000")
    // A clock: pixel_x runs 0..320 over the 20 s animations, so grabs pair by its reading.
    var/obj/clock/C = new(locate(1, 1, 1))
    animate(C, pixel_x = 320, time = 200)
    O = slot("anim-pixel"); O.filters = red; animate(O, pixel_y = 16, time = 200)
    O = slot("anim-alpha"); O.filters = red; animate(O, alpha = 0, time = 200)
    O = slot("anim-turn"); O.filters = red; animate(O, transform = turned(90), time = 200)
    O = slot("anim-shadow-turn"); O.filters = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ff00ff"); animate(O, transform = turned(90), time = 200)
    O = slot("anim-color"); O.filters = red; animate(O, color = "#0000ff", time = 200)
    O = slot("flick"); O.filters = red; O.icon = 'anim.dmi'; O.icon_state = "still"
    spawn(5) blinker(O)
    O = slot("anim-filter-param"); O.filters = filter(type = "outline", size = 1, color = "#ff0000"); animate(O.filters[1], size = 4, time = 200)
    O = slot("still"); O.filters = red
/proc/blinker(obj/O)
    while(1)
        flick("blink", O)
        sleep(40)
/proc/set_c()
    var/obj/thing/O
    O = slot("wave"); O.filters = filter(type = "wave", x = 2, size = 2)
    O = slot("wave-y"); O.filters = filter(type = "wave", x = 0, y = 4, size = 3, offset = 0.5)
    O = slot("wave-flags"); O.filters = filter(type = "wave", x = 3, y = 1, size = 1, flags = WAVE_SIDEWAYS | WAVE_BOUNDED)
    O = slot("ripple"); O.filters = filter(type = "ripple", size = 3, radius = 8, falloff = 1, repeat = 2)
    O = slot("ripple-xy"); O.filters = filter(type = "ripple", x = 4, y = -2, size = 2, radius = 12, falloff = 0.5, repeat = 1)
    O = slot("bloom"); O.filters = filter(type = "bloom", threshold = rgb(128, 128, 128), size = 3, offset = 1, alpha = 255)
    O = slot("bloom-soft"); O.filters = filter(type = "bloom", threshold = "#000000", size = 2, offset = 0, alpha = 128)
    O = slot("wave-shadow"); O.filters = list(filter(type = "wave", x = 2, size = 2), filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff00ff"))
    O = slot("plain")
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
    if(params["set"] == "b") set_b()
    else if(params["set"] == "c") set_c()
    else set_a()
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9,6,1)
        client.images += pictures
        world.log << "READY [key]"

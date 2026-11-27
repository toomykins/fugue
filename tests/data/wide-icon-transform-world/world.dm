// Transformed appearance trees and images in a 64 px icon world (Fugue refused both outside 32 px worlds). Cases sit on
// a 3-tile grid of 64 px tiles; run with -params "set=a" (default).
world
    name = "Wide icon transform probe"
    icon_size = 64
    maxx = 9
    maxy = 6
    maxz = 1
    view = "9x6"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
var/list/pictures = list()
/obj/thing
    icon = 'shape.png'
    layer = 5
/proc/slot(label)
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 3) * 3, 5 - round(i / 3) * 3, 1))
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
/proc/set_a()
    var/obj/thing/O
    var/image/I
    O = slot("turn-overlay"); O.overlays += image('small.png', pixel_x = 28); O.transform = turned(90)
    O = slot("scale-overlay"); O.overlays += image('small.png', pixel_x = 28); O.transform = scaled(0.5, 1)
    O = slot("image-turn"); O.transform = turned(90); I = image('small.png', O); I.pixel_x = 28; I.layer = 6; pictures += I
    O = slot("image-own-turn"); I = image('shape.png', O); I.transform = turned(90); I.layer = 6; pictures += I; O.icon = null
    O = slot("turn-maptext"); O.maptext = "<span style='color:#ffffff'>Ab</span>"; O.transform = turned(90)
    O = slot("plain-turn"); O.transform = turned(90)
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(9,6,1))) t.icon = 'floor.png'
    set_a()
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(5,3,1)
        client.images += pictures
        world.log << "READY [key]"

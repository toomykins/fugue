// Colour-matrix tweens: grey boxes animate their colour over the same 20 s loop as a map clock marker (bottom row).
// Cases on a 3-tile grid: identity to a matrix, a matrix to another, a plain colour to a matrix, a matrix to a plain
// colour, a matrix tween with SINE_EASING, and a static matrix set over a plain colour. Animation starts 3 s after the first login.
world
    name = "Color tween probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/obj/clock
var/list/boxes = list()
/obj/box
    icon = 'grey.png'
    layer = 5
/obj/mark
    icon = 'mark.png'
    layer = 5
/proc/box(i, label)
    var/obj/box/B = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    boxes += B
    world.log << "CASE [label]"
    return B
var/list/M1 = list(2,0,0,0, 0,0.5,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
var/list/M2 = list(0.5,0,0,0, 0,1,0,0, 0,0,2,0, 0,0,0,1, 0.2,0,0,0)
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    clock = new /obj/mark(locate(2,1,1))
    box(0, "id-to-m1")
    var/obj/box/B = box(1, "m1-to-m2"); B.color = M1
    B = box(2, "plain-to-m1"); B.color = "#ff8040"
    B = box(3, "m1-to-plain"); B.color = M1
    box(4, "id-to-m1-sine")
    B = box(5, "static-plain-then-m1"); B.color = "#ff8040"; B.color = M1
var/started = 0
/proc/start()
    animate(boxes[1], color = M1, time = 200, loop = -1); animate(color = null, time = 0.5)
    animate(boxes[2], color = M2, time = 200, loop = -1); animate(color = M1, time = 0.5)
    animate(boxes[3], color = M1, time = 200, loop = -1); animate(color = "#ff8040", time = 0.5)
    animate(boxes[4], color = "#ff8040", time = 200, loop = -1); animate(color = M1, time = 0.5)
    animate(boxes[5], color = M1, time = 200, loop = -1, easing = SINE_EASING); animate(color = null, time = 0.5)
    animate(clock, pixel_x = 320, time = 200, loop = -1); animate(pixel_x = 0, time = 0.5)
    world.log << "START [world.time]"
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        world.log << "READY [key]"
        if(!started)
            started = 1
            spawn(30) start()

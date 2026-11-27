// animate() flags Fugue refused: ANIMATION_SLICE, ANIMATION_CONTINUE and ANIMATION_END_LOOP, plus a parallel colour
// step. Each case starts 1 s after the previous one once a client logs in, so the animate packets arrive in CASE order.
// A clock marker on (1,1) runs pixel_x 0..320 over 32 s from the first case. Cases sit on a 3-tile grid.
world
    name = "Anim flags probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 20
    mob = /mob/observer
var/list/cases = list()
var/started = 0
/obj/thing
    icon = 'small.png'
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
/proc/run_cases()
    var/obj/thing/O
    var/obj/clock/C = new(locate(1, 1, 1))
    animate(C, pixel_x = 320, time = 320)
    world.log << "START [world.time]"
    // A 4 s back-and-forth loop, then a slice of it: 1 s long starting 1 s in, looping forever.
    O = slot("slice")
    animate(O, pixel_x = 32, time = 20, loop = -1)
    animate(pixel_x = 0, time = 20)
    sleep(10)
    animate(O, delay = 10, time = 10, loop = -1, flags = ANIMATION_SLICE)
    // A slice without a delay: the first 2 s of the loop, played once.
    O = slot("slice-once")
    animate(O, pixel_x = 32, time = 20, loop = -1)
    animate(pixel_x = 0, time = 20)
    sleep(10)
    animate(O, time = 20, loop = 1, flags = ANIMATION_SLICE)
    // A 3 s move east, then 1 s in a move north that continues after it rather than replacing it.
    O = slot("continue")
    animate(O, pixel_x = 32, time = 30)
    sleep(10)
    animate(O, pixel_y = 32, time = 30, flags = ANIMATION_CONTINUE)
    // The same without the flag, for reference: the second call replaces the first.
    O = slot("replace")
    animate(O, pixel_x = 32, time = 30)
    sleep(10)
    animate(O, pixel_y = 32, time = 30)
    // A looping 4 s back-and-forth, told 1 s in to finish its current loop.
    O = slot("end-loop")
    animate(O, pixel_x = 32, time = 20, loop = -1)
    animate(pixel_x = 0, time = 20)
    sleep(10)
    animate(O, flags = ANIMATION_END_LOOP)
    // A parallel colour step over a pixel move: 20 s each.
    O = slot("parallel-color")
    animate(O, pixel_x = 16, time = 200)
    animate(color = "#00ff00", time = 200, flags = ANIMATION_PARALLEL)
    O = slot("parallel-color-base")
    O.color = "#ff0000"
    animate(O, pixel_x = 16, time = 200)
    animate(color = "#0000ff", time = 200, flags = ANIMATION_PARALLEL)
    O = slot("plain")
    world.log << "DONE [world.time]"
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9,6,1)
        world.log << "READY [key]"
        if(!started)
            started = 1
            spawn(30) run_cases()

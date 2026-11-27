// Parallel steps and followers fixture. Arms (as in transform-tween-world) on a 3-tile grid animate over a 20 s
// loop: parallel transform/offset steps, an image and a vis copy following an animating holder, and plain offset
// tweens to read how stock rounds offsets. The bottom-row marker is the clock. Animation starts 3 s after the
// first login, so both clients see it.
world
    name = "Parallel probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/list/cases = list()
var/list/pictures = list()
var/obj/clock
/obj/arm
    icon = 'arm.png'
    layer = 5
/obj/holder
    icon = 'dot.png'
    layer = 5
/obj/mark
    icon = 'mark.png'
    layer = 5
/obj/fixed
    icon = 'fixed.png'
    layer = 4
/proc/T(a)
    var/matrix/M = matrix()
    M.Turn(a)
    return M
/proc/at(i, type=/obj/arm)
    var/obj/O = new type(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    return O
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    clock = new /obj/mark(locate(2,1,1))
    new /obj/fixed(locate(2,1,1))
    new /obj/fixed(locate(12,1,1))
/proc/log_case(n) world.log << "CASE [n]"
/proc/start()
    var/obj/O
    // 0: turn with a parallel 2x scale
    O = at(0); log_case("par-turn+scale")
    animate(O, transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(O, transform = matrix(2,0,0,0,2,0), time = 200, loop = -1, flags = ANIMATION_PARALLEL); animate(transform = matrix(), time = 0.5)
    // 1: turn with a parallel pixel_x move
    O = at(1); log_case("par-turn+move")
    animate(O, transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(O, pixel_x = 24, time = 200, loop = -1, flags = ANIMATION_PARALLEL); animate(pixel_x = 0, time = 0.5)
    // 2: two parallel quarter turns
    O = at(2); log_case("par-turn+turn")
    animate(O, transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(O, transform = T(90), time = 200, loop = -1, flags = ANIMATION_PARALLEL); animate(transform = matrix(), time = 0.5)
    // 3: turn with a parallel translation matrix
    O = at(3); log_case("par-turn+translate")
    animate(O, transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(O, transform = matrix(1,0,24,0,1,0), time = 200, loop = -1, flags = ANIMATION_PARALLEL); animate(transform = matrix(), time = 0.5)
    // 4: an image on a target whose offset and transform animate (target icon is a corner dot)
    O = at(4, /obj/holder); log_case("img-follow")
    var/image/I = image('arm.png', O); I.layer = 6; pictures += I
    for(var/client/C) C.images += I
    animate(O, pixel_x = 24, transform = T(90), time = 200, loop = -1); animate(pixel_x = 0, transform = matrix(), time = 0.5)
    // 5: a vis copy on a holder whose offset animates
    O = at(5, /obj/holder); log_case("vis-follow")
    var/obj/arm/V = new(null); O.vis_contents += V
    animate(O, pixel_x = 24, pixel_y = 12, time = 200, loop = -1); animate(pixel_x = 0, pixel_y = 0, time = 0.5)
    // 6: offset animation of an obj that has a static transform
    O = at(6); log_case("move-turned"); O.transform = T(90)
    animate(O, pixel_x = 24, time = 200, loop = -1); animate(pixel_x = 0, time = 0.5)
    // 7: a 2-step turn with a parallel scale started mid-way (delay)
    O = at(7); log_case("par-delayed")
    animate(O, transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(O, transform = matrix(1.5,0,0,0,1.5,0), time = 100, delay = 50, flags = ANIMATION_PARALLEL)
    O = at(8); log_case("neg-move")
    animate(O, pixel_x = -24, pixel_y = -24, time = 200, loop = -1); animate(pixel_x = 0, pixel_y = 0, time = 0.5)
    O = at(9); log_case("pos-move-y")
    animate(O, pixel_y = 24, time = 200, loop = -1); animate(pixel_y = 0, time = 0.5)
    O = at(10); log_case("m12-to-12"); O.pixel_x = -12
    animate(O, pixel_x = 12, time = 200, loop = -1); animate(pixel_x = -12, time = 0.5)
    O = at(11); log_case("12-to-m12"); O.pixel_x = 12
    animate(O, pixel_x = -12, time = 200, loop = -1); animate(pixel_x = 12, time = 0.5)
    animate(clock, pixel_x = 320, time = 200, loop = -1)
    animate(pixel_x = 0, time = 0.5)
    world.log << "START [world.time]"
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        client.images += pictures
        world.log << "READY [key]"
        if(!length(cases))
            cases += 1
            spawn(30) start()

// HUD animation fixture: screen objects animate over the same 20 s loop as a map clock marker (bottom row), so each
// grab pairs HUD and map positions at one instant. Rows from the top: a HUD marker sliding like the clock, a HUD arm
// turning, a HUD marker fading, a HUD marker sliding with a parallel turn; on row 2 a HUD marker with a static
// pixel_x, one whose pixel_y slides and one whose 1 s pixel_x tween has ended. Animation starts 3 s after the first login.
world
    name = "HUD anim probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/obj/clock
var/list/huds = list()
/obj/mark
    icon = 'mark.png'
    layer = 5
/obj/fixed
    icon = 'fixed.png'
    layer = 4
/obj/screen
    layer = 20
    plane = 1
/proc/hud(icon, loc, label)
    var/obj/screen/S = new()
    S.icon = icon; S.screen_loc = loc; S.name = label
    huds += S
    world.log << "CASE [label]"
    return S
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    clock = new /obj/mark(locate(2,1,1))
    new /obj/fixed(locate(2,1,1))
    new /obj/fixed(locate(12,1,1))
    hud('mark.png', "2,10", "slide")
    hud('arm.png', "2,8", "turn")
    hud('mark.png', "2,6", "fade")
    hud('arm.png', "2,4", "slide+turn")
    var/obj/screen/S = hud('mark.png', "2,2", "static-px"); S.pixel_x = 16
    S = hud('mark.png', "6,2", "slide-y"); S.pixel_y = 0
    hud('mark.png', "10,2", "settled-px")
/proc/T(a)
    var/matrix/M = matrix()
    M.Turn(a)
    return M
/proc/start()
    animate(huds[1], pixel_x = 320, time = 200, loop = -1); animate(pixel_x = 0, time = 0.5)
    animate(huds[2], transform = T(90), time = 200, loop = -1); animate(transform = matrix(), time = 0.5)
    animate(huds[3], alpha = 0, time = 200, loop = -1); animate(alpha = 255, time = 0.5)
    animate(huds[4], pixel_x = 320, time = 200, loop = -1); animate(pixel_x = 0, time = 0.5)
    animate(huds[4], transform = T(90), time = 200, loop = -1, flags = ANIMATION_PARALLEL); animate(transform = matrix(), time = 0.5)
    animate(huds[6], pixel_y = 64, time = 200, loop = -1); animate(pixel_y = 0, time = 0.5)
    animate(huds[7], pixel_x = 16, time = 10)
    animate(clock, pixel_x = 320, time = 200, loop = -1); animate(pixel_x = 0, time = 0.5)
    world.log << "START [world.time]"
var/started = 0
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        client.screen += huds
        world.log << "READY [key]"
        if(!started)
            started = 1
            spawn(30) start()

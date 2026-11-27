// Transform tween fixture. Each case is an arm (red east 16 px, green north 12 px, blue west dot) whose transform
// is animated from its start matrix to a target over 20 s (two-step cases: 10 s per step), then snapped back, looping.
// A white marker on the bottom row runs linearly over the same 20 s and is the clock; red markers fix its ends.
// Run with -params "set=a" (default) or "set=b" for the second set of cases.
world
    name = "Transform tween probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/list/cases = list()
var/obj/clock
/obj/arm
    icon = 'arm.png'
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
// flags/M2/flags2: the first step's flags and an optional second step; M0: the start matrix.
/proc/add(label, matrix/M, flags=0, matrix/M2=null, flags2=0, matrix/M0=null)
    var/i = length(cases)
    var/obj/arm/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    if(M0) O.transform = M0
    cases[O] = list(M, flags, M2, flags2, O.transform)
/proc/set_a()
    add("turn90", T(90))
    add("turn180", T(180))
    add("turn270", T(270))
    add("turn-90", T(-90))
    add("turn90scale2", T(90) * 2)
    add("turn90linear", T(90), ANIMATION_LINEAR_TRANSFORM)
    add("mirror", matrix(-1,0,0,0,1,0))
    add("turn135", T(135))
    add("turn180linear", T(180), ANIMATION_LINEAR_TRANSFORM)
    add("translate+turn90", T(90) * matrix(1,0,32,0,1,0))
    add("shear", matrix(1,0.5,0,0,1,0))
    add("exact-1", matrix(-1,0,0,0,-1,0))
    add("half-turn180", matrix(-0.5,0,0,0,-0.5,0))
    add("lin90-then180", T(90), ANIMATION_LINEAR_TRANSFORM, T(180), 0)
    add("90-then-lin180", T(90), 0, T(180), ANIMATION_LINEAR_TRANSFORM)
    add("flipy", matrix(1,0,0,0,-1,0))
    add("scale2x", matrix(2,0,0,0,1,0))
    add("turn45scale", T(45) * matrix(2,0,0,0,0.5,0))
/proc/set_b()
    add("turn179", T(179))
    add("turn181", T(181))
    add("turn-179", T(-179))
    add("swap", matrix(0,1,0,1,0,0))
    add("90to270", T(270), 0, null, 0, T(90))
    add("mirror2", matrix(-2,0,0,0,1,0))
    add("turn180scale", matrix(-2,0,0,0,-1,0))
    add("near180d", matrix(-1,0,0,0.05,-1,0))
    add("near180b", matrix(-1,0.05,0,0,-1,0))
    add("mirror90", matrix(0,-1,0,-1,0,0))
    add("45to225", T(225), 0, null, 0, T(45))
    add("halfmirror", matrix(-0.5,0,0,0,1,0))
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    if(params["set"] == "b") set_b()
    else set_a()
    clock = new /obj/mark(locate(2,1,1))
    new /obj/fixed(locate(2,1,1))
    new /obj/fixed(locate(12,1,1))
    for(var/obj/arm/O in cases)
        var/list/c = cases[O]
        var/matrix/M = c[1]
        var/matrix/M2 = c[3]
        var/matrix/S = c[5]
        world.log << "CASE [O.name] [M.a] [M.b] [M.c] [M.d] [M.e] [M.f] [c[2]] from [S.a] [S.b] [S.c] [S.d] [S.e] [S.f]\
[M2 ? " then [M2.a] [M2.b] [M2.c] [M2.d] [M2.e] [M2.f] [c[4]]" : ""]"
/proc/start()
    for(var/obj/arm/O in cases)
        var/list/c = cases[O]
        if(c[3])
            animate(O, transform = c[1], time = 100, loop = -1, flags = c[2])
            animate(transform = c[3], time = 100, flags = c[4])
        else
            animate(O, transform = c[1], time = 200, loop = -1, flags = c[2])
        animate(transform = c[5], time = 0.5)
    animate(clock, pixel_x = 320, time = 200, loop = -1)
    animate(pixel_x = 0, time = 0.5)
    world.log << "START [world.time]"
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        world.log << "READY [key]"
        spawn(20) start()

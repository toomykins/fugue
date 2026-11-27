// Wave, ripple and bloom filters (set a) and their neighbours in a chain (set b). Cases sit on a 3-tile grid; run
// with -params "set=a|b|c". grid.png is a 2 px checker, shape.png the filter fixtures' box with a white corner.
world
    name = "Filter types probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
/obj/thing
    icon = 'grid.png'
    layer = 5
/proc/slot(label, f, ico = 'grid.png')
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    O.icon = ico
    O.filters = f
    cases += O
    world.log << "CASE [label]"
    return O
/proc/set_a()
    slot("wave", filter(type = "wave", x = 2, size = 2))
    slot("wave-y", filter(type = "wave", x = 0, y = 4, size = 3, offset = 0.5))
    slot("wave-diag", filter(type = "wave", x = 8, y = 8, size = 1.5, offset = 0.25))
    slot("wave-side", filter(type = "wave", x = 5, size = 2, flags = WAVE_SIDEWAYS))
    slot("wave-bound", filter(type = "wave", x = -6, y = 2, size = 3, flags = WAVE_BOUNDED))
    slot("wave-flags", filter(type = "wave", x = 3, y = 1, size = 1, flags = WAVE_SIDEWAYS | WAVE_BOUNDED))
    slot("wave-two", list(filter(type = "wave", x = 4, size = 1), filter(type = "wave", y = 6, size = 2, offset = 0.3)))
    slot("wave-split", list(filter(type = "wave", x = 4, size = 1), filter(type = "wave", y = 6, size = 2, flags = WAVE_BOUNDED)))
    slot("wave-big", filter(type = "wave", x = 10, size = 40))
    slot("ripple", filter(type = "ripple", size = 3, radius = 8, falloff = 1, repeat = 2))
    slot("ripple-xy", filter(type = "ripple", x = 4, y = -2, size = 2, radius = 12, falloff = 0.5, repeat = 1))
    slot("ripple-bound", filter(type = "ripple", size = 4, radius = 10, falloff = 1, repeat = 3, flags = WAVE_BOUNDED))
    slot("ripple-two", list(filter(type = "ripple", x = -6, size = 2, radius = 6, repeat = 2), filter(type = "ripple", x = 6, y = 4, size = 3, radius = 9, repeat = 3)))
    slot("ripple-sign", filter(type = "ripple", size = 1.7, repeat = 1.3, radius = 0.6, falloff = 2))
    slot("bloom", filter(type = "bloom", threshold = rgb(128, 128, 128), size = 3, offset = 1, alpha = 255), 'shape.png')
    slot("bloom-soft", filter(type = "bloom", threshold = "#000000", size = 2, offset = 0, alpha = 128), 'shape.png')
    slot("bloom-orb", filter(type = "bloom", threshold = rgb(221, 221, 221), size = 2, offset = 2, alpha = 255), 'shape.png')
    slot("bloom-spread", filter(type = "bloom", threshold = "#000000", size = 0, offset = 2, alpha = 255), 'shape.png')
    slot("bloom-wide", filter(type = "bloom", threshold = "#000000", size = 4, offset = 0, alpha = 255), 'shape.png')
    slot("bloom-grid", filter(type = "bloom", threshold = rgb(100, 100, 100), size = 1, offset = 1, alpha = 200))
    slot("plain", null)
/proc/set_b()
    slot("wave-shadow", list(filter(type = "wave", x = 2, size = 2), filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff00ff")))
    slot("shadow-wave", list(filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff00ff"), filter(type = "wave", x = 2, size = 2)))
    slot("outline-ripple", list(filter(type = "outline", size = 1, color = "#ff0000"), filter(type = "ripple", size = 3, radius = 8, falloff = 1, repeat = 2)))
    slot("bloom-outline", list(filter(type = "bloom", threshold = rgb(128, 128, 128), size = 3, offset = 1, alpha = 255), filter(type = "outline", size = 1, color = "#ff0000")), 'shape.png')
    slot("blur-bloom", list(filter(type = "blur", size = 1), filter(type = "bloom", threshold = rgb(128, 128, 128), size = 2, offset = 1, alpha = 255)), 'shape.png')
    slot("wave-ripple", list(filter(type = "wave", x = 4, size = 1), filter(type = "ripple", size = 3, radius = 8, repeat = 2)))
    slot("plain", null)
/proc/set_c()
    // vg's bar sign: seven waves with animated phase and a ripple, on a vis_contents member; a static copy beside.
    var/obj/thing/H = slot("sign-holder", null)
    H.icon = null
    var/obj/thing/V = new
    V.icon = 'grid.png'
    H.vis_contents += V
    var/list/w = list(list(12, -9, 1.5, 0.2), list(-15, 4, 2, 0.6), list(20, 18, 0.8, 0.1), list(-11, -14, 2.5, 0.9), list(9, 25, 1, 0.4), list(-22, 9, 1.8, 0.3), list(14, -12, 0.6, 0.7))
    for(var/list/p in w)
        V.filters += filter(type = "wave", x = p[1], y = p[2], size = p[3], offset = p[4])
    for(var/i = 1 to 7)
        var/f = V.filters[i]
        animate(f, offset = f:offset, time = 0, loop = -1, flags = ANIMATION_PARALLEL)
        animate(offset = f:offset - 1, time = 40)
    var/obj/thing/S = slot("sign-static", null)
    for(var/list/p in w)
        S.filters += filter(type = "wave", x = p[1], y = p[2], size = p[3], offset = p[4])
    var/obj/thing/A = slot("sign-anim", null)
    for(var/list/p in w)
        A.filters += filter(type = "wave", x = p[1], y = p[2], size = p[3], offset = p[4])
    for(var/i = 1 to 7)
        var/f = A.filters[i]
        animate(f, offset = f:offset, time = 0, loop = -1, flags = ANIMATION_PARALLEL)
        animate(offset = f:offset - 1, time = 40)
    var/obj/thing/R = slot("vis-ripple", null)
    R.icon = null
    var/obj/thing/RV = new
    RV.icon = 'grid.png'
    RV.filters = filter(type = "ripple", size = 2, repeat = 1.5, radius = 0.8, falloff = 1.2)
    R.vis_contents += RV
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
        world.log << "READY [key]"

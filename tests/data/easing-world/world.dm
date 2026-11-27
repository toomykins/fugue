// Easing and playback-rate fixture. One white marker per curve x direction is animated pixel_x 0 -> 320 over
// 10 s (100 ds), then snaps back, looping. Red markers sit at both ends of each row. Row 0 (linear) is the clock.
// The playback-rate check also ran this world with fps = 40.
world
    name = "Easing probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/list/movers = list()
/turf/floor
    icon = 'floor.png'
/obj/mark
    icon = 'mark.png'
    layer = 5
/obj/fixed
    icon = 'fixed.png'
    layer = 4
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    // row 0 (y=12, slot 0) is the linear reference.
    var/list/modes = list(0, 64, 128, 192)
    var/row = 0
    for(var/type in 0 to 8)
        for(var/m in modes)
            if(type == 0 && m) continue
            var/ty = 12 - round(row / 4)
            var/slot = row % 4
            var/obj/mark/O = new(locate(3, ty, 1))
            O.pixel_y = slot * 8
            O.name = "[type]:[m]"
            movers += O
            var/obj/fixed/A = new(locate(3, ty, 1))
            A.pixel_y = slot * 8
            var/obj/fixed/B = new(locate(13, ty, 1))
            B.pixel_y = slot * 8
            row++
    world.log << "ROWS [row]"
/proc/start()
    for(var/obj/mark/O in movers)
        var/list/p = splittext(O.name, ":")
        var/e = text2num(p[1]) | text2num(p[2])
        animate(O, pixel_x = 320, time = 100, loop = -1, easing = e)
        animate(pixel_x = 0, time = 0.5)
    world.log << "START [world.time]"
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        world.log << "READY [key]"
        spawn(20) start()

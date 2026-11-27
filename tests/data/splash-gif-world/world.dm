// A lobby splash as a game shows one: a turf whose icon is an animated GIF read from disk at run time with file(),
// 480x480 on a 32-pixel map, its bottom-left turf at 1,1 so that the icon covers the whole 15x15 view.
world
    name = "Splash GIF fixture"
    maxx = 15
    maxy = 15
    maxz = 1
    view = 7
    fps = 10
    turf = /turf/floor
    mob = /mob/viewer
turf/floor
    name = "floor"
turf/splash
    name = "splash"
    icon = null
    layer = 2.1
turf/splash/New()
    ..()
    icon = file("splash.gif")
world/New()
    ..()
    world.log = file("events.log")
    new /turf/splash(locate(1,1,1))
mob/viewer/Login()
    ..()
    loc = locate(8,8,1)
    world.log << "READY [key]"

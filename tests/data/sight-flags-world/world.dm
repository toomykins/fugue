// Sight flags Fugue marked unsupported: BLIND, SEE_INFRA and SEE_PIXELS, against plain sight. The scene has lit and
// unlit floor, an opaque wall with objects behind it, an object with infra_luminosity in the dark and one with a pixel
// offset that pokes past the wall. Run with -params "set=plain|blind|infra|pixels|infra-pixels".
world
    name = "Sight flags probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
/turf/floor
    icon = 'floor.png'
/turf/wall
    icon = 'wall.png'
    opacity = 1
/area/lit
    luminosity = 1
/area/dark
    luminosity = 0
/obj/thing
    icon = 'small.png'
    layer = 5
/mob/observer
    icon = 'small.png'
    layer = 6
world/New()
    ..()
    world.log = file("events.log")
    var/area/lit/L = new
    var/area/dark/D = new
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1)))
        if(t.x == 12 && t.y >= 3 && t.y <= 10) new /turf/wall(t)
        else new /turf/floor(t)
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1)))
        if(t.x <= 6) D.contents += t
        else L.contents += t
    var/obj/thing/O
    // Objects in the light, behind the wall, and in the dark.
    O = new(locate(9, 6, 1)); O.name = "lit"
    O = new(locate(14, 6, 1)); O.name = "behind-wall"
    O = new(locate(14, 8, 1)); O.name = "behind-wall-pixel"; O.pixel_x = -24
    O = new(locate(3, 6, 1)); O.name = "dark"
    O = new(locate(3, 8, 1)); O.name = "dark-infra"; O.infra_luminosity = 3
    O = new(locate(4, 4, 1)); O.name = "dark-pixel"; O.pixel_x = 40
/mob/observer/Login()
    ..()
    loc = locate(9, 6, 1)
    var/s = world.params["set"]
    if(s == "blind") sight |= BLIND
    else if(s == "infra") sight |= SEE_INFRA
    else if(s == "pixels") sight |= SEE_PIXELS
    else if(s == "infra-pixels") sight |= SEE_INFRA | SEE_PIXELS
    see_in_dark = 0
    world.log << "READY [s] [sight]"

world
    name = "Screen location TOP fixture"
    hub = ""
    view = "7x5"
    maxx = 15
    maxy = 15
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'black.png'

/obj/marker
    icon = 'green.png'
    screen_loc = "CENTER"

/client/proc/phase(obj/marker/marker, placement)
    marker.screen_loc = placement
    sleep(10)
    world.log << "PHASE [placement]"
    sleep(20)

/client/New()
    ..()
    mob.loc = locate(8, 8, 1)
    spawn(5)
        var/obj/marker/marker = new
        screen += marker
        phase(marker, "NORTH,CENTER")
        phase(marker, "TOP,CENTER")
        phase(marker, "NORTH:-70,CENTER:-54")
        phase(marker, "TOP:-70,CENTER:-54")
        phase(marker, "CENTER:-61,NORTH")
        phase(marker, "CENTER:-61,TOP")
        phase(marker, "NORTH-1:-8,CENTER:+29")
        phase(marker, "TOP-1:-8,CENTER:+29")
        phase(marker, "WEST,NORTH TO EAST,NORTH")
        phase(marker, "WEST,TOP TO EAST,TOP")
        world.log << "SCREEN_LOC_TOP_DONE"

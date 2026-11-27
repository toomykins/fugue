world
    name = "Screen location alias fixture"
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

/client/New()
    ..()
    mob.loc = locate(8, 8, 1)
    spawn(5)
        var/obj/marker/marker = new
        screen += marker
        var/list/pairs = json_decode(file2text('pairs.json'))
        for(var/i in 1 to pairs.len)
            var/list/pair = pairs[i]
            for(var/j in 1 to 2)
                marker.screen_loc = pair[j]
                sleep(8)
                world.log << "PHASE [i]-[j]"
                sleep(12)
        world.log << "SCREEN_LOC_ALIAS_DONE"

world
    name = "Screen range differential"
    hub = ""
    fps = 20
    view = "7x5"
    maxx = 101
    maxy = 101
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'blue.png'

/obj/marker
    icon = 'marker.png'

/client/New()
    ..()
    mob.loc = locate(51, 51, 1)
    spawn(5)
        var/obj/marker/marker = new
        screen += marker
        var/list/cases = json_decode(file2text('cases.json'))
        for(var/i in 1 to cases.len)
            var/list/test = cases[i]
            view = test["view"]
            marker.screen_loc = test["text"]
            marker.pixel_x = isnum(test["pixelX"]) ? test["pixelX"] : 0
            marker.pixel_y = isnum(test["pixelY"]) ? test["pixelY"] : 0
            sleep(6)
            world.log << "READY [i] [marker.screen_loc]"
            var/deadline = world.time + 300
            while(text2num(file2text("ack.txt")) != i && world.time < deadline)
                sleep(1)
            if(world.time >= deadline)
                world.log << "TIMEOUT [i]"
                return
        world.log << "RANGE_DONE"

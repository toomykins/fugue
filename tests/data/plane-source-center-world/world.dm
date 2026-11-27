world
    name = "Plane source CENTER fixture"
    hub = ""
    view = "5x3"
    maxx = 11
    maxy = 11
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'black.png'

/obj/marker
    icon = 'magenta.png'
    screen_loc = "CENTER"

/obj/master
    appearance_flags = PLANE_MASTER
    screen_loc = "CENTER"

/client/proc/phase(name)
    sleep(20)
    world.log << "PHASE [name]"
    sleep(30)

/client/New()
    ..()
    mob.loc = locate(6, 6, 1)
    spawn(5)
        screen += new /obj/marker
        phase("baseline")
        var/obj/marker/green = new
        green.icon = 'green.png'
        green.plane = 60
        screen += green
        var/obj/master/producer = new
        producer.plane = 60
        producer.render_target = "*active"
        screen += producer
        var/obj/consumer = new
        consumer.plane = 70
        consumer.screen_loc = "SOUTHWEST"
        consumer.render_source = "*active"
        screen += consumer
        phase("southwest-source")
        consumer.screen_loc = "1,1"
        phase("numeric-source")
        consumer.appearance_flags = PASS_MOUSE | NO_CLIENT_COLOR | KEEP_TOGETHER
        phase("numeric-relay-flags")
        consumer.screen_loc = "CENTER"
        phase("center-source")
        screen -= consumer
        phase("removed-consumer")
        world.log << "PLANE_SOURCE_PROBE_DONE"

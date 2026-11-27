world
    name = "Stock BYOND Large Blur Measurement"
    view = "31x21"
    maxx = 31
    maxy = 21
    fps = 20
    turf = /turf/black
    mob = /mob/player
world/New()
    ..()
    log = file("events.log")
    log << "READY [byond_version].[byond_build] port=[port]"
turf/black
    icon = 'black.png'
    plane = -10
obj/pattern
    icon = 'square.png'
    screen_loc = "1,1"
    plane = 1
    mouse_opacity = 0
obj/marker
    icon = 'markers.png'
    screen_loc = "1,1"
    plane = 10
    mouse_opacity = 0
obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    screen_loc = "CENTER"
    mouse_opacity = 0
mob/player
    invisibility = 101
    var/obj/master/master
    var/obj/pattern/pattern
mob/player/Login()
    ..()
    loc = locate(16,11,1)
    client.show_popup_menus = 0
    pattern = new
    master = new
    client.screen += pattern
    client.screen += master
    client.screen += new /obj/marker
    world.log << "LOGIN [key]"
    spawn() watch_phase()
mob/player/proc/watch_phase()
    var/last = ""
    while(client)
        var/raw = file2text("phase.txt")
        if(raw && raw != last)
            last = raw
            var/list/p = splittext(raw,"|")
            var/token = p[1]
            var/shape = p[2]
            var/size = text2num(p[3])
            master.filters = null
            if(shape == "edge")
                pattern.icon = 'edge.png'
            else
                pattern.icon = 'square.png'
            master.filters = filter(type="blur",size=size)
            winset(src,"main","title=\"Stock BYOND Large Blur Measurement - [shape] size [size]\"")
            world.log << "PHASE [token] [shape] requested=[size] stored=[master.filters[1]:size]"
        sleep(1)

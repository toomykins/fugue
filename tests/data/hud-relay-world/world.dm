world
    name = "HUD Relay Fixture"
    hub = ""
    view = "7x5"
    maxx = 7
    maxy = 5
    maxz = 1
    turf = /turf/floor

/world/New()
    ..()
    log = file("events.log")

/turf/floor
    icon = 'floor.png'

/atom/proc/receipt(event, location, control, params)
    world.log << "EVENT [json_encode(list("event"=event,"name"=name,"location"="[location]","control"=control,"params"=params))]"

/atom/Click(location, control, params)
    receipt("Click", location, control, params)
/atom/MouseDown(location, control, params)
    receipt("Down", location, control, params)
/atom/MouseUp(location, control, params)
    receipt("Up", location, control, params)
/atom/DblClick(location, control, params)
    receipt("DoubleClick", location, control, params)
/atom/MouseWheel(dx, dy, location, control, params)
    receipt("Wheel", location, control, params)
/atom/MouseDrag(over, location, over_location, control, over_control, params)
    receipt("Drag", location, control, params)
/atom/MouseDrop(over, location, over_location, control, over_control, params)
    receipt("Drop", location, control, params)
/atom/MouseEntered(location, control, params)
    receipt("Enter", location, control, params)
/atom/MouseExited(location, control, params)
    receipt("Exit", location, control, params)

/obj/button
    name = "button"
    icon = 'small.png'
    screen_loc = "TOP:-13,CENTER:-20"
    plane = 10

/obj/master
    appearance_flags = PLANE_MASTER
    screen_loc = "CENTER"

/obj/relay
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    screen_loc = "1,1"

/client/var/phase_count = 0

/client/New()
    ..()
    mob.loc = locate(4,3,1)
    world.log << "LOGIN [key]"
    spawn(5)
        var/last
        while(src)
            var/request = file2text("phase.txt")
            if(request && request != last)
                last = request
                var/list/parts = splittext(request, ":")
                setup(parts[2])
                sleep(3)
                world.log << "READY [request]"
            sleep(1)

/client/proc/setup(phase)
    for(var/obj/O in screen)
        del(O)
    screen = list()
    var/obj/button/B = new
    screen += B
    if(findtext(phase,"large"))
        B.icon = 'large.png'
    if(findtext(phase,"north"))
        B.screen_loc = "NORTH:-65,CENTER:-20"
    if(findtext(phase,"right"))
        B.screen_loc = "RIGHT:-13,CENTER"
    if(findtext(phase,"corner"))
        B.screen_loc = "RIGHT:-13,TOP:-13"
    if(findtext(phase,"overlay"))
        var/image/I = image('large.png')
        I.pixel_x = -10
        I.pixel_y = 5
        B.overlays += I
    if(findtext(phase,"relay") || findtext(phase,"chain"))
        var/obj/master/M = new
        M.name = "source-master"
        M.plane = 10
        M.render_target = "*source"
        screen += M
        var/obj/relay/R = new
        R.name = "first-relay"
        R.render_source = "*source"
        R.plane = 20
        screen += R
        if(findtext(phase,"nopass"))
            R.appearance_flags &= ~PASS_MOUSE
        if(findtext(phase,"chain"))
            var/obj/master/N = new
            N.name = "middle-master"
            N.plane = 20
            N.render_target = "*middle"
            screen += N
            var/obj/relay/S = new
            S.name = "final-relay"
            S.render_source = "*middle"
            S.plane = 30
            screen += S
        if(findtext(phase,"mouse0"))
            B.mouse_opacity = 0
        if(findtext(phase,"mouse2"))
            B.mouse_opacity = 2
        if(findtext(phase,"master0"))
            M.mouse_opacity = 0
        if(findtext(phase,"relay0"))
            R.mouse_opacity = 0
    var/obj/badge = new
    badge.name = "badge"
    badge.icon = 'badge.png'
    badge.color = rgb(255, max(1,255 - (++phase_count)*10), 255)
    badge.screen_loc = "1,1"
    badge.plane = 100
    badge.mouse_opacity = 0
    screen += badge

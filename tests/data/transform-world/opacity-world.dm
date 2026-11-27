// prepare-opacity.py generates an asymmetric, multi-state/direction/frame DMI.
world
    name = "Whole icon opacity fixture"
    maxx = 9
    maxy = 9
    maxz = 1
    view = 4
var/list/tokens = list()
/obj/token
    icon = 'source.dmi'
    icon_state = "mark"
/proc/sample(label, x, y, factor)
    var/obj/token/O = new(locate(x,y,1))
    O.name = label
    if(!isnull(factor))
        var/icon/I = icon('source.dmi')
        I.MapColors(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,factor, 0,0,0,0)
        O.icon = I
        // Server-materialized RGBA oracle, separate from screenshot evidence.
        fcopy(I, "expected-[label].dmi")
    tokens += O
world/New()
    ..()
    world.log = file("events.log")
    sample("source",2,7,null)
    sample("half",5,7,0.5)
    sample("double",8,7,2)
    sample("zero",2,3,0)
    sample("opaque",5,3,10000)
    sample("identity",8,3,1)
/mob
    Login()
        ..()
        loc = locate(5,5,1)
        client.statpanel = "Opacity"
        world.log << "READY [key]"
    Stat()
        if(statpanel("Opacity"))
            for(var/obj/O in tokens)
                stat(null,O)
    verb/Facing()
        for(var/obj/O in tokens)
            O.dir = O.dir == SOUTH ? EAST : SOUTH
        world.log << "FACING"
    verb/Playback()
        for(var/obj/O in tokens)
            O.icon_state = O.icon_state == "mark" ? "pulse" : "mark"
        world.log << "PLAYBACK"

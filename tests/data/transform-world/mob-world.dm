// Use prepare.py in a separate runtime, then copy this file over its world.dm.
// Same asymmetric appearance in the object and mob registries; no game assets.
world
    name = "Static affine mob fixture"
    maxx = 9
    maxy = 18
    maxz = 1
    view = "9x18"
    fps = 20
var/list/tokens = list()
/obj/token
    icon = 'base.png'
/mob/token
    icon = 'base.png'
/proc/pair(label, y, matrix/M)
    var/obj/token/O = new(locate(3,y,1))
    O.name = "obj [label]"
    O.transform = M
    var/mob/token/B = new(locate(7,y,1))
    B.appearance = O.appearance
    B.name = "mob [label]"
    tokens += O
    tokens += B
world/New()
    ..()
    world.log = file("events.log")
    pair("identity",17,matrix())
    pair("lying rotation",14,matrix(0,1,0,-1,0,0))
    pair("translation",11,matrix(1,0,9,0,1,5))
    pair("reflection",8,matrix(-1,0,0,0,1,0))
    pair("shear",5,matrix(1,0.5,0,0,1,0))
    pair("positive scale",2,matrix(1.5,0,0,0,0.75,0))
/mob
    Login()
        ..()
        loc = locate(5,9,1)
        client.statpanel = "Transforms"
        world.log << "READY [key]"
    Stat()
        if(statpanel("Transforms"))
            for(var/atom/A in tokens)
                stat(null,A)
    verb/Update()
        var/mob/B = tokens[4]
        B.pixel_y = B.pixel_y ? 0 : -6
        world.log << "UPDATE mob pixel_y=[B.pixel_y]"
/atom/Click(location, control, params)
    world.log << "CLICK [name] location=[location] control=[control] [params]"

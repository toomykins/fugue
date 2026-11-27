world
    name = "Static affine fixture"
    maxx = 11
    maxy = 11
    maxz = 1
    view = 5
    fps = 20
var/list/tokens = list()
/obj/token
    icon = 'base.png'
/proc/add_token(label, x, y, matrix/M, overlay_mode=0)
    var/obj/token/O = new(locate(x,y,1))
    O.name = label
    O.transform = M
    if(overlay_mode)
        var/image/I = image('overlay.png')
        if(overlay_mode == 2)
            I.transform = matrix(0,-1,0,1,0,0)
        O.overlays += I
    tokens += O
world/New()
    ..()
    world.log = file("events.log")
    add_token("identity",3,9,matrix())
    add_token("rotation",6,9,matrix(0,-1,0,1,0,0))
    add_token("translation",9,9,matrix(1,0,9,0,1,5))
    add_token("reflection",3,6,matrix(-1,0,0,0,1,0))
    add_token("shear",6,6,matrix(1,0.5,0,0,1,0))
    add_token("positive scale",9,6,matrix(1.5,0,0,0,0.75,0))
    add_token("overlay identity",3,3,matrix(),1)
    add_token("overlay root rotation",6,3,matrix(0,-1,0,1,0,0),1)
    add_token("overlay child rotation",9,3,matrix(),2)
/mob
    Login()
        ..()
        loc = locate(6,6,1)
        client.statpanel = "Transforms"
        world.log << "READY [key]"
    Stat()
        if(statpanel("Transforms"))
            for(var/obj/O in tokens)
                stat(null,O)
    verb/Update()
        var/obj/O = tokens[1]
        O.transform = O.transform.c ? matrix() : matrix(1,0,9,0,1,5)
        world.log << "UPDATE [O.transform.c]"
/atom/Click(location, control, params)
    world.log << "CLICK [name] location=[location] control=[control] [params]"

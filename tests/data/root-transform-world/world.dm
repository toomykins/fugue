world
    name = "Ordinary root tree affine fixture"
    maxx = 15
    maxy = 15
    maxz = 1
    view = 7
    fps = 20
var/list/targets = list()
var/last_phase = ""
/obj/token
    icon = 'base.png'
/mob/tree
    icon = 'base.png'
/proc/add_case(label, x, y, matrix/M, offsets=0, rectangular=0, nested=0, reset=0)
    var/atom/movable/O
    if(nested) O = new /mob/tree(locate(x,y,1))
    else O = new /obj/token(locate(x,y,1))
    O.name = label
    if(rectangular == 2) O.icon = 'wide.png'
    if(M) O.transform = M
    var/image/I = image(rectangular ? 'wide.png' : 'square.png')
    var/image/U = image(rectangular ? 'tall.png' : 'leaf.png')
    if(offsets)
        O.pixel_x = 7
        O.pixel_y = 5
        I.pixel_x = 25
        I.pixel_y = 9
        U.pixel_x = -11
        U.pixel_y = -7
    if(nested)
        var/image/N = image('leaf.png')
        N.pixel_x = 13
        N.pixel_y = -5
        N.transform = matrix()
        I.overlays += N
    if(reset) I.appearance_flags = RESET_TRANSFORM
    O.overlays += I
    O.underlays += U
    targets += O
world/New()
    ..()
    world.log = file("events.log")
    add_case("absent",3,12,null,1,1)
    add_case("identity",8,12,matrix(),1,1)
    add_case("rotate",13,12,matrix(0,-1,9,1,0,5),1,1)
    add_case("reverse",3,8,matrix(0,1,9,-1,0,5),1,1)
    add_case("shrink",8,8,matrix(0.5,0,9,0,0.5,5),1,1)
    add_case("nested",13,8,matrix(0,-1,9,1,0,5),1,1,1)
    add_case("wide root",3,4,matrix(0,-1,9,1,0,5),1,2)
    add_case("square children",8,4,matrix(0,-1,9,1,0,5),1)
    add_case("reset",13,4,matrix(0,-1,9,1,0,5),1,1,0,1)
    spawn() watch()
/proc/watch()
    while(TRUE)
        var/phase = file2text("phase.txt")
        if(phase && phase != last_phase)
            last_phase = phase
            var/obj/O = targets[2]
            if(phase == "rotate") O.transform = matrix(0,-1,9,1,0,5)
            else if(phase == "reverse") O.transform = matrix(0,1,9,-1,0,5)
            else if(phase == "shrink") O.transform = matrix(0.5,0,9,0,0.5,5)
            else if(phase == "translate") O.transform = matrix(1,0,9,0,1,5)
            else if(phase == "absent") O.transform = null
            else O.transform = matrix()
            var/atom/movable/N = targets[6]
            N.transform = O.transform
            world.log << "PHASE [phase]"
        sleep(1)
/mob
    Login()
        ..()
        loc = locate(8,8,1)
        world.log << "READY [key]"
/atom/Click(location, control, params)
    world.log << "CLICK [name] location=[location] control=[control] [params]"

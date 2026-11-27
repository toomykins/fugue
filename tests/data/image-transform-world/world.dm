world
    name = "Object image affine fixture"
    maxx = 15
    maxy = 15
    maxz = 1
    view = 7
    fps = 20
var/list/targets = list()
var/list/pictures = list()
var/last_phase = ""
/obj/token
    icon = 'base.png'
/proc/add_case(label, x, y, matrix/M, offsets=0, own=0)
    var/obj/token/O = new(locate(x,y,1))
    O.name = label
    if(M) O.transform = M
    var/image/I = image('overlay.png', O)
    if(offsets)
        O.pixel_x = 7
        O.pixel_y = 5
        I.pixel_x = 25
        I.pixel_y = 9
    if(own == 1) I.transform = matrix(0,-1,0,1,0,0)
    if(own == 2) I.appearance_flags = RESET_TRANSFORM
    targets += O
    pictures += I
world/New()
    ..()
    world.log = file("events.log")
    add_case("absent",3,12,null)
    add_case("identity",8,12,matrix())
    add_case("rotate",13,12,matrix(0,-1,0,1,0,0))
    add_case("shrink",3,8,matrix(0.5,0,0,0,0.5,0))
    add_case("translate offsets",8,8,matrix(1,0,9,0,1,5),1)
    add_case("rotate offsets",13,8,matrix(0,-1,9,1,0,5),1)
    add_case("shrink offsets",3,4,matrix(0.5,0,9,0,0.5,5),1)
    add_case("image owned",8,4,matrix(0,-1,0,1,0,0),1,1)
    add_case("reset transform",13,4,matrix(0,-1,0,1,0,0),1,2)
    spawn() watch()
/proc/watch()
    while(TRUE)
        var/phase = file2text("phase.txt")
        if(phase && phase != last_phase)
            last_phase = phase
            var/obj/O = targets[2]
            if(phase == "rotate") O.transform = matrix(0,-1,0,1,0,0)
            else if(phase == "reverse") O.transform = matrix(0,1,0,-1,0,0)
            else if(phase == "shrink") O.transform = matrix(0.5,0,0,0,0.5,0)
            else O.transform = matrix()
            world.log << "PHASE [phase]"
        sleep(1)
/mob
    Login()
        ..()
        loc = locate(8,8,1)
        client.images += pictures
        world.log << "READY [key]"
/atom/Click(location, control, params)
    world.log << "CLICK [name] location=[location] control=[control] [params]"

world
    name = "Mob supplemental image fixture"
    maxx = 15
    maxy = 15
    maxz = 1
    view = 7
    fps = 20
var/list/targets = list()
var/list/pictures = list()
var/last_phase = ""
var/list/walls = list()
var/obj/camera
/obj/wall
    opacity = 1
/mob/token
    icon = 'base.png'
/proc/add_case(label,x,y,tree=0)
    var/mob/token/M = new(locate(x,y,1))
    M.name = label
    M.pixel_x = 7
    M.pixel_y = 5
    if(tree)
        var/image/O = image('base.png')
        O.pixel_x = -12
        O.pixel_y = -9
        M.overlays += O
    var/image/I = image('overlay.png',M)
    I.pixel_x = 25
    I.pixel_y = 9
    targets += M
    pictures += I
world/New()
    ..()
    world.log = file("events.log")
    camera = new /obj(locate(6,12,1))
    add_case("leaf",3,12)
    add_case("tree",8,12,1)
    add_case("control",13,12,1)
    spawn() watch()
/proc/watch()
    while(TRUE)
        var/phase = file2text("phase.txt")
        if(phase && phase != last_phase)
            last_phase = phase
            for(var/i=1,i<=2,i++)
                var/mob/M = targets[i]
                if(phase == "rotate") M.transform = matrix(0,-1,0,1,0,0)
                else if(phase == "reverse") M.transform = matrix(0,1,0,-1,0,0)
                else if(phase == "shrink") M.transform = matrix(0.5,0,0,0,0.5,0)
                else M.transform = matrix()
                if(phase == "tint") M.color = "#80FFFF"
                else M.color = null
                M.alpha = phase == "alpha" ? 128 : 255
            for(var/obj/W in walls) del(W)
            walls.Cut()
            if(findtext(phase,"sight-") == 1)
                for(var/y=1,y<=15,y++) walls += new /obj/wall(locate(7,y,1))
            // Keep the category target fully behind the wall: the ordinary
            // overlay's negative offset must not reach the visible blocker cell.
            var/mob/T = targets[2]
            var/image/P = pictures[2]
            T.loc = findtext(phase,"sight-") == 1 ? locate(9,12,1) : locate(8,12,1)
            P.pixel_x = findtext(phase,"sight-") == 1 ? 0 : 25
            for(var/client/C)
                C.view = findtext(phase,"sight-") == 1 ? 3 : 7
                C.perspective = EYE_PERSPECTIVE
                C.eye = findtext(phase,"sight-") == 1 ? camera : C.mob
                C.mob.loc = findtext(phase,"sight-") == 1 ? locate(9,11,1) : locate(8,8,1)
                for(var/image/I in C.images)
                    if(I.loc == C.mob) I.pixel_x = findtext(phase,"sight-") == 1 ? 0 : 25
                C.mob.sight = phase == "sight-mobs" ? SEE_MOBS : phase == "sight-objects" ? SEE_OBJS : phase == "sight-self" ? SEE_SELF : 0
            world.log << "PHASE [phase]"
        sleep(1)
/mob/Login()
    ..()
    loc = locate(8,8,1)
    client.images += pictures
    icon = 'base.png'
    var/image/I = image('overlay.png',src)
    I.pixel_x = 25
    client.images += I
    world.log << "READY [key]"
/atom/Click(location,control,params)
    world.log << "CLICK [name] location=[location] control=[control] [params]"

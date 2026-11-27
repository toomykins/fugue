world
    name = "Visibility fixture"
    maxx = 11
    maxy = 11
    maxz = 2
    view = 4
    fps = 20
    turf = /turf/floor
/turf/floor
    icon = 'floor.png'
/obj/blocker
    icon = 'wall.png'
/obj/target
    icon = 'object.png'
/mob/target
    icon = 'mob.png'
var/list/targets = list()
var/list/blockers = list()
var/phase = ""
var/obj/camera
/proc/setup(config)
    for(var/atom/A in blockers) del(A)
    blockers = list()
    for(var/turf/T in world)
        T.opacity = 0
        T.density = 0
        T.icon = 'floor.png'
        T.alpha = 255
    var/list/p = params2list(config)
    var/shape = p["shape"] ? p["shape"] : "wall"
    var/opaque = p["opaque"] != "0"
    var/dense = p["dense"] == "1"
    var/list/cells = list()
    if(p["cells"])
        for(var/pair in splittext(p["cells"],","))
            var/list/xy = splittext(pair,":")
            cells += locate(text2num(xy[1]),text2num(xy[2]),1)
    if(shape == "wall")
        for(var/y=1 to 11) cells += locate(6,y,1)
    if(shape == "point") cells += locate(6,6,1)
    if(shape == "corner") cells += list(locate(6,5,1),locate(5,6,1))
    if(shape == "diagonal") cells += list(locate(6,6,1),locate(7,7,1),locate(8,8,1))
    if(shape == "corridor")
        for(var/y=1 to 11) cells += list(locate(4,y,1),locate(6,y,1))
    for(var/turf/T in cells)
        if(p["door"] == "open" && T.y == 6) continue
        if(p["movable"] == "1")
            var/obj/blocker/B = new(T)
            B.opacity = opaque
            B.density = dense
            if(p["alpha"] == "0") B.alpha = 0
            blockers += B
        else
            T.icon = 'wall.png'
            T.opacity = opaque
            T.density = dense
            if(p["alpha"] == "0") T.alpha = 0
    if(!targets.len)
        var/obj/target/O = new(locate(8,6,1))
        O.name = "behind object"
        targets += O
        var/mob/target/M = new(locate(8,7,1))
        M.name = "behind mob"
        targets += M
        var/obj/target/F = new(locate(4,6,1))
        F.name = "foreground"
        targets += F
    var/obj/target/O = targets[1]
    O.icon = 'object.png'
    O.pixel_x = 0
    O.invisibility = 0
    O.transform = null
    O.overlays = null
    for(var/atom/A in O.vis_contents) del(A)
    O.vis_contents = null
    if(p["invisible"] == "1") O.invisibility = 50
    if(p["large"] == "1")
        O.icon = 'large.png'
        O.pixel_x = p["offset"] ? text2num(p["offset"]) : -96
    if(p["offset"] && p["large"] != "1") O.pixel_x = text2num(p["offset"])
    if(p["affine"] == "1") O.transform = matrix(1,0,-96,0,1,0)
    if(p["overlay"] == "1")
        var/image/I = image('mob.png')
        I.pixel_x = -96
        O.overlays += I
    if(p["vis"] == "1")
        var/obj/target/V = new
        V.pixel_x = p["vis_offset"] ? text2num(p["vis_offset"]) : -64
        O.vis_contents += V
    if(!camera) camera = new(locate(4,6,1))
    camera.loc = locate(p["x"] ? text2num(p["x"]) : 5, p["y"] ? text2num(p["y"]) : 6, p["z"] ? text2num(p["z"]) : 1)
    for(var/client/C)
        if(!C.mob) continue
        C.mob.sight = 0
        if(p["sight"] == "turfs") C.mob.sight = SEE_TURFS
        if(p["sight"] == "objs") C.mob.sight = SEE_OBJS
        if(p["sight"] == "mobs") C.mob.sight = SEE_MOBS
        if(p["sight"] == "through") C.mob.sight = SEE_THRU
        if(p["sight"] == "self") C.mob.sight = SEE_SELF
        if(p["sight"] == "blackness") C.mob.sight = SEE_BLACKNESS
        if(p["sight"] == "living") C.mob.sight = SEE_BLACKNESS | SEE_SELF
        if(p["sight"] == "ghost") C.mob.sight = SEE_TURFS | SEE_OBJS | SEE_MOBS | SEE_SELF
        if(p["sight"] == "blind") C.mob.sight = BLIND
        if(p["sight"] == "blindblack") C.mob.sight = BLIND | SEE_BLACKNESS | SEE_SELF
        C.mob.glide_size = 1
        camera.glide_size = 1
        C.mob.see_invisible = p["see"] ? text2num(p["see"]) : 0
        C.mob.invisibility = p["self_invisible"] == "1" ? 50 : 0
        C.mob.overlays = null
        if(p["self_overlay"])
            var/image/I = image('object.png')
            I.pixel_x = 32
            I.invisibility = text2num(p["self_overlay"])
            C.mob.overlays += I
        C.mob.loc = p["eye"] == "object" ? locate(8,6,1) : camera.loc
        C.images = null
        if(p["image"] == "1")
            var/image/I = image('mob.png',O)
            I.pixel_x = p["image_offset"] ? text2num(p["image_offset"]) : 0
            C.images += I
        C.eye = p["eye"] == "object" ? camera : C.mob
        C.perspective = EYE_PERSPECTIVE
        C.view = p["view"] ? text2num(p["view"]) : 4
        winset(C,"main", "title=\"Visibility fixture [config]\"")
    // Server view() is supplementary evidence only, not a claim about client rendering.
    var/list/V = view(5,camera)
    var/mask = ""
    for(var/y=11 to 1 step -1)
        for(var/x=1 to 11)
            var/turf/T = locate(x,y,1)
            mask += V.Find(T) ? "1" : "0"
        mask += "/"
    world.log << "PHASE [config] view=[mask]"
var/auto_dir = 0
var/auto_step = 0
world/New()
    ..()
    world.log = file("events.log")
    spawn()
        while(TRUE)
            var/config = file2text("phase.txt")
            var/list/p = params2list(config ? config : "")
            if(p["auto"])
                var/requested_dir = text2num(p["auto"])
                if(auto_dir != requested_dir)
                    auto_dir = requested_dir
                    auto_step = (auto_dir == 4 || auto_dir == 1) ? 1 : -1
                for(var/client/C)
                    var/atom/movable/E = C.eye
                    if(!E) continue
                    var/nx = E.x, ny = E.y
                    if(auto_dir == 4 || auto_dir == 8) nx += auto_step
                    else ny += auto_step
                    if(nx < 3 || nx > 7 || ny < 3 || ny > 7)
                        auto_step = -auto_step
                    else
                        E.loc = locate(nx, ny, E.z)
            else
                auto_dir = 0
                auto_step = 0
            sleep(world.tick_lag * 2)
    spawn()
        while(TRUE)
            var/config = file2text("phase.txt")
            if(config && config != phase)
                phase = config
                var/list/p = params2list(config)
                if(p["action"])
                    if(p["action"] == "step")
                        for(var/client/C)
                            if(C.eye) step(C.eye,text2num(p["dir"]))
                    for(var/obj/B in blockers)
                        if(p["action"] == "toggle") B.opacity = !B.opacity
                        if(p["action"] == "move") B.x += 1
                        if(p["action"] == "remove") del(B)
                    world.log << "ACTION [config]"
                else
                    setup(config)
            sleep(1)
/mob/Login()
    ..()
    loc = locate(2,2,1)
    icon = 'mob.png'
    client.statpanel = "Targets"
    var/obj/H = new
    H.icon = 'hud.png'
    H.screen_loc = "NORTHEAST"
    client.screen += H
    setup(phase ? phase : "shape=wall")
    world.log << "READY"
/mob/Stat()
    if(statpanel("Targets"))
        for(var/atom/A in targets) stat(null,A)
/client/Click(atom/object, location, control, params)
    world.log << "CLIENT_CLICK [isnull(object) ? "null" : object.type] control=[control] [params]"
    ..()
/atom/Click(location, control, params)
    world.log << "CLICK [type] [x],[y],[z] control=[control] [params]"
/atom/MouseDown(location,control,params)
    world.log << "DOWN [type] [x],[y] [params]"
/atom/MouseUp(location,control,params)
    world.log << "UP [type] [x],[y] [params]"
/atom/MouseEntered(location,control,params)
    world.log << "ENTER [type] [x],[y]"
/atom/MouseExited(location,control,params)
    world.log << "EXIT [type] [x],[y]"
/atom/MouseDrag(over_object,src_location,over_location,src_control,over_control,params)
    world.log << "DRAG [type] over=[over_object] [params]"
/atom/MouseDrop(over_object,src_location,over_location,src_control,over_control,params)
    world.log << "DROP [type] over=[over_object] [params]"

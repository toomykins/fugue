// Clicks under a HUD plane master (plane 1, screen_loc 1,1) with a matrix, filters or both, over a world object, a
// client image and a vis_contents member on its plane; run with -params "set=<case>" (see set_master()). Every atom
// logs Click() and has a verb of its own, so that an entry of the right-click menu tells which atom it is. Every atom
// also logs the mouse procs a pointer move and a drag reach, for hover and drag over the same shapes.
world
    name = "Plane click probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
var/list/pictures = list()
var/list/hud = list()
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseEntered(location, control, params)
    var/list/p = params2list(params)
    world.log << "ENTER [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseMove(location, control, params)
    var/list/p = params2list(params)
    world.log << "MOVE [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseDown(location, control, params)
    var/list/p = params2list(params)
    world.log << "DOWN [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseUp(location, control, params)
    var/list/p = params2list(params)
    world.log << "UP [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
    var/list/p = params2list(params)
    var/atom/O = over_object
    world.log << "DRAG [name] over=[istype(O) ? O.name : "none"] x=[p["icon-x"]] y=[p["icon-y"]]"
/atom/MouseDrop(over_object, src_location, over_location, src_control, over_control, params)
    var/list/p = params2list(params)
    var/atom/O = over_object
    world.log << "DROP [name] over=[istype(O) ? O.name : "none"] x=[p["icon-x"]] y=[p["icon-y"]]"
/turf
    icon = 'floor.png'
    name = "floor"
    verb/poke_floor()
        set src in world
        world.log << "VERB floor"
/obj/part
    icon = 'box.png'
    plane = 1
    name = "part"
    verb/poke_part()
        set src in world
        world.log << "VERB part"
// Shows the client image (green) and nothing of its own.
/obj/holder
    name = "holder"
    verb/poke_holder()
        set src in world
        world.log << "VERB holder"
// Shows the vis_contents member and nothing of its own.
/obj/visholder
    name = "visholder"
    verb/poke_visholder()
        set src in world
        world.log << "VERB visholder"
/obj/child
    icon = 'blue.png'
    plane = 1
    name = "child"
    verb/poke_child()
        set src in world
        world.log << "VERB child"
// A world object turned by 45 degrees on plane 0, for the right-click menu's rule on a turned icon.
/obj/leaner
    icon = 'box.png'
    name = "leaner"
    verb/poke_leaner()
        set src in world
        world.log << "VERB leaner"
// The source of a render-target mask: the left half of a tile.
/obj/maskatom
    icon = 'mask.png'
    name = "maskatom"
    render_target = "*maskrt"
/obj/maskplanepart
    icon = 'mask.png'
    plane = 2
    name = "maskplanepart"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
    verb/poke_master()
        set src in world
        world.log << "VERB master"
/obj/maskplane
    appearance_flags = PLANE_MASTER
    plane = 2
    name = "maskplane"
    screen_loc = "1,1"
    render_target = "*maskplane"
// Draws plane 1 through its hidden target *p1, as SS13 codebases relay their planes (case word `relay`).
/obj/relay
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    plane = 5
    name = "relay"
    screen_loc = "1,1"
    render_source = "*p1"
    verb/poke_relay()
        set src in world
        world.log << "VERB relay"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/set_master(obj/master/M, kind)
    if(findtext(kind, "tree"))   // an overlay on the master, turned by 45 degrees, as master-transform-world's child-turn
        var/image/I = image('blue.png')
        I.pixel_x = 64
        I.pixel_y = 32
        I.plane = 1
        I.transform = turned(45)
        M.overlays += I
    if(findtext(kind, "turn90")) M.transform = turned(90)
    if(findtext(kind, "scalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "outline1")) M.filters += filter(type = "outline", size = 1, color = "#00ff00")
    var/flags = findtext(kind, "inverse") ? MASK_INVERSE : 0
    if(findtext(kind, "iconalpha")) M.filters += filter(type = "alpha", icon = icon('mask.png'), x = 32, y = 0, flags = flags)
    if(findtext(kind, "rtalpha")) M.filters += filter(type = "alpha", render_source = "*maskrt", x = 32, y = 0, flags = flags)
    if(findtext(kind, "ptalpha")) M.filters += filter(type = "alpha", render_source = "*maskplane", flags = flags)
    if(findtext(kind, "relay")) M.render_target = "*p1"
    // Animated as master-transform-world's ax-* cases: half a second to the end value, then ten minutes held there.
    if(findtext(kind, "spin90") || findtext(kind, "grow2x"))
        var/matrix/T = findtext(kind, "spin90") ? turned(90) : matrix(2, 0, 0, 0, 1, 0)
        animate(M, transform = T, time = 5)
        animate(transform = T, time = 6000)
    if(findtext(kind, "slide"))   // the mask moves from 32 to 40 px right
        animate(M.filters[1], x = 40, time = 5)
        animate(x = 40, time = 6000)
    world.log << "CASE [kind]"
world/New()
    ..()
    world.log = file("events.log")
    var/kind = world.params["set"]
    new /obj/part(locate(5, 4, 1))
    var/obj/holder/H = new(locate(3, 4, 1))
    var/image/I = image('green.png', H)
    I.plane = 1
    pictures += I
    var/obj/visholder/V = new(locate(4, 6, 1))
    var/obj/child/C = new
    V.vis_contents += C
    if(findtext(kind, "selfturn"))   // the image and the member turned by 45 degrees on their own
        I.transform = turned(45)
        C.transform = turned(45)
    var/obj/leaner/L = new(locate(2, 6, 1))
    L.transform = turned(45)
    if(findtext(kind, "rtalpha") || findtext(kind, "selfmask"))
        var/obj/maskatom/A = new(locate(2, 2, 1))
        if(findtext(kind, "rtturn")) A.transform = turned(90)
        if(findtext(kind, "rtoutline")) A.filters += filter(type = "outline", size = 4, color = "#ffffff")
    if(findtext(kind, "selfmask"))   // the part's own alpha mask from that target, moved 4 px right
        var/obj/part/P = locate() in world
        P.filters += filter(type = "alpha", render_source = "*maskrt", x = 4, y = 0, flags = findtext(kind, "inverse") ? MASK_INVERSE : 0)
    if(findtext(kind, "ptalpha"))
        new /obj/maskplanepart(locate(5, 4, 1))
        var/obj/maskplane/MP = new
        if(findtext(kind, "ptturn")) MP.transform = turned(90)
        if(findtext(kind, "ptoutline")) MP.filters += filter(type = "outline", size = 4, color = "#ffffff")
        hud += MP
    if(findtext(kind, "relay")) hud += new /obj/relay
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        var/obj/master/M = new
        set_master(M, world.params["set"])
        client.screen += M
        client.screen += hud
        client.images += pictures
        world.log << "READY [key]"

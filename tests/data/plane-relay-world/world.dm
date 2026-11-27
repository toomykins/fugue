// A plane drawn through a relay: plane 1's master (screen_loc 1,1) has a render target, and a screen object `relay`
// on plane 5 draws it through render_source. Run with -params "set=<case>" (see set_case()). Every atom logs Click()
// and has a verb of its own, so that an entry of the right-click menu tells which atom it is.
world
    name = "Plane relay probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]]"
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
/obj/other
    icon = 'blue.png'
    plane = 1
    name = "other"
    verb/poke_other()
        set src in world
        world.log << "VERB other"
/obj/marker
    icon = 'blue.png'
    name = "marker"
// The whole view on plane 1 in place of `part` and `other` (case word `grid`): every pixel names itself.
/obj/grid
    icon = 'grid.png'
    plane = 1
    name = "grid"
// An ordinary screen object above the relay, put at the relay's screen_loc (case word `mark`).
/obj/mark
    icon = 'yellow.png'
    plane = 6
    name = "mark"
    verb/poke_mark()
        set src in world
        world.log << "VERB mark"
// A HUD object on the relayed plane.
/obj/button
    icon = 'green.png'
    plane = 1
    name = "button"
    screen_loc = "6,2"
    verb/poke_button()
        set src in world
        world.log << "VERB button"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
    render_target = "*p1"
    verb/poke_master()
        set src in world
        world.log << "VERB master"
// As SS13 codebases relay their planes.
/obj/relay
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    plane = 5
    name = "relay"
    screen_loc = "1,1"
    render_source = "*p1"
    verb/poke_relay()
        set src in world
        world.log << "VERB relay"
// The chain's second link (case word `chain`): plane 5, where `relay` stands, has a master with a render target of its
// own, and `relay2` on plane 7 draws that plane, as SS13 codebases stack their planes.
/obj/master5
    appearance_flags = PLANE_MASTER
    plane = 5
    name = "master5"
    screen_loc = "1,1"
    render_target = "*p5"
/obj/relay2
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    plane = 7
    name = "relay2"
    screen_loc = "1,1"
    render_source = "*p5"
    verb/poke_relay2()
        set src in world
        world.log << "VERB relay2"
// A client image's atom and a vis_contents member on the relayed plane (case word `iv`): neither holder draws anything
// of its own.
/obj/holder
    plane = 1
    name = "holder"
    verb/poke_holder()
        set src in world
        world.log << "VERB holder"
/obj/visholder
    plane = 1
    name = "visholder"
    verb/poke_visholder()
        set src in world
        world.log << "VERB visholder"
/obj/child
    icon = 'green.png'
    plane = 1
    name = "child"
    verb/poke_child()
        set src in world
        world.log << "VERB child"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/set_case(client/C, kind)
    var/obj/master/M = new
    if(findtext(kind, "visible")) M.render_target = "p1"   // the plane draws itself as well
    if(findtext(kind, "mturn90")) M.transform = turned(90)
    if(findtext(kind, "mscalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "malpha")) M.alpha = 254
    if(findtext(kind, "mcenter")) M.screen_loc = "CENTER"
    C.screen += M
    C.screen += new /obj/button
    if(findtext(kind, "iv"))   // the image is this client's own; the atoms are made once, in world/New()
        var/image/I = image('yellow.png', locate(/obj/holder))
        I.plane = 1
        C.images += I
    if(findtext(kind, "norelay"))
        new /obj/marker(locate(6, 6, 1))   // so that the map is not one colour (stockprobe waits for a drawn map)
        world.log << "CASE [kind]"
        return
    var/obj/relay/R = new
    if(findtext(kind, "visible")) R.render_source = "p1"
    if(findtext(kind, "nopass")) R.appearance_flags &= ~PASS_MOUSE
    if(findtext(kind, "nokt")) R.appearance_flags &= ~KEEP_TOGETHER
    if(findtext(kind, "rnear")) R.appearance_flags |= PIXEL_SCALE
    if(findtext(kind, "loc23")) R.screen_loc = "2,3"
    if(findtext(kind, "locpx")) R.screen_loc = "2:5,3:-7"
    if(findtext(kind, "loccenter")) R.screen_loc = "CENTER-2,CENTER-1"   // 2,3 in a 7x7 view
    if(findtext(kind, "locne")) R.screen_loc = "EAST-5,NORTH-4"   // 2,3 as well
    if(findtext(kind, "locleft")) R.screen_loc = "LEFT+1,BOTTOM+2"   // edge anchors
    if(findtext(kind, "locright")) R.screen_loc = "RIGHT-5,TOP-4"
    if(findtext(kind, "locroff")) R.screen_loc = "RIGHT-5:4,TOP-4:-3"
    if(findtext(kind, "locws")) R.screen_loc = "WEST+1,SOUTH+2"
    if(findtext(kind, "locpb")) R.screen_loc = "1+25%,1+40%"
    if(findtext(kind, "locpd")) R.screen_loc = "1+10%,1+10%"
    if(findtext(kind, "locpe")) R.screen_loc = "1-20%,1-20%"
    if(findtext(kind, "locpf")) R.screen_loc = "RIGHT-90%,TOP-90%"
    if(findtext(kind, "locpg")) R.screen_loc = "1+40%,1+25%"
    if(findtext(kind, "locrange")) R.screen_loc = "1,1 to 3,3"   // ranges
    if(findtext(kind, "locrow")) R.screen_loc = "2,3 to 4,3"
    if(findtext(kind, "rpx"))   // the relay's own offsets
        R.pixel_x = 10
        R.pixel_y = 5
    if(findtext(kind, "rpw"))
        R.pixel_w = 10
        R.pixel_z = 5
    if(findtext(kind, "rturn90")) R.transform = turned(90)
    if(findtext(kind, "rturn45")) R.transform = turned(45)
    if(findtext(kind, "rscalex2")) R.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "rhalf")) R.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    if(findtext(kind, "rflip")) R.transform = matrix(-1, 0, 0, 0, 1, 0)
    if(findtext(kind, "rmove")) R.transform = matrix(1, 0, 10, 0, 1, 5)
    if(findtext(kind, "rgrowmove")) R.transform = matrix(1.5, 0, 100, 0, 1.5, 0)
    if(findtext(kind, "rturnmove"))   // turned by 30 degrees, then moved 20 right and 10 up
        var/matrix/T = turned(30)
        T.Translate(20, 10)
        R.transform = T
    var/at = findtext(kind, "rm_")   // rm_a_b_c_d_e_f: any matrix on the relay
    if(at)
        var/list/v = splittext(copytext(kind, at + 3), "_")
        R.transform = matrix(text2num(v[1]), text2num(v[2]), text2num(v[3]), text2num(v[4]), text2num(v[5]), text2num(v[6]))
    at = findtext(kind, "rt_")   // rt_<degrees>: the relay turned (last in the case name)
    if(at) R.transform = turned(text2num(copytext(kind, at + 3)))
    if(findtext(kind, "locpbare")) R.screen_loc = "40%,40%"   // percents of the fourth round
    if(findtext(kind, "locpneg")) R.screen_loc = "RIGHT-15%,TOP-15%"
    if(findtext(kind, "locprange")) R.screen_loc = "1+10%,1+10% to 2,2"
    if(findtext(kind, "fblur")) R.filters += filter(type = "blur", size = 2)   // the relay's own filters
    if(findtext(kind, "foutline")) R.filters += filter(type = "outline", size = 1, color = "#ffffff")
    if(findtext(kind, "fcolor")) R.filters += filter(type = "color", color = list(0,0,1, 0,1,0, 1,0,0))
    if(findtext(kind, "rov")) R.overlays += image('yellow.png')   // an overlay of the relay's own
    if(findtext(kind, "ovrelay"))   // the plane drawn by an overlay of a screen object instead of the object itself
        R.render_source = null
        R.appearance_flags = PASS_MOUSE | NO_CLIENT_COLOR
        var/mutable_appearance/O = new
        O.render_source = "*p1"
        O.appearance_flags = PASS_MOUSE
        O.plane = FLOAT_PLANE
        R.overlays += O
    C.screen += R
    if(findtext(kind, "mark"))
        var/obj/mark/K = new
        K.screen_loc = R.screen_loc
        C.screen += K
    if(findtext(kind, "chain"))   // relay's plane relayed again
        var/obj/master5/M5 = new
        if(findtext(kind, "m5turn90")) M5.transform = turned(90)
        C.screen += M5
        var/obj/relay2/B = new
        if(findtext(kind, "bopaque")) B.appearance_flags &= ~PASS_MOUSE
        if(findtext(kind, "bneg")) B.screen_loc = "1:-40,1:-72"   // with loc23 the boxes end 8 left and 8 down
        if(findtext(kind, "bturn90")) B.transform = turned(90)
        if(findtext(kind, "bturn45")) B.transform = turned(45)
        if(findtext(kind, "bhalf")) B.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
        C.screen += B
    world.log << "CASE [kind]"
world/New()
    ..()
    world.log = file("events.log")
    if(findtext(world.params["set"], "iv"))
        new /obj/holder(locate(3, 3, 1))
        var/obj/visholder/V = new(locate(6, 5, 1))
        V.vis_contents += new /obj/child
    if(findtext(world.params["set"], "grid"))
        new /obj/grid(locate(1, 1, 1))
    else
        new /obj/part(locate(5, 4, 1))
        new /obj/other(locate(2, 5, 1))
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        set_case(client, world.params["set"])
        world.log << "READY [key]"

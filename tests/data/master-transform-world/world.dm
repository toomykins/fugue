// A HUD plane master (plane 1, screen_loc 1,1) owning an overlay image and a world object on its plane, with a matrix on
// the master or on the overlay, or (cases starting "fx-") a master with a matrix, filters or both and no overlay; run
// with -params "set=<case>" (see set_master()). The "fx-grid-" cases put one view-sized icon on the plane in place of
// the box, under the matrix shape() names. Cases starting "ax-" animate such a master (see animate_master()); only
// those with "tree" in the name keep the overlay.
world
    name = "Master transform probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 10
    mob = /mob/observer
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]] loc=[location]"
/turf
    icon = 'floor.png'
    name = "floor"
/obj/part
    icon = 'box.png'
    plane = 1
    name = "part"
/obj/edge
    icon = 'full.png'
    plane = 1
    name = "edge"
/obj/grid
    icon = 'grid.png'
    plane = 1
    name = "grid"
/obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    name = "master"
    screen_loc = "1,1"
// Draws the master's hidden target *p1, as SS13 codebases relay their planes (fx-grid-relay-*, fx-grid-rth-*).
/obj/relay
    appearance_flags = PASS_MOUSE | KEEP_TOGETHER | NO_CLIENT_COLOR
    plane = 5
    name = "relay"
    screen_loc = "1,1"
    render_source = "*p1"
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
// The matrix of an fx-grid case, named by the case's last part.
/proc/shape(name)
    switch(name)
        if("plain") return matrix()
        if("s75") return matrix(0.75, 0, 0, 0, 0.75, 0)
        if("s50") return matrix(0.5, 0, 0, 0, 0.5, 0)
        if("s25") return matrix(0.25, 0, 0, 0, 0.25, 0)
        if("s60") return matrix(0.6, 0, 0, 0, 0.6, 0)
        if("s40") return matrix(0.4, 0, 0, 0, 0.4, 0)
        if("sx50") return matrix(0.5, 0, 0, 0, 1, 0)
        if("sy50") return matrix(1, 0, 0, 0, 0.5, 0)
        if("sx50y75") return matrix(0.5, 0, 0, 0, 0.75, 0)
        if("s50t90")
            var/matrix/M = matrix(0.5, 0, 0, 0, 0.5, 0)
            M.Turn(90)
            return M
        if("s50t30")
            var/matrix/M = matrix(0.5, 0, 0, 0, 0.5, 0)
            M.Turn(30)
            return M
        if("s50m") return matrix(0.5, 0, 16, 0, 0.5, 8)
        if("s50m3") return matrix(0.5, 0, 3, 0, 0.5, -5)
        if("s150") return matrix(1.5, 0, 0, 0, 1.5, 0)
        if("s200") return matrix(2, 0, 0, 0, 2, 0)
        if("s300") return matrix(3, 0, 0, 0, 3, 0)
        if("sx200y300") return matrix(2, 0, 0, 0, 3, 0)
        if("s200m3") return matrix(2, 0, 3, 0, 2, -5)
        if("s200t90")
            var/matrix/M = matrix(2, 0, 0, 0, 2, 0)
            M.Turn(90)
            return M
        if("s200t30")
            var/matrix/M = matrix(2, 0, 0, 0, 2, 0)
            M.Turn(30)
            return M
        if("s200e") return matrix(2, 0, -112, 0, 2, 112)   // the plane's bottom right quarter fills the view
        if("s300t1")
            var/matrix/M = matrix(3, 0, 0, 0, 3, 0)
            M.Turn(1)
            return M
        if("s400t4")
            var/matrix/M = matrix(4, 0, 0, 0, 4, 0)
            M.Turn(4)
            return M
    world.log << "UNKNOWN SHAPE [name]"
    return matrix()
/proc/set_master(obj/master/M, kind)
    var/image/I = image('blue.png')
    I.pixel_x = 64
    I.pixel_y = 32
    I.plane = 1
    if(findtext(kind, "child-turn")) I.transform = turned(45)
    if(findtext(kind, "child-scale")) I.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "child-lean")) I.transform = turned(30)
    if(!findtext(kind, "solo") && !findtext(kind, "fx-") && (!findtext(kind, "ax-") || findtext(kind, "tree"))) M.overlays += I
    if(findtext(kind, "group")) M.appearance_flags |= KEEP_TOGETHER
    if(findtext(kind, "master-turn")) M.transform = turned(90)
    if(findtext(kind, "master-scale")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "master-shift")) M.transform = matrix(1, 0, 16, 0, 1, 8)
    if(findtext(kind, "master-triple")) M.transform = matrix(3, 0, 0, 0, 1, 0)
    if(findtext(kind, "master-grow")) M.transform = matrix(1.5, 0, 0, 0, 1.5, 0)
    if(findtext(kind, "scalex2")) M.transform = matrix(2, 0, 0, 0, 1, 0)
    if(findtext(kind, "turn90")) M.transform = turned(90)
    if(findtext(kind, "half")) M.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    // fx-grid-kt-*: the grid is a KEEP_TOGETHER group (it carries its own icon as an overlay) and takes the matrix
    // itself, not the master. fx-grid-relay-*: the master has the hidden target *p1 and a relay draws it through the
    // matrix. "pxs" puts PIXEL_SCALE on whatever takes the matrix. fx-grid-rth-*: the master keeps the matrix, with the
    // hidden target *p1, and a relay without a matrix draws it; fx-grid-rtv-*: the visible target p1, and no relay.
    var/matrix/shaped = findtext(kind, "grid") ? shape(copytext(kind, findlasttext(kind, "-") + 1)) : null
    var/pxs = findtext(kind, "pxs") ? PIXEL_SCALE : 0
    if(findtext(kind, "grid-kt-"))
        for(var/obj/grid/G in world)
            G.overlays += icon('grid.png')
            G.appearance_flags |= KEEP_TOGETHER | pxs
            G.transform = shaped
    else if(findtext(kind, "grid-relay-"))
        M.render_target = "*p1"
        var/obj/relay/R = new
        R.appearance_flags |= pxs
        R.transform = shaped
        usr.client.screen += R
    else if(shaped)
        M.transform = shaped
        M.appearance_flags |= pxs
    else if(pxs) M.appearance_flags |= PIXEL_SCALE
    if(findtext(kind, "grid-rth-"))
        M.render_target = "*p1"
        usr.client.screen += new /obj/relay
    if(findtext(kind, "grid-rtv-")) M.render_target = "p1"
    if(findtext(kind, "dim")) M.alpha = 192
    if(findtext(kind, "tint")) M.color = rgb(255, 255, 128)
    if(findtext(kind, "cover")) M.filters += filter(type = "alpha", icon = icon('cover.png'))
    if(findtext(kind, "outline1")) M.filters += filter(type = "outline", size = 1, color = "#00ff00")
    if(findtext(kind, "outline4")) M.filters += filter(type = "outline", size = 4, color = "#00ff00")
    if(findtext(kind, "blur2")) M.filters += filter(type = "blur", size = 2)
    if(findtext(kind, "shadow6")) M.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
    if(findtext(kind, "wave")) M.filters += filter(type = "wave", x = 16, y = 0, size = 3, flags = WAVE_SIDEWAYS)
    if(findtext(kind, "ripple")) M.filters += filter(type = "ripple", x = 0, y = 0, size = 6, repeat = 16, radius = 24, falloff = 1)
    if(findtext(kind, "bloom")) M.filters += filter(type = "bloom", threshold = rgb(100, 0, 0), size = 3, offset = 2, alpha = 255)
    if(findtext(kind, "swap")) M.filters += filter(type = "color", color = list(0,1,0, 1,0,0, 0,0,1))
    if(findtext(kind, "mask")) M.filters += filter(type = "alpha", icon = icon('mask.png'), x = 32, y = 0)
    if(findtext(kind, "ax-")) animate_master(M, kind)
    world.log << "CASE [kind]"
// ax-*: the master above, then animated. Each animation takes half a second and then holds its end for ten minutes
// (a second step to the same value), so a grab taken seconds after login sees the end of the first step while the
// animation is still running. With "mid" the first step takes a minute instead and there is no hold.
/proc/animate_master(obj/master/M, kind)
    if(findtext(kind, "tree-group"))
        // Stock draws nothing of this plane: a blue box on plane 0 keeps the map from being one flat colour, which
        // stockprobe takes for a blank window.
        var/obj/O = new /obj(locate(2, 6, 1))
        O.icon = 'blue.png'
        O.plane = 0
        O.name = "marker"
    if(findtext(kind, "-add") || findtext(kind, "-clear") || findtext(kind, "pline") || findtext(kind, "late"))
        restructure(M, kind)
        return
    var/time = findtext(kind, "mid") ? 600 : 5
    var/hold = !findtext(kind, "mid")
    var/parallel = 0
    if(findtext(kind, "grow2x") || findtext(kind, "spin90"))
        var/matrix/T = findtext(kind, "grow2x") ? matrix(2, 0, 0, 0, 1, 0) : turned(90)
        animate(M, transform = T, time = time)
        if(hold) animate(transform = T, time = 6000)
        parallel = ANIMATION_PARALLEL
    if(findtext(kind, "toshadow"))
        var/shadow = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
        animate(M, filters = shadow, time = time, flags = parallel)
        if(hold) animate(filters = shadow, time = 6000)
        return
    var/f = length(M.filters) ? M.filters[1] : null
    if(!f) return
    if(findtext(kind, "size4"))
        animate(f, size = 4, time = time, flags = parallel)
        if(hold) animate(size = 4, time = 6000)
    if(findtext(kind, "left"))
        animate(f, x = -6, time = time, flags = parallel)
        if(hold) animate(x = -6, time = 6000)
    if(findtext(kind, "phase"))
        animate(f, offset = 0.5, time = time, flags = parallel)
        if(hold) animate(offset = 0.5, time = 6000)
// A filter list that changes structure during an animation that animates nothing but alpha: a tenth-of-a-second step,
// then the filters change ("-add": a drop shadow after the outline, "-clear": no filter), then a ten-minute step.
// "addclear" adds the shadow before a ten-minute step and clears the list before a second one, so the step being drawn
// carries filters the atom no longer has. "late" changes the filters after a single ten-minute step ("lateadd" adds
// the shadow, otherwise the list is cleared). "pline" does it to the red box (an outline on it) in place of the
// master; "still" only sets the box's filters, as a settled reference.
/proc/restructure(obj/master/M, kind)
    var/atom/movable/A = M
    var/obj/part/P = locate() in world
    if(findtext(kind, "pline"))
        // The box is shared by every client that logs in (stockprobe runs two): set it up once.
        if(length(P.filters)) return
        A = P
        A.filters += filter(type = "outline", size = 1, color = "#00ff00")
        if(findtext(kind, "pdrop")) A.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
        if(findtext(kind, "still")) return
    if(findtext(kind, "late"))
        animate(A, alpha = 255, time = 6000)
        if(findtext(kind, "lateadd")) A.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
        else A.filters = null
        return
    animate(A, alpha = 255, time = 1)
    if(findtext(kind, "-add") || findtext(kind, "addclear"))
        A.filters += filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ffffff")
    if(findtext(kind, "-clear")) A.filters = null
    animate(alpha = 255, time = 6000)
    if(findtext(kind, "addclear"))
        A.filters = null
        animate(alpha = 255, time = 6000)
world/New()
    ..()
    world.log = file("events.log")
    if(findtext(world.params["set"], "grid")) new /obj/grid(locate(1, 1, 1))
    else new /obj/part(locate(5, 4, 1))
    if(findtext(world.params["set"], "edge")) new /obj/edge(locate(7, 4, 1))
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(4, 4, 1)
        var/obj/master/M = new
        set_master(M, world.params["set"])
        client.screen += M
        world.log << "READY [key]"

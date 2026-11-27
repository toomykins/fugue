// Right-click menus on vis_contents members (a mob in nullspace, a mob on the map, a turf, a turf from another z) and
// a z change under a held button. Every turf and /mob/dummy has two verbs, near (set src in view()) and anywhere (set
// src in world); a verb logs the atom it ran on and where that atom is. See README.md.
world
    name = "Vis menu probe"
    maxx = 18
    maxy = 12
    maxz = 2
    view = "18x12"
    fps = 10
    mob = /mob/observer
    turf = /turf/ground
/atom/Click(location, control, params)
    world.log << "CLICK [name] loc=[location]"
/atom/MouseDown(location, control, params)
    world.log << "DOWN [name]"
/atom/MouseUp(location, control, params)
    world.log << "UP [name] loc=[location]"
/client/MouseUp(object, location, control, params)
    world.log << "CLIENT-UP object=[object ? "[object]" : "null"] location=[location ? "[location]" : "null"] control=[control]"
    ..()
/proc/said(atom/A, what)
    world.log << "VERB [what] src=[A.name] at=[A.x],[A.y],[A.z]"
/turf/ground
    name = "floor"
    icon = 'floor.png'
    verb/near()
        set src in view()
        said(src, "[type] near")
    verb/anywhere()
        set src in world
        said(src, "[type] anywhere")
/mob/dummy
    icon = 'box.png'
    layer = 5
    verb/near()
        set src in view()
        said(src, "[type] near")
    verb/anywhere()
        set src in world
        said(src, "[type] anywhere")
/obj/thing
    icon = 'box.png'
    layer = 5
/obj/thing/plain
    verb/near()
        set src in view()
        said(src, "[type] near")
/obj/thing/jumper
    icon = 'blue.png'
    MouseDown(location, control, params)
        world.log << "DOWN [name]"
        var/mob/M = usr
        spawn(3)
            M.loc = locate(9, 6, 2)
            world.log << "JUMPED z=[M.z]"
/proc/holder(x, label)
    var/obj/thing/O = new(locate(x, 10, 1))
    O.name = label
    O.icon = null
    return O
world/New()
    ..()
    world.log = file("events.log")
    var/obj/thing/O
    var/mob/dummy/M
    O = holder(2, "holds-ghost")
    M = new
    M.name = "ghost"
    O.vis_contents += M
    O = holder(4, "holds-walker")
    M = new(locate(2, 2, 1))
    M.name = "walker"
    O.vis_contents += M
    var/turf/T = locate(18, 1, 1)
    T.name = "copied-turf"
    T.icon = 'tile.png'
    O = holder(6, "holds-turf")
    O.vis_contents += T
    T = locate(3, 3, 2)
    T.name = "far-turf"
    T.icon = 'tile.png'
    T = locate(3, 3, 1)
    T.name = "same-cell-turf"
    O = holder(8, "holds-far-turf")
    O.vis_contents += locate(3, 3, 2)
    O = new /obj/thing/jumper(locate(11, 10, 1))
    O.name = "jumper"
    O = new /obj/thing/plain(locate(16, 10, 1))
    O.name = "plain"
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9, 6, 1)
        world.log << "READY [key]"

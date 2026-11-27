// The map's right-click menu: which picture each entry shows and what its text says. Every probe atom stands on its
// own cell over a verbless floor and has the verb poke (set src in world); the player is an invisible observer at
// 9,6 whose verb examine takes an obj (as obj in view()). See README.md for the cells and what stock showed.
world
    name = "Menu probe"
    maxx = 18
    maxy = 12
    view = "18x12"
    fps = 10
    icon_size = 32
    mob = /mob/observer
    turf = /turf/floor
/proc/said(atom/A, what)
    world.log << "VERB [what] src=[A.name]"
/turf/floor
    name = "floor"
    icon = 'floor.png'
/turf/verbed
    name = "verbed-turf"
    icon = 'z.dmi'
    icon_state = "white"
    verb/poke()
        set src in world
        said(src, "poke")
/obj/probe
    icon = 'z.dmi'
    icon_state = "a"
    layer = 5
    verb/poke()
        set src in world
        said(src, "poke")
/mob/dummy
    name = "dummy-mob"
    icon = 'z.dmi'
    icon_state = "a"
    verb/poke()
        set src in world
        said(src, "poke")
// The verb variants, all on one obj.
/obj/probe/verbs
    name = "verbs"
    verb/do_thing()
        set src in world
        said(src, "do_thing")
    verb/Capital()
        set src in world
        said(src, "Capital")
    verb/named()
        set name = "Look At"
        set src in world
        said(src, "named")
    verb/described()
        set desc = "describes it"
        set src in world
        said(src, "described")
    verb/hid()
        set hidden = 1
        set src in world
        said(src, "hid")
    verb/nopop()
        set popup_menu = 0
        set src in world
        said(src, "nopop")
    verb/zcat()
        set category = "Zeta"
        set src in world
        said(src, "zcat")
    verb/acat()
        set category = "Alpha"
        set src in world
        said(src, "acat")
    verb/nullcat()
        set category = null
        set src in world
        said(src, "nullcat")
    verb/withargs(t as text)
        set src in world
        said(src, "withargs [t]")
    verb/amp()
        set name = "Cut & Paste"
        set src in world
        said(src, "amp")
    verb/html()
        set name = "<b>Bold</b>"
        set src in world
        said(src, "html")
    verb/_under()
        set src in world
        said(src, "_under")
    verb/x10()
        set src in world
        said(src, "x10")
    verb/near()
        set src in oview()
        said(src, "near")
    verb/carried()
        set src in usr
        said(src, "carried")
    verb/hyphen()
        set name = "a-b"
        set src in world
        said(src, "hyphen")
/obj/argonly
    name = "argonly"
    icon = 'z.dmi'
    icon_state = "a"
/proc/put(type, x, y, label, state)
    var/atom/movable/A = new type(locate(x, y, 1))
    if(label != null) A.name = label
    if(state != null) A.icon_state = state
    return A
world/New()
    ..()
    world.log = file("events.log")
    var/atom/movable/A
    // Row 11: pictures.
    put(/obj/probe, 1, 11, "plain", "a")
    A = put(/obj/probe, 3, 11, "dir-east", "d")
    A.dir = EAST
    put(/obj/probe, 5, 11, "anim", "anim")
    A = put(/obj/probe, 7, 11, "overlay", "half")
    A.overlays += image('z.dmi', "ov")
    A = put(/obj/probe, 9, 11, "underlay", "half")
    A.underlays += image('z.dmi', "un")
    A = put(/obj/probe, 11, 11, "colored", "white")
    A.color = "#00ff00"
    A = put(/obj/probe, 13, 11, "alpha", "a")
    A.alpha = 128
    A = put(/obj/probe, 15, 11, "scaled", "a")
    A.transform = matrix(0.5, 0, 0, 0, 0.5, 0)
    A = put(/obj/probe, 17, 11, "turned", "a")
    A.transform = turn(matrix(), 90)
    // Row 9: more pictures.
    A = put(/obj/probe, 1, 9, "big", "")
    A.icon = 'big.dmi'
    A = put(/obj/probe, 4, 9, "small", "")
    A.icon = 'small.dmi'
    A = put(/obj/probe, 6, 9, "iconless", null)
    A.icon = null
    put(/obj/probe, 8, 9, "nostate", "nosuch")
    A = put(/obj/probe, 10, 9, "offset", "a")
    A.pixel_x = 8
    A.pixel_y = 8
    A = put(/obj/probe, 12, 9, "outlined", "half")
    A.filters = filter(type = "outline", size = 2, color = "#ffffff")
    A = put(/obj/probe, 14, 9, "holder", "half")
    var/obj/probe/M = new
    M.name = "member"
    M.icon_state = "ov"
    A.vis_contents += M
    A = put(/obj/probe, 16, 9, "imaged", "half")
    global.imaged = A
    A = put(/obj/probe, 18, 9, "changer", "a")
    spawn(10) flip(A)
    // Row 7: names.
    put(/obj/probe, 1, 7, "\improper the Rock", null)
    put(/obj/probe, 3, 7, "\proper rock", null)
    put(/obj/probe, 5, 7, "Tom & Jerry", null)
    put(/obj/probe, 7, 7, "<b>bold</b> name", null)
    put(/obj/probe, 9, 7, "café ünï", null)
    put(/obj/probe, 11, 7, "a very long name that goes on and on past any menu width you would expect to see in a game", null)
    put(/obj/probe, 13, 7, "twin", null)
    put(/obj/probe, 13, 7, "twin", "white")
    A = put(/obj/probe, 15, 7, "suffixed", null)
    A.suffix = "(worn)"
    put(/obj/probe, 17, 7, "", null)
    // Row 4: verbs and stacks.
    put(/obj/probe/verbs, 2, 4, null, null)
    var/turf/T = locate(5, 4, 1)
    new /turf/verbed(T)
    A = put(/obj/probe, 5, 4, "first", "half")
    A.layer = 5
    A = put(/obj/probe, 5, 4, "second", "ov")
    A.layer = 6
    A = put(/obj/probe, 5, 4, "third", "un")
    A.layer = 4
    new /mob/dummy(locate(8, 4, 1))
    put(/obj/argonly, 11, 4, null, null)
    A = put(/obj/probe, 14, 4, "hud", "a")
    A.loc = null
    A.screen_loc = "18,1"
    global.hud = A
var/atom/movable/imaged
var/atom/movable/hud
/proc/flip(atom/movable/A)
    while(A)
        A.icon_state = A.icon_state == "a" ? "white" : "a"
        sleep(10)
/mob/observer
    invisibility = 101
    verb/examine(obj/O as obj in view())
        world.log << "VERB examine src=[O.name]"
    Login()
        ..()
        loc = locate(9, 6, 1)
        client.images += image('z.dmi', global.imaged, "ov", 6)
        client.screen += global.hud
        world.log << "READY [key]"

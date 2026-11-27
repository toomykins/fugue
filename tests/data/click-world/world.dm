// What a click hits: vis members (turf copies and their contents, a mob, VIS_INHERIT_ID, a turned copy), a turned
// screen object, KEEP_TOGETHER ink, filter ink (outline, drop_shadow, wave), filtered images, maptext and an image
// overlay on a turned target. Every atom logs Click() and MouseDown() with src, usr and the icon-x/y params.
world
    name = "Click probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
var/list/pictures = list()
var/list/huds = list()
/atom/Click(location, control, params)
    var/list/p = params2list(params)
    world.log << "CLICK [name] usr=[usr ? usr.name : "-"] x=[p["icon-x"]] y=[p["icon-y"]] loc=[location]"
/atom/MouseDown(location, control, params)
    var/list/p = params2list(params)
    world.log << "DOWN [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/obj/thing
    icon = 'box.png'
    layer = 5
/mob/dummy
    icon = 'box.png'
    layer = 5
/proc/slot(label)
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    cases += O
    world.log << "CASE [label]"
    return O
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/member(obj/holder, name, ico = 'box.png')
    var/obj/thing/V = new
    V.name = name
    V.icon = ico
    holder.vis_contents += V
    return V
/proc/set_a()
    var/obj/thing/O
    var/obj/thing/V
    var/image/I
    // A turf copy: the corner turf (its own icon) and an obj standing on it.
    var/turf/T = locate(18, 1, 1)
    T.icon = 'tile.png'
    T.name = "copied-turf"
    var/obj/thing/C = new(T)
    C.name = "copied-content"
    C.icon = 'small.png'
    O = slot("turf-member"); O.icon = null; O.vis_contents += T
    O = slot("mob-member"); O.icon = null
    var/mob/dummy/M = new
    M.name = "member-mob"
    O.vis_contents += M
    O = slot("inherit-id"); O.icon = null; V = member(O, "member-inherit"); V.vis_flags = VIS_INHERIT_ID
    O = slot("plain-member"); O.icon = null; member(O, "member-plain")
    O = slot("vis-turned"); O.icon = null; V = member(O, "member-turned"); V.transform = turned(45)
    // A turned screen object on the next case's tile (screen and world tiles coincide here).
    O = slot("screen-turned"); O.icon = null
    var/obj/thing/S = new
    S.name = "hud-turned"
    S.screen_loc = "17,10"
    S.transform = turned(45)
    huds += S
    O = slot("kt-root"); O.appearance_flags = KEEP_TOGETHER; O.overlays += image('blue.png', pixel_x = 12)
    O = slot("outline"); O.filters = filter(type = "outline", size = 2, color = "#00ff00")
    O = slot("shadow"); O.filters = filter(type = "drop_shadow", x = 6, y = 0, size = 0, color = "#ff00ff")
    O = slot("image"); O.icon = null
    I = image('blue.png', O); I.filters = filter(type = "outline", size = 2, color = "#00ff00"); pictures += I
    O = slot("maptext"); O.icon = null; O.maptext = "<span style='color:#ffffff;font-size:14pt'>MW</span>"
    O = slot("image-turned-target"); O.transform = turned(90)
    I = image('blue.png', O); I.overlays += image('small.png', pixel_x = 14); pictures += I
    O = slot("wave"); O.filters = filter(type = "wave", x = 6, size = 3)
    O = slot("kt-outline"); O.appearance_flags = KEEP_TOGETHER; O.overlays += image('blue.png', pixel_x = 12); O.filters = filter(type = "outline", size = 2, color = "#00ff00")
    O = slot("scaled-member"); O.icon = null; V = member(O, "member-scaled"); V.transform = matrix(2, 0, 0, 0, 1, 0)
    O = slot("kt-turned"); O.appearance_flags = KEEP_TOGETHER; O.overlays += image('blue.png', pixel_x = 12); O.transform = turned(90)
    O = slot("filter-turned"); O.filters = filter(type = "outline", size = 2, color = "#00ff00"); O.transform = turned(45)
    O = slot("maptext-big"); O.icon = null; O.maptext_width = 64; O.maptext = "<span style='color:#ffffff;font-size:10pt'>#####</span>"
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(17,12,1)))
        t.icon = 'floor.png'
        t.name = "floor"
    set_a()
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9,6,1)
        client.images += pictures
        client.screen += huds
        world.log << "READY [key]"

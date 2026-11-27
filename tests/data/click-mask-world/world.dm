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
var/list/hud = list()
/atom/Click(location, control, params)
	var/list/p = params2list(params)
	world.log << "CLICK [usr.key] [name] x=[p["icon-x"]] y=[p["icon-y"]]"
/turf
	name = "floor"
/obj/thing
	icon = 'big.png'
	layer = 3
/obj/screen
	icon = null
/particles/one
	width = 96
	height = 96
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#ff0000"
	icon = 'sq.png'
	position = list(0, 14)
/proc/slot(label)
	var/i = length(cases)
	var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
	O.name = label
	cases += O
	world.log << "CASE [label]"
	return O
/proc/group(obj/thing/O)
	O.appearance_flags = KEEP_TOGETHER
	O.alpha = 128
world/New()
	..()
	world.log = file("events.log")
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
	var/obj/thing/O
	var/image/I
	var/matrix/A = matrix(); A.Turn(45)
	var/matrix/B = matrix(); B.Turn(135)
	O = slot("img-kt"); group(O); I = image('shape.png', O); I.layer = FLOAT_LAYER; I.pixel_x = 8; pictures += I
	O = slot("mask-icon"); O.filters += filter(type = "alpha", icon = icon('mask.png'))
	O = slot("mask-inverse"); O.filters += filter(type = "alpha", icon = icon('mask.png'), flags = MASK_INVERSE)
	O = slot("mask-swap"); O.filters += filter(type = "alpha", icon = icon('mask.png'), flags = MASK_SWAP)
	O = slot("particle"); O.particles = new /particles/one
	O = slot("kt-anim-turn"); group(O); O.overlays += image('shape.png'); O.transform = A; animate(O, transform = B, time = 60000)
	O = slot("kt-turn-opaque2"); group(O); O.transform = A; O.mouse_opacity = 2
	O = slot("flat-turn-opaque2"); O.transform = A; O.mouse_opacity = 2
	O = slot("plane-shift"); O.plane = 7
	var/obj/screen/M = new(); M.plane = 7; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master"
	var/matrix/T = matrix(); T.Translate(8, 0); M.transform = T; hud += M
	O = slot("plane-mask"); O.plane = 8
	M = new(); M.plane = 8; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master8"
	M.filters += filter(type = "alpha", icon = icon('mask96.png'), flags = MASK_INVERSE); hud += M
	O = slot("two-outlines"); O.filters += filter(type = "outline", size = 2, color = "#ff0000"); O.filters += filter(type = "outline", size = 2, color = "#ffffff")
	O = slot("plane-blur"); O.plane = 9
	M = new(); M.plane = 9; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master9"; M.filters += filter(type = "blur", size = 1); hud += M
	O = slot("plane-outline"); O.plane = 10
	M = new(); M.plane = 10; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master10"; M.filters += filter(type = "outline", size = 2, color = "#ffffff"); hud += M
	O = slot("plane-color"); O.plane = 11
	M = new(); M.plane = 11; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master11"; M.filters += filter(type = "color", color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)); hud += M
	O = slot("plane-mask-hide"); O.plane = 12
	M = new(); M.plane = 12; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master12"; M.filters += filter(type = "alpha", icon = icon('mask96.png')); hud += M
	O = slot("plane-plain"); O.plane = 13
	M = new(); M.plane = 13; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master13"; hud += M
	O = slot("plane-alpha0"); O.plane = 14
	M = new(); M.plane = 14; M.appearance_flags = PLANE_MASTER; M.screen_loc = "1,1"; M.name = "master14"; M.alpha = 0; hud += M
	O = slot("plain")
/mob/observer
	invisibility = 101
	Login()
		..()
		loc = locate(9,6,1)
		client.screen += hud
		client.images += pictures
		world.log << "READY [key]"

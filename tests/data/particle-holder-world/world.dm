world
	name = "Particle holder probe"
	maxx = 18
	maxy = 12
	maxz = 1
	view = "18x12"
	fps = 10
	mob = /mob/observer
var/list/cases = list()
var/list/hud = list()
/obj/thing
	icon = 'small.png'
	layer = 3
/obj/screen
	icon = 'small.png'
	layer = 3
/mob/dummy
	icon = 'small.png'
	layer = 3
/particles/one
	width = 96
	height = 96
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#ff0000"
	icon = 'sq.png'
/particles/ell
	width = 96
	height = 96
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#00ff00"
	icon = 'ell.png'
/particles/small
	width = 24
	height = 24
	count = 1
	spawning = 1
	lifespan = 100000
	color = "#00ff00"
	icon = 'ell.png'
/proc/slot(label)
	var/i = length(cases)
	var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
	O.name = label
	cases += O
	world.log << "CASE [label]"
	return O
world/New()
	..()
	world.log = file("events.log")
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
	var/obj/thing/O
	var/particles/P
	var/matrix/M
	O = slot("obj"); O.particles = new /particles/one
	O = slot("screen"); O.icon = null; var/obj/screen/S = new(); S.screen_loc = "[O.x],[O.y]"; S.particles = new /particles/one; hud += S
	O = slot("mob"); O.icon = null; var/mob/dummy/D = new(O.loc); D.particles = new /particles/one
	O = slot("holder-scale2"); M = matrix(); M.Scale(2, 2); O.transform = M; O.particles = new /particles/ell
	O = slot("holder-turn90"); M = matrix(); M.Turn(90); O.transform = M; O.particles = new /particles/ell
	O = slot("holder-alpha"); O.alpha = 128; O.particles = new /particles/one
	O = slot("holder-color"); O.color = "#8080ff"; O.particles = new /particles/one
	O = slot("holder-pixel"); O.pixel_x = 8; O.pixel_y = 4; O.particles = new /particles/one
	O = slot("holder-kt"); O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.particles = new /particles/one
	O = slot("p-scale3"); P = new /particles/ell; P.scale = list(3, 3); O.particles = P
	O = slot("p-rot45"); P = new /particles/ell; P.rotation = 45; O.particles = P
	O = slot("p-rot90"); P = new /particles/ell; P.rotation = 90; O.particles = P
	O = slot("small-edge"); P = new /particles/small; P.position = list(10, 0); O.particles = P
	O = slot("small-edge-scale"); P = new /particles/small; P.position = list(6, 0); P.scale = list(2, 2); O.particles = P
	O = slot("small-edge-rot"); P = new /particles/small; P.position = list(8, 0); P.rotation = 45; O.particles = P
	O = slot("small-out"); P = new /particles/small; P.position = list(20, 0); O.particles = P
	O = slot("holder-layer"); O.particles = new /particles/one; var/obj/thing/C = new(O.loc); C.icon = 'big.png'; C.layer = 4
	O = slot("holder-layer-over"); O.layer = 5; O.particles = new /particles/one; C = new(O.loc); C.icon = 'big.png'; C.layer = 4
	O = slot("holder-filter"); O.filters += filter(type = "outline", size = 1, color = "#ffffff"); O.particles = new /particles/one
	O = slot("holder-iconless"); O.icon = null; O.particles = new /particles/one
	O = slot("p-transform"); P = new /particles/ell; P.transform = matrix(2, 0, 0, 0, 1, 0); O.particles = P
	O = slot("p-grow"); P = new /particles/ell; P.grow = list(0.001, 0.001); P.scale = list(2, 1); O.particles = P
	O = slot("plain")
/mob/observer
	invisibility = 101
	Login()
		..()
		loc = locate(9,6,1)
		client.screen += hud
		world.log << "READY [key]"

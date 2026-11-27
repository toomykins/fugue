// AISTATIC - an atom render_target and its render_source relays (vg's AI camera static: a client.screen obj with
// render_target "*aistatic" and /image(null, turf) relays on plane 22 in client.images). 15x15, view 3, the viewer
// at (8,8). The "*st" screen obj (screen_loc 4,4, over the viewer; plane 24) is the source; every turf with x < 8 carries a relay
// image. "nst" (a render_target without *) is a second screen obj at 7,7 relayed onto (10,10).
world
	name = "Fugue Aistatic"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 15
	maxy = 15

turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
mob/player
	icon = 'z.dmi'
	icon_state = "mob"

obj/src_st
	icon = 'z.dmi'
	icon_state = "st"
	render_target = "*st"
	screen_loc = "4,4"
	plane = 24
obj/src_nst
	icon = 'z.dmi'
	icon_state = "nst"
	render_target = "nst"
	screen_loc = "7,7"
	plane = 24
image/relay
	render_source = "*st"
	layer = 2
	plane = 22
image/nrelay
	render_source = "nst"
	layer = 2
	plane = 22

var/global/obj/src_st/st = new
var/global/obj/src_nst/nst = new
var/global/edge = 8

mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	client.screen += st
	client.screen += nst
	for(var/turf/T in world)
		if(T.x < edge)
			client.images += new /image/relay(null, T)
	client.images += new /image/nrelay(null, locate(10, 10, 1))
	world << "READY"

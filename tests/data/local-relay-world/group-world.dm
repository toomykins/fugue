// PASSMOUSE: the mouse over render_source relays (vg's AI camera static: an /image with render_source "*aistatic" and
// PASS_MOUSE on each obscured turf, the source a client.screen obj). 15x15, view 3, the viewer at (8,8). Sources on
// client.screen, plane 24, icon state "st" (only the top-left quadrant painted): "obscured" (render_target "*st",
// screen_loc 1,1), "s_two" ("*s2", 2,1, mouse_opacity 2), "s_zero" ("*s0", 3,1, mouse_opacity 0), "shown" ("nst", 7,7).
// Turf images on plane 22, rows y 5..10, every turf named t_<col>, a map obj "rock_<col>" at y 7 under them:
//   A x5  "*st", PASS_MOUSE (vg); y 5 "*s2" and y 10 "*s0" instead    B x6  "*st"
//   C x7  "*st", PASS_MOUSE, mouse_opacity 0    D x9  "*st", PASS_MOUSE, mouse_opacity 2
//   E x10 an icon image (state "img"), PASS_MOUSE, no render_source    F x11 "nst", PASS_MOUSE
//   G x8  y 5 "*sp" (source "s_px", screen_loc 5,2, pixel_x 5, pixel_y 3), y 9 "*so" ("s_off", 4:6,2:3), y 10 "*mp" (a map
//         obj "m_src" at 8,11), all PASS_MOUSE
// Every atom logs MouseEntered/MouseExited/Click; F2 (vgrule) turns on vg's rule: a source's MouseEntered turns
// show_popup_menus off and its MouseExited on. Examine (in view) gives every atom a verb for the right-click menu.
world
	name = "Fugue Pass Mouse"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 15
	maxy = 15
	maxz = 2

var/global/vgmode = 0
proc/note(t)
	text2file("[t]\n", "events.log")
	world.log << t
	world << t

atom/MouseEntered(location, control, params)
	note("ENTER [src] [params]")
atom/MouseExited(location, control, params)
	note("EXIT [src]")
atom/Click(location, control, params)
	note("CLICK [src] loc=[location] [params]")
	var/atom/L = location
	if(L) note("COORD source=[x],[y],[z] location=[L.x],[L.y],[L.z]")

turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
mob/player
	icon = 'z.dmi'
	icon_state = "mob"
obj/rock
	icon = 'z.dmi'
	icon_state = "rock"
	verb/poke()
		set src in view()
		note("POKE [src]")

obj/source
	icon = 'z.dmi'
	icon_state = "st"
	plane = 24
	MouseEntered(location, control, params)
		..()
		if(vgmode) usr.client.show_popup_menus = FALSE
	MouseExited(location, control, params)
		..()
		if(vgmode) usr.client.show_popup_menus = TRUE
obj/source/st
	name = "obscured"
	render_target = "*st"
	screen_loc = "1,1"
obj/source/s2
	name = "s_two"
	render_target = "*s2"
	screen_loc = "2,1"
	mouse_opacity = 2
obj/source/s0
	name = "s_zero"
	render_target = "*s0"
	screen_loc = "3,1"
	mouse_opacity = 0
obj/source/sp
	name = "s_px"
	render_target = "*sp"
	screen_loc = "5,2"
	pixel_x = 5
	pixel_y = 3
obj/source/so
	name = "s_off"
	render_target = "*so"
	screen_loc = "4:6,2:3"
obj/msrc
	name = "m_src"
	icon = 'z.dmi'
	icon_state = "st"
	render_target = "*mp"
obj/source/nst
	name = "shown"
	icon_state = "nst"
	render_target = "nst"
	screen_loc = "7,7"

image/relay
	appearance_flags = PASS_MOUSE
	layer = 2
	plane = 22
image/relay/A
	render_source = "*st"
image/relay/A2
	render_source = "*s2"
image/relay/A0
	render_source = "*s0"
image/relay/B
	render_source = "*st"
	appearance_flags = 0
image/relay/C
	render_source = "*st"
	mouse_opacity = 0
image/relay/D
	render_source = "*st"
	mouse_opacity = 2
image/relay/E
	icon = 'z.dmi'
	icon_state = "img"
image/relay/F
	render_source = "nst"
image/relay/G
	render_source = "*sp"
image/relay/G9
	render_source = "*so"
image/relay/G10
	render_source = "*mp"

var/global/list/cols = list("5" = "A", "6" = "B", "7" = "C", "9" = "D", "10" = "E", "11" = "F", "8" = "G")

mob/player/Login()
	..()
	loc = locate(8, 8, 1)
	for(var/p in typesof(/obj/source) - /obj/source)
		client.screen += new p
	for(var/turf/T in world)
		var/c = cols["[T.x]"]
		if(!c) continue
		T.name = "t_[c]"
		if(c == "G")
			if(T.y == 11) new /obj/msrc(T)
			if(T.y != 5 && T.y != 9 && T.y != 10) continue
		else if(T.y == 7) new /obj/rock(T).name = "rock_[c]"
		if(T.y < 5 || T.y > 10) continue
		var/p = text2path("/image/relay/[c]")
		if(c == "A" && T.y == 5) p = /image/relay/A2
		if(c == "A" && T.y == 10) p = /image/relay/A0
		if(c == "G" && T.y > 5) p = text2path("/image/relay/G[T.y]")
		client.images += new p(null, T)
	client.images += new /image/relay/A(null, locate(11,10,2))
	note("READY")
	spawn()
		while(client && !fexists("group.txt")) sleep(1)
		if(client)
			for(var/obj/source/st/S in client.screen)
				S.appearance_flags |= KEEP_TOGETHER
				var/image/I = image('z.dmi',icon_state="nst")
				I.pixel_x = -12
				I.pixel_y = 8
				S.overlays += I
			note("GROUP_READY")

mob/player/verb/vgrule()
	vgmode = !vgmode
	note("VGMODE [vgmode]")

mob/verb/examination(atom/A as mob|obj|turf in view(src))
	note("EXAMINE [A]")

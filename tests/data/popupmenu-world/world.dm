// POPUPMENU: when the right click opens the verb menu, and what the menu lists. passmouse-world's scene (15x15, view 3,
// the viewer at (8,8); vg's AI static: client.screen sources "*st" and PASS_MOUSE relays on turf images in columns A..G)
// with these changes: there is no Examine verb, so an atom has a verb only where this file gives it one (obj/rock's poke:
// the "rock_<col>" objs at y 7), turfs and obj/bare have none; every atom logs MouseDown/MouseUp/Click/MouseEntered/
// MouseExited with the value of client.show_popup_menus; F2 turns vg's rule on (a source's MouseEntered sets the flag
// FALSE, its MouseExited TRUE), F3/F4 set the flag FALSE/TRUE directly (a hidden verb, runnable while a button is held),
// F5 makes the rule's MouseEntered sleep 3 ticks first (a slow link: the flag follows the hover late).
// dreamseeker-rightclick.tsv is what stock DreamSeeker 516 under wine did against stock DreamDaemon 516.1684.
world
	name = "Fugue Pass Mouse"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	fps = 10
	maxx = 15
	maxy = 15

var/global/vgmode = 0
var/global/vgdelay = 0
proc/note(t)
	world.log << t
	world << t

atom/MouseEntered(location, control, params)
	note("ENTER [src] [params] flag=[usr.client.show_popup_menus]")
atom/MouseExited(location, control, params)
	note("EXIT [src] [params] flag=[usr.client.show_popup_menus]")
atom/Click(location, control, params)
	note("CLICK [src] loc=[location] [params]")
atom/MouseDown(location, control, params)
	note("DOWN [src] [params] flag=[usr.client.show_popup_menus]")
atom/MouseUp(location, control, params)
	note("UP [src] [params] flag=[usr.client.show_popup_menus]")

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
		if(vgmode)
			if(vgdelay) sleep(vgdelay)
			usr.client.show_popup_menus = FALSE
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
	new /obj/bare(locate(6, 8, 1)).name = "bare_B"
	new /obj/bare(locate(7, 8, 1)).name = "bare_C"
	note("READY")

mob/player/verb/vgrule()
	set hidden = 1
	vgmode = !vgmode
	note("VGMODE [vgmode]")

mob/player/verb/vgslow()
	set hidden = 1
	vgdelay = vgdelay ? 0 : 3
	note("VGDELAY [vgdelay]")
mob/player/verb/popoff()
	set hidden = 1
	usr.client.show_popup_menus = FALSE
	note("FLAG off")
mob/player/verb/popon()
	set hidden = 1
	usr.client.show_popup_menus = TRUE
	note("FLAG on")
obj/bare
	icon = 'z.dmi'
	icon_state = "rock"

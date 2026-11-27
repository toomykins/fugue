// Focused stock probe for plane-master screen_loc admission.
// Stage with prepare.py, then replace staged world.dm with this file and compile.
world
	name = "Fugue Master Placement"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 7
	maxy = 7
	fps = 10
proc/note(t)
	text2file("[t]\n", "events.log")
atom/Click(location, control, params)
	var/atom/L = location
	var/list/event = list("target"=name, "location"=L ? L.name : null, "locationXYZ"=L ? list(L.x,L.y,L.z) : null, "control"=control, "params"=params)
	note("DETAIL [json_encode(event)]")
turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
	name = "floor"
mob/player
	invisibility = 101
obj/part
	icon = 'z.dmi'
	icon_state = "st"
	plane = 1
	name = "red"
obj/master
	appearance_flags = PLANE_MASTER
	plane = 1
	name = "master"
	screen_loc = "1,1"
	mouse_opacity = 1
mob/player/Login()
	..()
	loc = locate(4,4,1)
	client.show_popup_menus = 0
	if(!locate(/obj/part) in world)
		new /obj/part(locate(4,4,1))
		var/obj/part/B = new(locate(5,4,1))
		B.name = "yellow"
		B.plane = 2
		B.icon_state = "nst"
	var/obj/master/M = new
	client.screen += M
	spawn() phases(M)
mob/player/proc/phases(obj/master/M)
	var/phase = ""
	while(client)
		var/p = file2text("phase.txt")
		if(p && p != phase)
			phase = p
			M.overlays = null
			M.underlays = null
			M.filters = null
			M.appearance_flags = PLANE_MASTER
			if(findtext(p,"group")) M.appearance_flags |= KEEP_TOGETHER
			M.screen_loc = findtext(p,"center") ? "CENTER,CENTER" : findtext(p,"offset") ? "2:7,2:5" : "1,1"
			if(findtext(p,"overlay") || findtext(p,"underlay"))
				var/image/I = image('z.dmi',icon_state="img")
				I.pixel_x = 32
				I.pixel_y = 32
				if(findtext(p,"apart")) I.appearance_flags = KEEP_APART
				if(findtext(p,"leafplane")) I.plane = 2
				if(findtext(p,"underlay")) M.underlays += I
				else M.overlays += I
			if(findtext(p,"nested"))
				var/image/N = image(null)
				N.appearance_flags = PLANE_MASTER
				N.plane = 2
				M.overlays += N
			if(findtext(p,"blur0")) M.filters = filter(type="blur",size=0)
			if(findtext(p,"blur1")) M.filters = filter(type="blur",size=1)
			note("READY [phase]")
		sleep(1)

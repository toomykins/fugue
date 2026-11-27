world
	name = "Fugue Master Tree"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 7
	maxy = 7
	fps = 10
proc/note(t)
	text2file("[t]\n", "events.log")
atom/Click(location, control, params)
	note("CLICK [src] loc=[location] [params]")
	if(fexists("live-details.txt"))
		var/atom/L = location
		var/list/event = list("target"=name, "targetRef"="\ref[src]", "location"=L ? L.name : null, "locationRef"=L ? "\ref[L]" : null, "locationXYZ"=L ? list(L.x,L.y,L.z) : null, "control"=control, "params"=params)
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
			M.color = null
			M.filters = null
			M.alpha = 255
			M.appearance_flags = PLANE_MASTER
			M.mouse_opacity = 1
			M.pixel_x = 0
			M.pixel_y = 0
			M.icon = null
			var/outline_size = findtext(p,"size2") ? 2 : 1
			var/radius_at = findtext(p,"-radius")
			if(radius_at)
				var/list/radius_tokens = splittext(copytext(p,radius_at+7),"-")
				outline_size = text2num(radius_tokens[1])
			var/child_size = outline_size
			var/child_at = findtext(p,"-childradius")
			if(child_at)
				var/list/child_tokens = splittext(copytext(p,child_at+12),"-")
				child_size = text2num(child_tokens[1])
			for(var/obj/part/B in world)
				if(B.name == "yellow") B.pixel_x = findtext(p,"overlap") ? -24 : 0
			if(findtext(p,"-offset"))
				M.pixel_x = 7
				M.pixel_y = 5
			if(findtext(p,"ownicon"))
				M.icon = 'z.dmi'
				M.icon_state = "rock"
			var/image/I = image('z.dmi',icon_state="img")
			I.pixel_x = 32
			I.pixel_y = 32
			if(findtext(p,"leafplane")) I.plane = 2
			if(findtext(p,"-apart")) I.appearance_flags = KEEP_APART
			if(findtext(p,"leafoffset"))
				I.pixel_x = -7
				I.pixel_y = 9
			if(findtext(p,"leafalpha")) I.alpha = 128
			if(findtext(p,"leafcenter"))
				I.pixel_x = 96
				I.pixel_y = 96
			if(findtext(p,"overlay"))
				if(findtext(p,"under")) M.underlays += I
				else M.overlays += I
			if(findtext(p,"nested"))
				var/image/N = image(null)
				N.appearance_flags = PLANE_MASTER
				N.plane = 2
				N.color = "#808080"
				if(findtext(p,"childoutline")) N.filters = filter(type="outline", size=child_size, color="#ff00ff")
				if(findtext(p,"same")) N.plane = 1
				if(findtext(p,"childzero")) N.mouse_opacity = 0
				if(findtext(p,"childgroup")) N.appearance_flags |= KEEP_TOGETHER
				if(findtext(p,"childapart")) N.appearance_flags |= KEEP_APART
				if(findtext(p,"deep"))
					var/image/D = image(null)
					D.appearance_flags = PLANE_MASTER
					D.plane = 2
					D.color = "#808080"
					N.overlays += D
				M.overlays += N
			if(findtext(p,"-outline")) M.filters = filter(type="outline", size=outline_size, color="#ff00ff")
			if(findtext(p,"tint")) M.color = "#808080"
			if(findtext(p,"zero") && !findtext(p,"childzero")) M.mouse_opacity = 0
			if(findtext(p,"-alpha")) M.alpha = 128
			if(findtext(p,"-clear")) M.alpha = 0
			if(findtext(p,"-group")) M.appearance_flags |= KEEP_TOGETHER
			if(findtext(p,"-removed")) client.screen -= M
			else if(!(M in client.screen)) client.screen += M
			note("READY [phase]")
		sleep(1)

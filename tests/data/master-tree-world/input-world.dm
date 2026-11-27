// Five focused plane-input/outline controls with synthetic assets.
world
	name = "Fugue Focused Plane Input"
	view = 3
	maxx = 7
	maxy = 7
	fps = 10
	turf = /turf/floor
	mob = /mob/player
proc/note(t)
	text2file("[t]\n", "events.log")
atom/Click(location,control,params)
	note("CLICK [src] loc=[location] [params]")
turf/floor
	name = "floor"
	icon = 'z.dmi'
	icon_state = "floor"
obj/part
	name = "red"
	icon = 'z.dmi'
	icon_state = "st"
	plane = 1
obj/master
	name = "master"
	appearance_flags = PLANE_MASTER
	plane = 1
	screen_loc = "1,1"
	mouse_opacity = 1
obj/producer
	name = "producer"
	icon = 'z.dmi'
	icon_state = "st"
	plane = 4
	screen_loc = "1,1"
mob/player
	invisibility = 101
mob/player/Login()
	..()
	name = "TestGuest"
	loc = locate(4,4,1)
	client.show_popup_menus = 0
	if(!(locate(/obj/part) in world))
		new /obj/part(locate(4,4,1))
		var/obj/part/B = new(locate(5,4,1))
		B.name = "yellow"
		B.plane = 2
		B.icon_state = "nst"
	var/obj/master/M = new
	var/obj/producer/P = new
	var/obj/producer/Q = new
	Q.name = "replacement"
	Q.screen_loc = "2,1"
	spawn() phases(M,P,Q)
mob/player/proc/phases(obj/master/M,obj/producer/P,obj/producer/Q)
	var/last = ""
	while(client)
		var/raw = file2text("phase.txt")
		if(raw && raw != last)
			last = raw
			var/list/tokens = splittext(raw,":")
			var/p = tokens[tokens.len]
			client.images = null
			client.screen -= P
			client.screen -= Q
			if(!(M in client.screen)) client.screen += M
			M.overlays = null
			M.filters = null
			M.transform = null
			P.render_target = "*local"
			if(p == "forward")
				client.screen -= M
				client.screen += P
				Q.icon = null
				Q.render_target = "*forward"
				Q.render_source = "*local"
				client.screen += Q
				var/image/R = image(null,locate(2,2,1))
				R.plane = 4
				R.appearance_flags = PASS_MOUSE
				R.render_source = "*forward"
				client.images += R
			else if(p == "plane-rotate")
				M.transform = matrix(0,-1,0,1,0,0)
			else
				var/image/I = image('z.dmi',icon_state="img")
				I.pixel_x = 32
				I.pixel_y = 32
				if(p == "edge-outline")
					I.pixel_x = -8
					I.pixel_y = 200
					I.alpha = 128
					M.filters = list(filter(type="outline",size=4.25,color="#ff00ff"),filter(type="blur",size=0.5))
				else if(p == "filtered-reverse")
					M.filters = list(filter(type="blur",size=1),filter(type="outline",size=2,color="#ff00ff"))
				else if(p == "filtered-input")
					M.filters = list(filter(type="outline",size=2,color="#ff00ff"),filter(type="blur",size=1))
				M.overlays += I
			note("READY [raw]")
		sleep(1)

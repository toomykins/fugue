// Synthetic outer-shadow and plane-input controls; no game assets.
world
	name = "Fugue Shadow Fixture"
	view = 3
	maxx = 7
	maxy = 7
	fps = 10
	turf = /turf/floor
	mob = /mob/player
proc/note(t)
	text2file("[t]\n", "events.log")
proc/event(kind, atom/A, atom/B, atom/L, atom/OL, control, oc, params)
	var/list/e = list("kind"=kind,"target"=A ? A.name : null,"targetRef"=A ? "\ref[A]" : null,"over"=B ? B.name : null,"location"=L ? L.name : null,"locationXYZ"=L ? list(L.x,L.y,L.z) : null,"overLocation"=OL ? OL.name : null,"control"=control,"overControl"=oc,"params"=params)
	note("EVENT [json_encode(e)]")
atom/MouseEntered(location,control,params)
	event("Enter",src,null,location,null,control,null,params)
atom/MouseExited(location,control,params)
	event("Exit",src,null,location,null,control,null,params)
client
	Click(atom/A,l,c,p)
		event("Click",A,null,l,null,c,null,p)
	MouseDown(atom/A,l,c,p)
		event("Down",A,null,l,null,c,null,p)
	MouseUp(atom/A,l,c,p)
		event("Up",A,null,l,null,c,null,p)
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
	spawn() phases(M)
mob/player/proc/phases(obj/master/M)
	var/last = ""
	while(client)
		var/raw = file2text("phase.txt")
		if(raw && raw != last)
			last = raw
			var/list/tokens = splittext(raw,":")
			var/p = tokens[tokens.len]
			client.images = null
			for(var/obj/part/C in client.screen)
				client.screen -= C
				del C
			if(!(M in client.screen)) client.screen += M
			M.overlays = null
			M.filters = null
			M.alpha = 255
			M.render_target = null
			for(var/obj/part/B in world)
				B.alpha = 255
				B.pixel_x = 0
				B.pixel_y = 0
			if(findtext(p,"shadow"))
				var/list/c = json_decode(file2text("cases.json"))[p]
				for(var/obj/part/B in world)
					B.alpha = c["source_alpha"]
					B.pixel_x = (c["edge"] && B.name == "red") ? -100 : 0
					B.pixel_y = (c["edge"] && B.name == "red") ? 100 : 0
				var/list/operations = list()
				if(c["before"]) operations += filter(type="blur",size=0.5)
				for(var/i in 1 to (c["repeat"] ? c["repeat"] : 1))
					operations += filter(type="drop_shadow",size=c["size"],offset=c["offset"],x=c["x"],y=c["y"],color=c["color"])
				if(c["after"]) operations += filter(type="outline",size=1.5,color="#80c02080")
				M.filters = operations
				if(c["target"])
					M.render_target = "*shadow"
					var/obj/part/C = new
					C.icon = null
					C.name = "consumer"
					C.plane = 3
					C.screen_loc = "1,1"
					C.render_source = "*shadow"
					C.appearance_flags = PASS_MOUSE
					client.screen += C
			else
				var/image/I = image('z.dmi',icon_state="img")
				I.pixel_x = 32
				I.pixel_y = 32
				M.overlays += I
			note("READY [raw]")
		sleep(1)

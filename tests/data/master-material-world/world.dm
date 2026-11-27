// Synthetic master-material evidence. Stage beside generated local-relay z.dmi.
world
	name = "Fugue Composition Matrix"
	view = 3
	maxx = 7
	maxy = 7
	fps = 20
	turf = /turf/floor
	mob = /mob/player
proc/note(t)
	text2file("[t]\n", "events.log")
proc/event(kind, atom/A, atom/L, control, params)
	var/list/e = list("kind"=kind,"target"=A ? A.name : null,"targetRef"=A ? "\ref[A]" : null,"over"=null,"location"=L ? L.name : null,"locationXYZ"=L ? list(L.x,L.y,L.z) : null,"overLocation"=null,"control"=control,"overControl"=null,"params"=params)
	note("EVENT [json_encode(e)]")
client/Click(atom/A,l,c,p)
	event("Click",A,l,c,p)
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
	loc = locate(4,4,1)
	client.show_popup_menus = 0
	if(!(locate(/obj/part) in world))
		new /obj/part(locate(4,4,1))
		var/obj/part/B = new(locate(5,4,1))
		B.name = "yellow"
		B.plane = 2
		B.icon_state = "nst"
	spawn() phases()
mob/player/proc/phases()
	var/last = ""
	while(client)
		var/raw = file2text("phase.txt")
		if(raw && raw != last)
			last = raw
			var/list/tokens = splittext(raw,":")
			var/p = tokens[tokens.len]
			for(var/obj/O in client.screen)
				client.screen -= O
				del O
			var/obj/master/M = new
			client.screen += M
			var/image/I = image('z.dmi',icon_state="img")
			I.pixel_x = 32
			I.pixel_y = 32
			var/list/mat = list(0.5,0.125,0,0, 0,0.75,0.125,0, 0.125,0,0.25,0, 0,0,0,1, 0,0,0,0)
			if(p != "control") M.color = mat
			if(findtext(p,"white")) M.color = list(0,0,0,0, 0,0,0,0, 0,0,0,0, 1,1,1,1, 0,0,0,0)
			if(findtext(p,"offset")) M.color = list(0.5,0,0,0, 0,0.75,0,0, 0,0,0.25,0, 0,0,0,0.5, 0.1,0.2,0.3,0.25)
			if(findtext(p,"childmatrix")) I.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0.1,0,0,0)
			if(findtext(p,"childscalar")) I.color = "#80c0ff"
			if(findtext(p,"parent-scalar")) M.color = "#80c0ff"
			if(findtext(p,"leafplane")) I.plane = 3
			if(findtext(p,"group")) M.appearance_flags |= KEEP_TOGETHER
			if(findtext(p,"apart")) I.appearance_flags |= KEEP_APART
			if(findtext(p,"resetcolor")) I.appearance_flags |= RESET_COLOR
			if(findtext(p,"resetalpha"))
				M.alpha = 128
				I.appearance_flags |= RESET_ALPHA
			if(findtext(p,"partial")) I.alpha = 128
			if(findtext(p,"add")) M.blend_mode = BLEND_ADD
			if(findtext(p,"multiply")) M.blend_mode = BLEND_MULTIPLY
			if(findtext(p,"subtract")) M.blend_mode = BLEND_SUBTRACT
			if(findtext(p,"childadd")) I.blend_mode = BLEND_ADD
			if(findtext(p,"nested"))
				var/image/N = image(null)
				N.appearance_flags = PLANE_MASTER
				N.plane = findtext(p,"same") ? 1 : 2
				N.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0.1,0,0,0)
				M.overlays += N
			if(findtext(p,"under")) M.underlays += I
			else M.overlays += I
			if(findtext(p,"parallax"))
				M.plane = 0
				M.color = list(0,0,0,0, 0,0,0,0, 0,0,0,0, 1,1,1,1, 0,0,0,0)
				var/obj/master/P = new
				P.plane = 1
				P.blend_mode = BLEND_MULTIPLY
				var/list/opaque = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0, 0,0,0,1)
				P.color = opaque
				client.screen += P
				animate(P,color=opaque,time=100)
			if(findtext(p,"animate"))
				animate(M,color=list(0.8,0.1,0,0, 0,0.25,0.2,0, 0.1,0,0.75,0, 0,0,0,1, 0.1,0,0.1,0),time=100)
			note("READY [raw]")
		sleep(1)

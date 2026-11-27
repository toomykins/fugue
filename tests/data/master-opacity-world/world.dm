// Tiny independent oracle: no overlays, transforms, filters, targets or attachments.
world
	name = "Fugue Composition Matrix"
	view = 3
	maxx = 7
	maxy = 7
	fps = 10
	turf = /turf/floor
	mob = /mob/player
proc/note(t)
	text2file("[t]\n", "events.log")
proc/event(kind, atom/A, atom/L, control, params)
	note("EVENT [json_encode(list("kind"=kind,"target"=A ? A.name : null,"targetRef"=A ? "\ref[A]" : null,"location"=L ? L.name : null,"locationXYZ"=L ? list(L.x,L.y,L.z) : null,"control"=control,"params"=params))]")
client/Click(atom/A,l,c,p)
	event("Click",A,l,c,p)
client/MouseDown(atom/A,l,c,p)
	event("Down",A,l,c,p)
client/MouseUp(atom/A,l,c,p)
	event("Up",A,l,c,p)
atom/verb/inspect()
	set src in view()
	event("Menu",src,null,null,null)
turf/floor
	name = "floor"
	icon = 'z.dmi'
	icon_state = "floor"
obj/member
	name = "member"
	icon = 'z.dmi'
	icon_state = "st"
	plane = 1
obj/master
	name = "master"
	appearance_flags = PLANE_MASTER
	plane = 1
	screen_loc = "1,1"
	mouse_opacity = 2
mob/player
	invisibility = 101
mob/player/Login()
	..()
	loc = locate(4,4,1)
	client.show_popup_menus = 0
	if(!(locate(/obj/member) in world)) new /obj/member(locate(2,2,1))
	var/obj/master/M = new
	client.screen += M
	spawn()
		var/last = ""
		while(client)
			var/raw = file2text("phase.txt")
			if(raw && raw != last)
				last = raw
				var/list/tokens = splittext(raw,":")
				var/p = tokens[tokens.len]
				M.mouse_opacity = p == "control" ? 1 : p == "zero" ? 0 : 2
				client.show_popup_menus = fexists("popup.txt")
				note("READY [raw]")
			sleep(1)

// Probe: does a filtered plane master act on its plane without a usable screen_loc?
// Level 1 is the original 7x7 room (the tiles past 7 have no icon); level 2 is a 15x15 room of striped columns with a
// few objects, for phases that end in "|wide": the eye and the part stand at its centre, 8,8, so turfs lie past the view.
// Every atom logs Click() with its name, icon-x/y and place, and has one verb of its own type, poke_<kind>, so that an
// entry of the right-click menu tells which kind of atom it is.
world
	name = "Master Loc Probe"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
	maxz = 2
	fps = 10
world/New()
	..()
	for(var/turf/T in block(locate(1,1,1), locate(15,15,1)))
		if(T.x > 7 || T.y > 7) T.icon = null
	for(var/turf/T in block(locate(1,1,2), locate(15,15,2)))
		// columns told apart by their icon, three by three
		T.icon_state = list("floor", "mob", "rock")[T.x % 3 + 1]
	// a turf and an object on the default plane coloured by a colour matrix (a constant colour), in the view
	var/turf/C = locate(10,10,2)
	C.color = list(0,0,0, 0,0,0, 0,0,0, 1,0,1)
	var/obj/K = new /obj/nst(locate(10,6,2)); K.color = list(0,0,0, 0,0,0, 0,0,0, 1,0.5,0)
proc/note(t)
	text2file("[t]\n", "events.log")
atom/Click(location, control, params)
	var/list/p = params2list(params)
	note("CLICK [name] x=[p["icon-x"]] y=[p["icon-y"]] at=[x],[y]")
turf/floor
	icon = 'z.dmi'
	icon_state = "floor"
	verb/poke_floor()
		set src in world
		note("VERB floor")
mob/player
	invisibility = 101
obj/part
	icon = 'z.dmi'
	icon_state = "st"
	plane = 1
	verb/poke_part()
		set src in world
		note("VERB part")
obj/master
	plane = 1
	verb/poke_master()
		set src in world
		note("VERB master")
// The objects of level 2, all named "obj" as plain objects are.
obj/nst
	name = "obj"
	icon = 'z.dmi'
	icon_state = "nst"
	verb/poke_nst()
		set src in world
		note("VERB nst")
obj/rock
	name = "obj"
	icon = 'z.dmi'
	icon_state = "rock"
	verb/poke_rock()
		set src in world
		note("VERB rock")
obj/img
	name = "obj"
	icon = 'z.dmi'
	icon_state = "img"
	verb/poke_img()
		set src in world
		note("VERB img")
// White squares (w.dmi: "w" a whole tile, "ws" 16x16 at the top left) for the phases ending in "wide-fx".
obj/fx
	icon = 'w.dmi'
	icon_state = "ws"
	verb/poke_fx()
		set src in world
		note("VERB [name]")
mob/player/Login()
	..()
	loc = locate(4,4,1)
	if(!locate(/obj/part) in locate(4,4,1))
		new /obj/part(locate(4,4,1))
		new /obj/part(locate(8,8,2))
		// on level 2, objects on the default plane in the view (6,8) and past it (12,8 and 4,8), and one on plane 1
		// past it (12,9)
		new /obj/nst(locate(6,8,2))
		new /obj/nst(locate(12,8,2))
		new /obj/rock(locate(4,8,2))
		var/obj/O = new /obj/img(locate(12,9,2)); O.plane = 1
		// and, moved half a tile right, on the default plane and on plane 1 at the view's left edge (5,9 and 5,10)
		O = new /obj/nst(locate(5,9,2)); O.pixel_x = 16
		O = new /obj/nst(locate(5,10,2)); O.pixel_x = 16; O.plane = 1
	note("READY login")
	spawn() phases()
// One white square on level 2 for "wide-fx" phases, drawn in one of the ways an atom can take.
proc/fx(x, y, name, state = "ws")
	var/obj/fx/F = new(locate(x, y, 2))
	F.name = name
	F.icon_state = state
	return F
proc/extras()
	. = list()
	var/obj/fx/F
	F = fx(7, 6, "plain", "w"); F.color = "#ff8080"; . += F                        // a plain colour
	F = fx(6, 10, "swap", "w"); F.color = list(0,0,1, 0,1,0, 1,0,0); F.icon = 'z.dmi'; F.icon_state = "st"; . += F
	F = fx(9, 7, "outline"); F.filters = filter(type="outline", size=1, color="#00ffff"); . += F
	F = fx(7, 10, "kt"); F.appearance_flags = KEEP_TOGETHER                         // with an overlay to its right
	var/image/I = image('w.dmi', icon_state="ws"); I.pixel_x = 16; I.color = "#ff80ff"; F.overlays += I; . += F
	F = fx(9, 10, "alpha", "w"); F.alpha = 128; . += F
	F = fx(9, 6, "add"); F.blend_mode = BLEND_ADD; . += F
	F = fx(7, 8, "scaled"); F.transform = matrix(2, 0, 0, 0, 2, 0); . += F             // a matrix without a turn
	// turned by 45 degrees: in the view's left column, and in the column added on the right by 8,1
	var/matrix/M = matrix(); M.Turn(45)
	F = fx(5, 7, "turned", "w"); F.transform = M; F.color = "#80ff80"; . += F
	F = fx(12, 6, "turnedfar", "w"); F.transform = M; F.color = "#80ff80"; . += F
// The phase comes from phase.txt (collect.py), or once from world.params "case" (stockprobe.py pair) or "set"
// (stockprobe.py record), where "_" stands
// for "|" and "~" for ":" so that the case name can be a file name.
mob/player/proc/phases()
	var/phase = ""
	var/obj/master/M
	var/list/added = list()
	var/given = world.params["case"] || world.params["set"]
	if(given) given = replacetext(replacetext(given, "_", "|"), "~", ":")
	while(client)
		var/p = given ? given : file2text("phase.txt")
		if(p && p != phase)
			phase = p
			if(M) client.screen -= M
			M = null
			for(var/atom/A in added) del A
			added = list()
			if(p != "none")
				M = new
				M.appearance_flags = PLANE_MASTER
				var/list/w = splittext(p, "|")
				var/loc = w[1]
				if(loc == "null") M.screen_loc = null
				else if(loc == "empty") M.screen_loc = ""
				else M.screen_loc = loc
				var/fx = w[2]
				// "wide" moves to level 2; "wide-fx" adds the white squares of extras() there
				var/wide = w.len >= 3 && findtext(w[3], "wide") == 1
				var/turf/where = wide ? locate(8,8,2) : locate(4,4,1)
				if(src.loc != where) src.loc = where
				if(w.len >= 3 && w[3] == "wide-fx") added = extras()
				// a fourth field resizes the window and its map control, e.g. "8,1|obj|wide|288x288", and a fifth sets
				// the map's icon-size, e.g. "8,1|obj|wide|336x336|48"
				if(w.len >= 4) winset(src, null, "main.size=[w[4]];map.size=[w[4]]")
				if(w.len >= 5) winset(src, "map", "icon-size=[w[5]]")
				if(fx == "outline") M.filters = filter(type="outline", size=1, color="#ffffff")
				if(fx == "blur") M.filters = filter(type="blur", size=2)
				if(fx == "shadow") M.filters = filter(type="drop_shadow", x=4, y=-4, size=0, offset=0, color="#00ff00")
				if(fx == "alpha0") M.alpha = 0
				if(fx == "green") M.color = "#00ff00"
				if(fx == "kt") M.appearance_flags |= KEEP_TOGETHER
				if(fx == "kt-alpha0")
					M.appearance_flags |= KEEP_TOGETHER
					M.alpha = 0
				if(fx == "ov")
					var/image/I = image('z.dmi', icon_state="img")
					M.overlays += I
				if(fx == "ov-outline")
					var/image/I = image('z.dmi', icon_state="img")
					M.overlays += I
					M.filters = filter(type="outline", size=1, color="#ffffff")
				if(fx == "obj")
					M.appearance_flags = 0
					M.icon = 'z.dmi'
					M.icon_state = "img"
				if(fx == "rt")
					M.render_target = "*cap"
					var/obj/R = new
					R.render_source = "*cap"
					R.screen_loc = "2,2"
					R.plane = 5
					R.tag = "relay"
					client.screen += R
				client.screen += M
			else
				for(var/obj/O in client.screen) client.screen -= O
				if(src.loc != locate(4,4,1)) src.loc = locate(4,4,1)
			note("READY [phase]")
		sleep(1)

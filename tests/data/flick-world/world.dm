// flick() on an obj, a mob, a turf, a screen object, with an icon file, on a new obj and on objs outside the view.
world
	name = "Flick probe"
	maxx = 18
	maxy = 12
	maxz = 2
	view = "18x12"
	fps = 10
	mob = /mob/observer
var/list/cases = list()
var/list/hud = list()
var/list/pictures = list()
var/ready = 0
/obj/thing
	icon = 'anim.dmi'
	icon_state = "still"
	layer = 5
/mob/dummy
	icon = 'anim.dmi'
	icon_state = "still"
	layer = 5
/obj/screen
	icon = 'anim.dmi'
	icon_state = "still"
/proc/place(i)
	return locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1)
/proc/fire()
	sleep(20)
	world.log << "FIRE"
	var/i = 0
	var/obj/thing/O
	// 0 obj, state
	O = new(place(i++)); flick("flash", O)
	// 1 obj that existed before
	flick("flash", cases[1])
	i++
	// 2 mob
	flick("flash", cases[2])
	i++
	// 3 turf
	var/turf/T = place(i++)
	T.icon = 'anim.dmi'; T.icon_state = "still"
	flick("flash", T)
	// 4 icon file
	flick('other.dmi', cases[3])
	i++
	// 5 screen object
	flick("flash", hud[1])
	i++
	// 6 obj flicked outside the view, then moved in
	var/obj/thing/Far = cases[4]
	flick("flash", Far)
	// 7 obj made outside the view, flicked, then moved in
	var/obj/thing/Far2 = new(locate(3, 3, 2))
	flick("flash", Far2)
	// 8 the same flick twice
	flick("flash", cases[5]); flick("flash", cases[5])
	// 9 flick, then another state
	flick("flash", cases[6]); flick("other", cases[6])
	// 10 the client's own mob stays invisible; a mob made now
	var/mob/dummy/M = new(place(10)); flick("flash", M)
	// 11 a turf that was plain
	flick("flash", place(11))
	// 12 a KEEP_TOGETHER obj at half alpha with a client image, flicked (kt-image)
	flick("flash", cases[7])
	flick("flash", cases[8])
	sleep(20)
	Far.loc = place(6)
	Far2.loc = place(7)
	world.log << "MOVED"
world/New()
	..()
	world.log = file("events.log")
	for(var/turf/t in block(locate(1,1,1), locate(18,12,2))) t.icon = 'floor.png'
	cases += new /obj/thing(place(1))
	cases += new /mob/dummy(place(2))
	cases += new /obj/thing(place(4))
	cases += new /obj/thing(locate(2, 2, 2))
	cases += new /obj/thing(place(8))
	cases += new /obj/thing(place(9))
	var/obj/thing/K = new(place(12))
	K.appearance_flags = KEEP_TOGETHER
	K.alpha = 128
	var/image/KI = image('other.dmi', K, "still")
	KI.pixel_x = 8
	pictures += KI
	cases += K
	var/obj/thing/K2 = new(place(13))
	K2.appearance_flags = KEEP_TOGETHER
	K2.alpha = 128
	KI = image('other.dmi', K2, "still")
	KI.pixel_x = 8
	pictures += KI
	var/obj/thing/K3 = new(place(14))
	K3.appearance_flags = KEEP_TOGETHER
	K3.alpha = 128
	K3.layer = 3
	KI = image('other.dmi', K3, "still")
	KI.pixel_x = 8
	pictures += KI
	cases += K3
	var/obj/thing/K4 = new(place(15))
	K4.appearance_flags = KEEP_TOGETHER
	K4.alpha = 128
	K4.layer = 7
	KI = image('other.dmi', K4, "still")
	KI.pixel_x = 8
	pictures += KI
	var/obj/thing/K5 = new(place(16))
	K5.appearance_flags = KEEP_TOGETHER
	K5.alpha = 128
	KI = image('other.dmi', K5, "still")
	KI.pixel_x = 8
	KI.layer = 5
	pictures += KI
	var/obj/thing/K6 = new(place(17))
	K6.appearance_flags = KEEP_TOGETHER
	K6.alpha = 128
	K6.layer = 7
	KI = image('other.dmi', K6, "still")
	KI.pixel_x = 8
	KI.layer = 6
	pictures += KI
	var/turf/P = place(11)
	P.icon = 'anim.dmi'; P.icon_state = "still"
	var/obj/screen/S = new()
	S.screen_loc = "17,1"
	hud += S
/mob/observer
	invisibility = 101
	Login()
		..()
		loc = locate(9,6,1)
		client.screen += hud
		client.images += pictures
		world.log << "READY [key]"
		if(++ready == text2num(world.params["clients"] || "2")) spawn() fire()

// Probe: what a runtime winset of a window's or a control's size or place does, and what winget reads back after it.
// Login sends one size winset at once; a click on the map runs the steps for that client, each logging the wingets a
// second after its winset.
world
	name = "Winset Size Probe"
	view = 3
	turf = /turf/floor
	mob = /mob/player
	maxx = 15
	maxy = 15
proc/note(t)
	text2file("[t]\n", "events.log")
turf/floor
	icon = 'floor.png'
turf/tile
	icon = 'tile.png'
mob/player
	invisibility = 101
	Login()
		..()
		loc = locate(8,8,1)
		var/turf/T = locate(8,8,1)
		T.icon = 'tile.png'
		winset(src, "main", "size=240x240")
		note("READY [key]")
client/var/ran = 0
client/Click()
	if(ran) return
	ran = 1
	spawn() steps()
client/proc/report(label)
	var/t = "[label]:"
	for(var/id in list("main", "map", "fixed", "half", "kid", "pane", "pl", "rt"))
		for(var/p in list("size", "pos"))
			t += " [id].[p]=[winget(src, id, p)]"
	t += " main.inner-size=[winget(src, "main", "inner-size")] main.outer-size=[winget(src, "main", "outer-size")]"
	t += " main.is-maximized=[winget(src, "main", "is-maximized")] map.view-size=[winget(src, "map", "view-size")]"
	t += " loose.size=[winget(src, "loose", "size")] ll.size=[winget(src, "ll", "size")]"
	t += " flt.size=[winget(src, "flt", "size")] fl.size=[winget(src, "fl", "size")]"
	note(t)
client/proc/phase(label, control, params)
	winset(src, control, params)
	sleep(10)
	report(label)
client/proc/steps()
	report("start")
	phase("win288", null, "main.size=288x288")
	phase("map160", null, "map.size=160x160")
	phase("win224", null, "main.size=224x224")
	phase("win288b", null, "main.size=288x288")
	phase("children", null, "fixed.size=64x64;half.size=100x20")
	phase("map0x0", null, "map.size=0x0")
	phase("map100x0", null, "map.size=100x0")
	phase("reset", null, "main.size=224x224;map.size=224x224")
	phase("fixture", null, "main.size=288x288;map.size=288x288")
	phase("ctlwin", "main", "size=256x256")
	phase("ctlmap", "map", "size=128x128")
	// a zero extent on a control without anchors, then a resize
	phase("fixed0x0", null, "fixed.size=0x0")
	phase("win224c", null, "main.size=224x224")
	// pos alone and with size, without anchors and with both, then a resize; then the main window's pos
	phase("fixedpos", null, "fixed.size=32x32;fixed.pos=10,20")
	phase("halfpos", null, "half.pos=8,100")
	phase("halfboth", null, "half.pos=4,150;half.size=50x30")
	phase("win288c", null, "main.size=288x288")
	phase("mainpos", null, "main.pos=100,120")
	// a control created at runtime, a resize, then its size and pos, then a resize
	phase("rtnew", "rt", "parent=main;type=map;pos=20,40;size=100x100;anchor1=0,0;anchor2=100,100")
	phase("win224d", null, "main.size=224x224")
	phase("rtsize", null, "rt.size=80x60")
	phase("rtpos", null, "rt.pos=10,10")
	phase("win256", null, "main.size=256x256")
	// a CHILD and the pane it shows, a pane shown nowhere, a floating window that is not shown
	phase("kidsize", null, "kid.size=96x96")
	phase("panesize", null, "pane.size=40x40")
	phase("pl0x0", null, "pl.size=0x0")
	phase("loosesize", null, "loose.size=70x30")
	phase("fltsize", null, "flt.size=120x80")
	// size while maximized, then back
	phase("max", null, "main.is-maximized=true")
	phase("maxsize", null, "main.size=300x300")
	phase("unmax", null, "main.is-maximized=false")
	note("DONE")

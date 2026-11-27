// Independent one-property phases. All assets are synthetic; commands are acknowledged.
world
	name = "Relay Gate Diagnosis"
	view = "5x3"
	maxx = 11
	maxy = 11
	fps = 20
	turf = /turf/floor
	mob = /mob/player
proc/note(t)
	text2file("[t]\n", "events.log")
turf/floor
	name = "floor"
	icon = 'floor.png'
obj/ink
	name = "source ink"
	icon = 'ink.png'
	plane = 60
obj/master
	appearance_flags = PLANE_MASTER
	plane = 60
	screen_loc = "1,1"
	render_target = "*relay-diagnosis"
obj/relay
	name = "relay"
	plane = 70
	screen_loc = "1,1"
	render_source = "*relay-diagnosis"
obj/ack
	icon = 'ack.png'
	plane = 80
	screen_loc = "1,1"
	mouse_opacity = 0
mob/player
	invisibility = 101
mob/player/Login()
	..()
	loc = locate(6,6,1)
	client.show_popup_menus = 0
	if(!(locate(/obj/ink) in world)) new /obj/ink(loc)
	var/obj/ink/S = locate(/obj/ink) in world
	var/obj/ink/O = new(loc)
	O.name = "overlap ink"
	O.icon = null
	O.layer = 4
	var/obj/master/M = new
	var/obj/relay/R = new
	var/obj/ack/A = new
	client.screen += M
	client.screen += R
	client.screen += A
	note("LOGIN [client.key]")
	spawn()
		var/last = ""
		while(client)
			var/raw = file2text("phase.txt")
			if(raw && raw != last)
				last = raw
				var/list/tokens = splittext(raw, ":")
				var/p = tokens[tokens.len]
				R.appearance_flags = 0
				R.transform = null
				R.filters = null
				R.vis_flags = 0
				R.maptext = ""
				R.overlays = null
				R.underlays = null
				R.pixel_x = 0
				R.render_source = "*relay-diagnosis"
				R.icon = null
				R.color = null
				R.alpha = 255
				R.blend_mode = BLEND_DEFAULT
				M.alpha = 255
				M.color = null
				M.filters = null
				S.alpha = 255
				S.color = null
				S.icon = 'ink.png'
				O.icon = null
				O.alpha = 255
				A.color = "#00ff00"
				switch(p)
					if("together")
						R.appearance_flags = KEEP_TOGETHER
						A.color = "#ffff00"
					if("transform")
						var/matrix/T = matrix()
						T.Scale(0.5, 0.5)
						R.transform = T
						A.color = "#8030ff"
					if("blur-zero")
						R.filters = filter(type="blur", size=0)
						A.color = "#ff8030"
					if("blur-active")
						R.filters = filter(type="blur", size=1)
						A.color = "#30a0ff"
					if("vis-id")
						R.vis_flags = VIS_INHERIT_ID
						A.color = "#ff30a0"
					if("apart")
						R.appearance_flags = KEEP_APART
						A.color = "#a0ff30"
					if("maptext")
						R.maptext = "<div style='text-align:right;color:#ffffff;font-size:16px'>R</div>"
						A.color = "#a030ff"
					if("overlay", "underlay", "blur-overlay", "group-blur-overlay")
						var/image/I = image('child.png')
						I.pixel_x = 68
						I.pixel_y = 32
						if(p != "underlay")
							R.overlays += I
							A.color = "#ff3030"
							if(p == "blur-overlay" || p == "group-blur-overlay")
								R.filters = filter(type="blur", size=1)
								A.color = "#ff80ff"
								if(p == "group-blur-overlay")
									R.appearance_flags = KEEP_TOGETHER
									A.color = "#80ff80"
						else
							R.underlays += I
							A.color = "#30ffff"
					if("group-overlay", "group-blur-origin", "group-blur-nosource", "group-blur-baseicon", "group-blur-underlay")
						R.appearance_flags = KEEP_TOGETHER
						var/image/I = image('child.png')
						I.pixel_x = 68
						I.pixel_y = 32
						if(p == "group-blur-origin")
							I.pixel_x = 0
							I.pixel_y = 0
							A.color = "#ff8040"
						else if(p == "group-blur-nosource")
							R.render_source = null
							A.color = "#40ff80"
						else if(p == "group-blur-baseicon")
							R.icon = 'ink.png'
							A.color = "#4080ff"
						else if(p == "group-blur-underlay")
							A.color = "#ff4080"
						else
							A.color = "#8040ff"
						if(p == "group-blur-underlay")
							R.underlays += I
						else
							R.overlays += I
						if(p != "group-overlay")
							R.filters = filter(type="blur", size=1)
					if("pixel-offset")
						R.pixel_x = 8
						A.color = "#ffffff"
				// Coherent relay matrix: paired topology, noncommuting material/effect
				// and source-surface geometry discriminators (not a Cartesian product).
				if(copytext(p,1,3) == "m-")
					var/grouped = findtext(p, "-group")
					var/kind = copytext(p,3,grouped ? grouped : 0)
					if(grouped) R.appearance_flags = KEEP_TOGETHER
					var/image/I = image('child.png')
					I.pixel_x = 68
					I.pixel_y = 32
					R.overlays += I
					var/list/matrix_names = list("tint", "tint-group", "matrix", "matrix-group", "add", "subtract-group", "multiply", "child-material-group", "blur", "blur-group", "outline-group", "chain", "chain-group", "reverse-group", "scale", "scale-group", "rotate", "rotate-group", "translate", "negative-group", "expand-group", "reverse-rotate-group", "wide-group", "matrix-alpha-group", "matrix-generate-group", "scale-material-group", "scale-blur-group")
					A.color = rgb(24 + 8*matrix_names.Find(copytext(p,3)),176,128)
					switch(kind)
						if("tint")
							R.color = "#8040ff"
							R.alpha = 128
						if("matrix")
							R.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
						if("add")
							R.blend_mode = BLEND_ADD
							R.alpha = 128
						if("subtract")
							R.blend_mode = BLEND_SUBTRACT
						if("multiply")
							R.blend_mode = BLEND_MULTIPLY
						if("child-material")
							R.overlays = null
							I.color = "#ff0080"
							I.alpha = 65
							I.blend_mode = BLEND_ADD
							R.overlays += I
							R.filters = filter(type="blur",size=1)
						if("blur")
							R.filters = filter(type="blur",size=2.5)
						if("outline")
							R.filters = filter(type="outline",size=3,color="#40ff80")
						if("chain")
							R.filters = list(filter(type="outline",size=2,color="#40ff80"),filter(type="blur",size=1))
							R.color = "#8040ff"
							R.alpha = 128
						if("reverse")
							R.filters = list(filter(type="blur",size=1),filter(type="outline",size=2,color="#40ff80"))
						if("scale")
							R.transform = matrix(0.5,0,0,0,0.5,0)
						if("rotate")
							R.transform = matrix(0,-1,0,1,0,0)
						if("translate")
							R.transform = matrix(1,0,12,0,1,8)
						if("negative")
							R.transform = matrix(1,0,-12,0,1,-8)
						if("expand")
							R.transform = matrix(2,0,0,0,2,0)
						if("reverse-rotate")
							R.transform = matrix(0,1,0,-1,0,0)
						if("wide")
							R.transform = matrix(0.5,0,0,0,1,0)
						if("matrix-alpha")
							R.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0)
						if("matrix-generate")
							R.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0.1,0.2,0,0.25)
						if("scale-material", "scale-blur")
							R.transform = matrix(0.5,0,0,0,0.5,0)
							R.color = "#8040ff"
							R.alpha = 128
							if(kind == "scale-blur") R.filters = filter(type="blur",size=2.5)
				// Source-capture discriminators: no relay child can mask a hole.
				if(copytext(p,1,3) == "s-")
					S.icon = 'partial.png'
					var/list/source_names = list("base", "master-alpha", "member-alpha", "master-zero", "member-zero", "master-rgb", "master-generate", "master-erase", "blur", "outline", "chain", "combined", "member-rgb", "overlap", "member-generate", "master-threshold", "reverse", "axis", "axis-alpha", "axis-rgb", "axis-zero-blur")
					var/source_index = source_names.Find(copytext(p,3))
					A.color = source_index <= 18 ? rgb(32 + 12*source_index,112,208) : rgb(32 + 12*(source_index-18),128,208)
					switch(copytext(p,3))
						if("master-alpha") M.alpha = 128
						if("member-alpha") S.alpha = 128
						if("master-zero") M.alpha = 0
						if("member-zero") S.alpha = 0
						if("master-rgb")
							M.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
						if("member-rgb")
							S.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
						if("master-generate")
							M.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0.1,0.2,0,0.25)
						if("master-erase")
							M.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0, 0,0,0,0)
						if("blur", "combined")
							M.filters = filter(type="blur",size=1)
							if(p == "s-combined")
								R.filters = filter(type="blur",size=2.5)
								R.color = "#8040ff"
								R.alpha = 128
								R.appearance_flags = KEEP_TOGETHER
						if("outline")
							M.filters = filter(type="outline",size=2,color="#40ff80")
							R.appearance_flags = KEEP_TOGETHER
						if("chain")
							M.filters = list(filter(type="outline",size=2,color="#40ff80"),filter(type="blur",size=1))
						if("member-generate")
							S.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0.1,0.2,0,0.25)
						if("master-threshold")
							M.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,2, 0,0,0,-1)
						if("reverse")
							S.icon = 'ink.png'
							M.filters = list(filter(type="blur",size=1),filter(type="outline",size=2,color="#40ff80"))
						if("axis", "axis-alpha", "axis-rgb", "axis-zero-blur")
							if(p == "s-axis") M.filters = filter(type="outline",size=2,color="#40ff80")
							if(p == "s-axis" || p == "s-axis-alpha") M.alpha = 128
							if(p == "s-axis-rgb") M.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
							if(p == "s-axis-zero-blur") M.filters = filter(type="blur",size=0)
							R.transform = matrix(0.5,0,0,0,0.5,0)
							R.appearance_flags = KEEP_TOGETHER
						if("overlap")
							O.icon = 'child.png'
							O.alpha = 0
				// Capture precision/basis controls. Topology and raster vary
				// independently of source-master material/filter processing.
				if(copytext(p,1,3) == "g-" || copytext(p,1,3) == "p-")
					var/list/pixel_names = list("g-neutral","g-alpha","g-rgb","g-zero-blur","g-partial","g-child","g-child-alpha","g-repeat","p-neutral","p-master","p-member","p-member-alpha","p-member-rgb","p-double")
					A.color = rgb(24+12*pixel_names.Find(p),144,224)
					var/list/generate = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0.1,0.2,0,0.25)
					if(copytext(p,1,3) == "g-")
						R.appearance_flags = KEEP_TOGETHER
						R.transform = matrix(0.5,0,0,0,0.5,0)
						if(p == "g-alpha" || p == "g-child-alpha") M.alpha = 128
						if(p == "g-rgb") M.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
						if(p == "g-zero-blur") M.filters = filter(type="blur",size=0)
						if(p == "g-partial") S.icon = 'partial.png'
						if(p == "g-child" || p == "g-child-alpha")
							var/image/I = image('child.png')
							I.pixel_x = 68
							I.pixel_y = 32
							R.overlays += I
					else
						S.icon = 'precision.png'
						if(p != "p-neutral" && p != "p-member") M.color = generate
						if(p == "p-member") S.color = generate
						if(p == "p-member-alpha") S.alpha = 101
						if(p == "p-member-rgb") S.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0,0,0,0)
						if(p == "p-double") R.color = generate
						if(p == "p-low-master" || p == "p-low-member" || p == "p-overlap")
							var/list/boundary_names = list("p-low-master","p-low-member","p-overlap")
							A.color = rgb(24+12*boundary_names.Find(p),144,224)
							M.color = null
							if(p == "p-overlap")
								O.icon = 'precision.png'
								O.alpha = 101
								M.color = generate
							else
								R.color = generate
								if(p == "p-low-master") M.alpha = 17
								else S.alpha = 17
						if(p == "p-material-master" || p == "p-material-member")
							A.color = p == "p-material-master" ? rgb(36,144,224) : rgb(48,144,224)
							M.color = null
							var/list/mat = list(0.731,0,0,0, 0,0.731,0,0, 0,0,0.731,0, 0,0,0,0, 0.093,0.093,0.093,0.07)
							if(p == "p-material-master") M.color = mat
							else S.color = mat
							R.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0, 0,0,0,1)
				// Precision-model controls; constant patches isolate material
				// storage from raster/capture arithmetic. Input is unchanged.
				if(copytext(p,1,3) == "q-")
					var/list/qnames = list("q-capture","q-opaque","q-master","q-member","q-const-master","q-const-member","q-const-off-master","q-tie-master","q-tie-member","q-overlap")
					var/list/qfollow = list("q-round-member","q-round-master","q-old-constant")
					var/qi = qnames.Find(p)
					if(!qi) qi = qfollow.Find(p)
					A.color = rgb(24+12*qi,144,224)
					S.icon = 'precision.png'
					R.color = list(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,1)
					var/list/qmat
					if(p == "q-opaque" || p == "q-master" || p == "q-member")
						qmat = list(0.731,0,0,0,0,0.731,0,0,0,0,0.731,0,0,0,0,0,0.093,0.093,0.093,0.07)
						if(p == "q-opaque") qmat[20] = 1
					if(findtext(p,"const-"))
						qmat = list(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.1043137255,0.1360784314,0.1568627451,0.13)
						if(p == "q-const-off-master") qmat[20] = 0.1305
					if(findtext(p,"tie-"))
						qmat = list(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.499,0.5,0.501,1)
						R.color = list(32,0,0,0,0,32,0,0,0,0,32,0,0,0,0,0,-15.5,-15.5,-15.5,1)
					if(findtext(p,"round-"))
						qmat = list(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.1945098039,0.6956862745,0.6968627451,0.07)
					if(p == "q-old-constant")
						qmat = list(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0.1974285745,0.6983593823,0.6731587373,0.07)
					if(qmat)
						if(findtext(p,"member")) S.color = qmat
						else M.color = qmat
					if(p == "q-overlap")
						O.icon = 'precision.png'
						O.alpha = 101
				sleep(2)
				note("READY [raw] KEY [client.key]")
			sleep(1)
client/Click(atom/A, location, control, params)
	note("CLICK [json_encode(list("target"=A ? A.name : null,"location"=location ? "[location]" : null,"control"=control,"params"=params))]")

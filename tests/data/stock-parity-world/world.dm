// Probe sets checked against stock DreamSeeker/DreamDaemon: one set per run, chosen with -params "set=<name>" (the names
// are in world/New(); README.md has what each one established). Cases sit on a 3-tile grid.
world
	name = "Stock parity probe"
	maxx = 18
	maxy = 12
	maxz = 1
	view = "18x12"
	fps = 10
	mob = /mob/observer
var/list/cases = list()
var/list/pictures = list()
var/list/hud = list()
/obj/thing
	icon = 'small.png'
	layer = 3
/obj/bar
	icon = 'cover.png'
	layer = 4
/obj/content
	icon = 'big.png'
/obj/screen
	icon = 'small.png'
	layer = 3
/obj/member
	icon = 'shape.png'
/proc/slot(label)
	var/i = length(cases)
	var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
	O.name = label
	cases += O
	world.log << "CASE [label]"
	return O
// A red bar over the left of the tile at `low` and a green bar over its top at `high`: what hides the yellow box tells
// the layer it was drawn at.
/proc/covered(atom/A, low = 4, high = 6)
	var/obj/bar/C = new(A.loc)
	C.layer = low
	var/obj/bar/T = new(A.loc)
	T.icon = 'top.png'
	T.layer = high
/proc/picture(atom/A)
	var/image/I = image('shape.png', A)
	pictures += I
	return I
/proc/screen_at(obj/thing/O, loc_text = null)
	var/obj/screen/S = new()
	S.name = O.name
	S.screen_loc = loc_text ? loc_text : "[O.x],[O.y]"
	hud += S
	O.icon = null
	return S
/proc/group(obj/thing/O)
	O.icon = 'big.png'
	O.appearance_flags = KEEP_TOGETHER
	O.alpha = 128
/proc/set_images_groups()
	var/obj/thing/O
	var/image/I
	var/obj/screen/S
	var/obj/member/V
	// client.image layers against objects at 4 and 6 on the same tile.
	O = slot("img-default"); covered(O); picture(O)
	O = slot("img-float"); covered(O); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("img-neg2"); covered(O); I = picture(O); I.layer = -2
	O = slot("img-explicit"); covered(O); I = picture(O); I.layer = 3.5
	O = slot("img-float-tie5"); covered(O, 5, 5); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("img-float-parent7"); O.layer = 7; covered(O); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("turf-img-float"); covered(O); I = picture(O.loc); I.layer = FLOAT_LAYER
	O = slot("turf-img-float-tie5"); covered(O, 5, 5); I = picture(O.loc); I.layer = FLOAT_LAYER
	O = slot("hud-img-float"); S = screen_at(O); I = picture(S); I.layer = FLOAT_LAYER
	var/obj/screen/C = new(); C.icon = 'cover.png'; C.layer = 4; C.screen_loc = S.screen_loc; hud += C
	C = new(); C.icon = 'top.png'; C.layer = 6; C.screen_loc = S.screen_loc; hud += C
	// Images on a KEEP_TOGETHER parent at alpha 128.
	O = slot("img-kt"); group(O); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("img-kt-apart"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.appearance_flags = KEEP_APART
	O = slot("img-kt-plane"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.plane = 2
	O = slot("img-kt-override"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.override = 1
	O = slot("img-matrix-parent"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0); I = picture(O); I.layer = FLOAT_LAYER
	// Groups.
	O = slot("kt-overlay-plane"); group(O); O.color = "#ff8080"; I = image('shape.png'); I.plane = 2; O.overlays += I
	O = slot("kt-add-first"); group(O); O.alpha = 255; I = image('adda.png'); I.blend_mode = BLEND_ADD; O.overlays += I; O.overlays += image('addb.png')
	O = slot("kt-add-last"); group(O); O.alpha = 255; O.overlays += image('addb.png'); I = image('adda.png'); I.blend_mode = BLEND_ADD; O.overlays += I
	O = slot("kt-inset"); group(O); O.alpha = 255; I = image('shape.png'); I.pixel_x = 12; I.blend_mode = BLEND_INSET_OVERLAY; O.overlays += I
	// A screen object's vis_contents.
	O = slot("hud-vis"); S = screen_at(O); V = new(); S.vis_contents += V
	O = slot("hud-vis-pixel"); S = screen_at(O); S.pixel_x = 8; S.pixel_y = 4; V = new(); S.vis_contents += V
	O = slot("hud-vis-locoffset"); S = screen_at(O, "[O.x]:8,[O.y]:4"); V = new(); S.vis_contents += V
	O = slot("hud-vis-range"); S = screen_at(O, "[O.x],[O.y] to [O.x + 1],[O.y]"); V = new(); S.vis_contents += V
	O = slot("hud-vis-iconless"); S = screen_at(O); S.icon = null; V = new(); S.vis_contents += V
	O = slot("hud-vis-member-pixel"); S = screen_at(O); V = new(); V.pixel_x = 8; S.vis_contents += V
// More of the same questions: what an image takes from a plain translucent or coloured target, and from a group.
/proc/set_images_groups_more()
	var/obj/thing/O
	var/image/I
	var/obj/screen/S
	O = slot("img-alpha"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("img-alpha-override"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER; I.override = 1
	O = slot("img-color-override"); O.icon = 'big.png'; O.color = "#ff8080"; I = picture(O); I.layer = FLOAT_LAYER; I.override = 1
	O = slot("img-alpha-apart"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER; I.appearance_flags = KEEP_APART
	O = slot("img-kt-layer2"); group(O); I = picture(O); I.layer = 2
	O = slot("img-kt-turn"); group(O); var/matrix/M = matrix(); M.Turn(90); O.transform = M; I = picture(O); I.layer = FLOAT_LAYER; I.pixel_x = 6
	O = slot("img-kt-two"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I = image('adda.png', O); I.layer = FLOAT_LAYER; pictures += I
	O = slot("img-kt-matrix"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("hud-img-kt"); S = screen_at(O); S.icon = 'big.png'; S.appearance_flags = KEEP_TOGETHER; S.alpha = 128; I = picture(S); I.layer = FLOAT_LAYER
	O = slot("inset-flat"); O.icon = 'big.png'; I = image('shape.png'); I.pixel_x = 12; I.blend_mode = BLEND_INSET_OVERLAY; O.overlays += I
	O = slot("kt-nested-plane"); group(O); var/mutable_appearance/A = new(); A.icon = 'adda.png'; A.appearance_flags = KEEP_TOGETHER; A.alpha = 64; I = image('shape.png'); I.plane = 2; A.overlays += I; O.overlays += A
	O = slot("img-kt-mob-alpha"); group(O); O.alpha = 64; I = picture(O); I.layer = FLOAT_LAYER; I.alpha = 128
	O = slot("matrix-alpha"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0)
	O = slot("kt-matrix-alpha"); group(O); O.alpha = 255; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0)
	O = slot("kt-matrix-alpha-overlay"); group(O); O.alpha = 255; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.overlays += image('shape.png')
// BLEND_ADD inside a group: as an overlay or an underlay, alone or before an ordinary overlay, opaque or translucent.
/proc/added(alpha = 255, under = FALSE, after = FALSE, apart = FALSE)
	var/obj/thing/O = slot("kt-add[under ? "-under" : ""][after ? "-then" : ""][alpha < 255 ? "-half" : ""][apart ? "-flat" : ""]")
	group(O)
	if(apart) O.appearance_flags = 0
	O.alpha = alpha
	var/image/I = image('adda.png')
	I.blend_mode = BLEND_ADD
	if(under)
		O.underlays += I
		if(after) O.underlays += image('addb.png')
	else
		O.overlays += I
		if(after) O.overlays += image('addb.png')
/proc/set_color_matrix_alpha()
	var/obj/thing/O
	var/image/I
	O = slot("base"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("const-alpha"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,-0.5); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("with-alpha128"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("non-kt"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("apart"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER; I.appearance_flags = KEEP_APART
	O = slot("img-alpha"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER; I.alpha = 128
	O = slot("no-image"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255
	O = slot("overlay"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; O.overlays += image('shape.png')
	O = slot("identity-rgb"); group(O); O.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("rgba-string"); group(O); O.color = "#ffffff80"; O.alpha = 255; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("explicit-layer"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 255; I = picture(O); I.layer = 6
	O = slot("matrix-rgb-only"); group(O); O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("flat-alpha-after"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0); O.alpha = 128
	O = slot("kt-alpha-after"); group(O); O.alpha = 255; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0); O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER
	O = slot("flat-alpha-before"); O.icon = 'big.png'; O.alpha = 128; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	O = slot("flat-rgba-before"); O.icon = 'big.png'; O.color = "#ff804080"; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	O = slot("flat-alpha-after-half"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,0.5, 0,0,0,0); O.alpha = 128
/proc/set_animated_groups()
	var/obj/thing/O
	var/image/I
	O = slot("still"); group(O); I = picture(O); I.layer = FLOAT_LAYER
	O = slot("anim-pixel"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(O, pixel_x = 1, time = 6000)
	O = slot("anim-alpha"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(O, alpha = 127, time = 6000)
	O = slot("anim-color"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(O, color = "#fffffe", time = 6000)
	var/matrix/M = matrix(); M.Translate(1, 0)
	O = slot("anim-transform"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(O, transform = M, time = 6000)
	O = slot("anim-done"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(O, pixel_x = 4, time = 5)
	O = slot("anim-matrix-parent"); O.icon = 'big.png'; O.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0); I = picture(O); I.layer = FLOAT_LAYER; animate(O, pixel_x = 1, time = 6000)
	O = slot("anim-flat"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER; animate(O, pixel_x = 1, time = 6000)
	O = slot("anim-transform-noimg"); group(O); animate(O, transform = M, time = 6000)
	O = slot("anim-transform-flat"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER; animate(O, transform = M, time = 6000)
	var/matrix/T = matrix(); T.Turn(90)
	O = slot("anim-turn-done"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.pixel_x = 6; animate(O, transform = T, time = 5)
	O = slot("anim-turn-overlay"); group(O); O.overlays += image('shape.png'); animate(O, transform = M, time = 6000)
	var/matrix/A = matrix(); A.Turn(45)
	var/matrix/B = matrix(); B.Turn(135)
	O = slot("anim-turn45-overlay"); group(O); O.overlays += image('shape.png'); O.transform = A; animate(O, transform = B, time = 60000)
	O = slot("anim-turn45-image"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.pixel_x = 6; O.transform = A; animate(O, transform = B, time = 60000)
	var/matrix/S = matrix(); S.Scale(2, 1.5)
	O = slot("anim-scale-image"); group(O); I = picture(O); I.layer = FLOAT_LAYER; O.transform = S; animate(O, transform = matrix(), time = 60000)
	O = slot("anim-turn45-flat"); O.icon = 'big.png'; O.overlays += image('shape.png'); O.transform = A; animate(O, transform = B, time = 60000)
	// Animated before any client was sent the image: its animation arrives just before it.
	O = slot("anim-image"); group(O); I = picture(O); I.layer = FLOAT_LAYER; animate(I, pixel_x = 12, time = 6000)
	O = slot("anim-image-flat"); O.icon = 'big.png'; O.alpha = 128; I = picture(O); I.layer = FLOAT_LAYER; animate(I, pixel_x = 12, time = 6000)
/proc/set_plane_alpha_mask(n)
	world.log << "CASE [world.params["set"]]"
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1)))
		var/obj/content/B = new(t)
		B.plane = 5
	var/obj/screen/M = new()
	M.icon = null
	M.plane = 5
	M.appearance_flags = PLANE_MASTER
	M.screen_loc = "1,1"
	hud += M
	var/obj/content/K = new(locate(5, 5, 1))
	K.icon = 'mask.png'
	K.plane = 0
	K.render_target = "*src"
	var/x = 0
	var/y = 0
	var/f = 0
	if(n == 2) { x = 64; y = 32 }
	if(n == 3) K.icon = 'mask96.png'
	if(n == 4)
		K.loc = null
		var/obj/screen/S = new()
		S.icon = 'mask.png'
		S.render_target = "*src"
		S.screen_loc = "3,3"
		hud += S
	if(n == 5) K.render_target = "src"
	if(n == 6) f = MASK_INVERSE
	if(n == 7) { K.icon = 'mask96.png'; K.pixel_x = 16; K.pixel_y = 8 }
	M.filters += filter(type = "alpha", render_source = (n == 5 ? "src" : "*src"), x = x, y = y, flags = f)
/proc/set_alpha_masks()
	var/obj/thing/O
	var/obj/screen/M
	var/obj/content/K = new(locate(1, 1, 1))
	K.icon = 'mask.png'
	K.render_target = "*src"
	O = slot("atom-icon"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png'))
	O = slot("atom-icon-x8"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png'), x = 8)
	O = slot("atom-icon-y8"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png'), y = 8)
	O = slot("atom-icon-inverse"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png'), flags = MASK_INVERSE)
	O = slot("atom-icon-swap"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png'), flags = MASK_SWAP)
	O = slot("atom-source"); O.icon = 'big.png'; O.filters += filter(type = "alpha", render_source = "*src")
	O = slot("atom-source-x8"); O.icon = 'big.png'; O.filters += filter(type = "alpha", render_source = "*src", x = 8)
	O = slot("kt-icon"); group(O); O.alpha = 255; O.overlays += image('shape.png'); O.filters += filter(type = "alpha", icon = icon('mask.png'))
	O = slot("atom-icon96"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask96.png'))
	O = slot("atom-icon-outline"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png')); O.filters += filter(type = "outline", size = 1, color = "#ffffff")
	O = slot("atom-outline-icon"); O.icon = 'big.png'; O.filters += filter(type = "outline", size = 1, color = "#ffffff"); O.filters += filter(type = "alpha", icon = icon('mask.png'))
	M = masked("plane-icon"); M.filters += filter(type = "alpha", icon = icon('mask.png'))
	M = masked("plane-icon-x8"); M.filters += filter(type = "alpha", icon = icon('mask.png'), x = 8, y = 8)
	O = slot("atom-icon-turn"); O.icon = 'big.png'; var/matrix/T = matrix(); T.Turn(90); O.transform = T; O.filters += filter(type = "alpha", icon = icon('mask.png'))
	O = slot("plain")
/proc/set_repeated_filters()
	var/obj/thing/O
	var/obj/screen/M
	var/list/swap = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	var/list/dim = list(0.5,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	O = slot("two-outlines"); O.icon = 'big.png'; O.filters += filter(type = "outline", size = 1, color = "#ff0000"); O.filters += filter(type = "outline", size = 1, color = "#ffffff")
	O = slot("three-outlines"); O.icon = 'big.png'; O.filters += filter(type = "outline", size = 1, color = "#ff0000"); O.filters += filter(type = "outline", size = 1, color = "#ffffff"); O.filters += filter(type = "outline", size = 1, color = "#00ff00")
	O = slot("two-blurs"); O.icon = 'big.png'; O.filters += filter(type = "blur", size = 1); O.filters += filter(type = "blur", size = 2)
	O = slot("blur-outline-blur"); O.icon = 'big.png'; O.filters += filter(type = "blur", size = 1); O.filters += filter(type = "outline", size = 1, color = "#ff0000"); O.filters += filter(type = "blur", size = 1)
	O = slot("two-shadows"); O.icon = 'big.png'; O.filters += filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ff0000"); O.filters += filter(type = "drop_shadow", x = -3, y = 3, size = 0, color = "#00ff00")
	O = slot("two-colors"); O.icon = 'big.png'; O.filters += filter(type = "color", color = swap); O.filters += filter(type = "color", color = dim)
	O = slot("three-colors"); O.icon = 'big.png'; O.filters += filter(type = "color", color = swap); O.filters += filter(type = "color", color = dim); O.filters += filter(type = "color", color = swap)
	O = slot("two-masks"); O.icon = 'big.png'; O.filters += filter(type = "alpha", icon = icon('mask.png')); O.filters += filter(type = "alpha", icon = icon('mask.png'), y = 8)
	M = masked("plane-two-outlines"); M.filters += filter(type = "outline", size = 1, color = "#ff0000"); M.filters += filter(type = "outline", size = 1, color = "#ffffff")
	M = masked("plane-two-blurs"); M.filters += filter(type = "blur", size = 1); M.filters += filter(type = "blur", size = 2)
	M = masked("plane-three-colors"); M.filters += filter(type = "color", color = swap); M.filters += filter(type = "color", color = dim); M.filters += filter(type = "color", color = swap)
	O = slot("plain")
/obj/kid
	icon = 'small.png'
	vis_flags = VIS_INHERIT_ID
/proc/kid(obj/thing/O)
	var/obj/kid/K = new()
	K.pixel_x = 16
	O.vis_contents += K
	return K
// A sweep of combinations: images, overlays and filters in groups, blend modes, render targets, animations.
/proc/set_sweep()
	var/obj/thing/O
	var/image/I
	var/matrix/T = matrix(); T.Turn(45)
	var/matrix/S = matrix(); S.Scale(1.5, 1)
	var/matrix/E = matrix(); E.Turn(46)
	var/red = filter(type = "outline", size = 1, color = "#ff0000")
	var/list/swap = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	O = slot("image-outline-anim-target"); O.icon = 'big.png'; I = picture(O); I.layer = FLOAT_LAYER; I.filters = red; animate(O, pixel_x = 1, time = 60000)
	O = slot("image-turn-anim-target"); O.icon = 'big.png'; I = picture(O); I.layer = FLOAT_LAYER; I.transform = T; animate(O, transform = E, time = 60000)
	O = slot("image-kt-own"); O.icon = 'big.png'; I = picture(O); I.layer = FLOAT_LAYER; I.appearance_flags = KEEP_TOGETHER; I.alpha = 128; I.overlays += image('adda.png')
	O = slot("image-kt-own-on-kt"); group(O); I = picture(O); I.layer = FLOAT_LAYER; I.appearance_flags = KEEP_TOGETHER; I.alpha = 128; I.overlays += image('adda.png')
	O = slot("image-vis"); O.icon = 'big.png'; I = picture(O); I.layer = FLOAT_LAYER; var/obj/kid/V = new(); V.pixel_x = 16; I.vis_contents += V
	O = slot("image-override-kt"); group(O); O.overlays += image('adda.png'); I = picture(O); I.override = 1
	O = slot("overlay-kt-in-kt"); group(O); var/mutable_appearance/A = new(); A.icon = 'shape.png'; A.appearance_flags = KEEP_TOGETHER; A.alpha = 128; A.overlays += image('adda.png'); O.overlays += A
	O = slot("overlay-outline-in-kt"); group(O); A = new(); A.icon = 'shape.png'; A.filters = red; O.overlays += A
	O = slot("overlay-outline-flat"); O.icon = 'big.png'; A = new(); A.icon = 'shape.png'; A.filters = red; O.overlays += A
	O = slot("overlay-turn-in-kt-turn"); group(O); O.transform = T; A = new(); A.icon = 'shape.png'; A.transform = T; O.overlays += A
	O = slot("overlay-apart-in-kt-turn"); group(O); O.transform = T; A = new(); A.icon = 'shape.png'; A.appearance_flags = KEEP_APART; O.overlays += A
	O = slot("overlay-reset-in-kt-turn"); group(O); O.transform = T; A = new(); A.icon = 'shape.png'; A.appearance_flags = RESET_TRANSFORM; O.overlays += A
	O = slot("underlay-kt-turn"); group(O); O.transform = T; O.underlays += image('shape.png', pixel_x = 10)
	O = slot("kt-scale-anim-alpha"); group(O); O.transform = S; O.overlays += image('shape.png'); animate(O, alpha = 127, time = 60000)
	O = slot("kt-outline-anim-turn"); group(O); O.filters = red; O.transform = T; animate(O, transform = E, time = 60000)
	O = slot("kt-two-filters-turn"); group(O); O.filters = red; O.filters += filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#ffffff"); O.transform = T
	O = slot("blend-add-turn"); O.icon = 'big.png'; O.blend_mode = BLEND_ADD; O.transform = T
	O = slot("kt-blend-mult"); group(O); O.blend_mode = BLEND_MULTIPLY; O.overlays += image('shape.png')
	O = slot("kt-plane-turn"); group(O); O.plane = 3; O.transform = T; O.overlays += image('shape.png')
	O = slot("target-turn"); O.icon = 'big.png'; O.render_target = "rt1"; O.transform = T
	O = slot("target-outline"); O.icon = 'big.png'; O.render_target = "rt2"; O.filters = red
	O = slot("kt-target"); group(O); O.render_target = "rt3"; O.overlays += image('shape.png')
	O = slot("matrix-outline-turn"); O.icon = 'big.png'; O.color = swap; O.filters = red; O.transform = T
	O = slot("plain")
// One relay of an atom's render target each: 4 to 7 on screen objects, 8, 10 to 12 on objs, 9 an icon-less one.
/proc/set_relay(n)
	var/obj/thing/O
	var/matrix/T = matrix(); T.Turn(45)
	var/red = filter(type = "outline", size = 1, color = "#ff0000")
	var/obj/screen/H2
	var/obj/content/Src2 = new(locate(1, 1, 1)); Src2.icon = 'shape.png'; Src2.render_target = "*relaysrc"
	if(n == 4) { O = slot("hud-relay"); H2 = screen_at(O); H2.render_source = "*relaysrc" }
	if(n == 5) { O = slot("hud-relay-turn"); H2 = screen_at(O); H2.render_source = "*relaysrc"; H2.transform = T }
	if(n == 6) { O = slot("hud-relay-color"); H2 = screen_at(O); H2.render_source = "*relaysrc"; H2.color = "#ff8080"; H2.alpha = 128 }
	if(n == 7) { O = slot("hud-relay-outline"); H2 = screen_at(O); H2.render_source = "*relaysrc"; H2.filters = red }
	if(n == 8) { O = slot("obj-relay"); O.render_source = "*relaysrc" }
	if(n == 10) { O = slot("obj-relay-turn"); O.render_source = "*relaysrc"; O.transform = T }
	if(n == 11) { O = slot("obj-relay-color"); O.render_source = "*relaysrc"; O.color = "#ff8080"; O.alpha = 128 }
	if(n == 12) { O = slot("obj-relay-outline"); O.render_source = "*relaysrc"; O.filters = red }
	if(n == 9) { O = slot("hud-relay-noicon"); H2 = screen_at(O); H2.icon = null; H2.render_source = "*relaysrc" }
/obj/dark
	icon = 'floor.png'
	plane = 10
/proc/pm(p)
	var/obj/screen/M = new()
	M.icon = null
	M.plane = p
	M.appearance_flags = PLANE_MASTER
	M.screen_loc = "1,1"
	hud += M
	return M
/proc/set_lighting(n)
	world.log << "CASE [world.params["set"]]"
	var/obj/screen/M
	// game objects on plane 0, a lighting plane of grey squares over every turf, lit brighter in a patch
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1)))
		if((t.x + t.y) % 3 == 0) { var/obj/thing/B = new(t); B.icon = 'big.png' }
		var/obj/dark/D = new(t)
		D.color = (t.x > 6 && t.x < 12) ? "#ffffff" : "#606060"
	var/obj/thing/E = new(locate(4, 6, 1)); E.icon = 'shape.png'; E.plane = 11   // an emissive
	M = pm(10); M.blend_mode = BLEND_MULTIPLY
	if(n == 1) { M = pm(11); M.alpha = 0 }
	if(n == 2)
		M.filters += filter(type = "alpha", render_source = "*emis", flags = MASK_INVERSE)
		M = pm(11); M.render_target = "*emis"
	if(n == 3)
		M.render_target = "*light"; M.blend_mode = 0
		var/obj/screen/R = new(); R.icon = null; R.screen_loc = "1,1"; R.render_source = "*light"; R.plane = 20; R.blend_mode = BLEND_MULTIPLY; R.color = "#ffc0c0"; hud += R
		M = pm(11); M.alpha = 0
	if(n == 4)
		M.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 0.1,0.1,0.1,0)
		M = pm(11); M.blend_mode = BLEND_ADD
		M = pm(0); M.filters += filter(type = "blur", size = 1)
	if(n == 6 || n == 7)
		for(var/obj/dark/D2 in world) D2.alpha = 128
		if(n == 7) { M.alpha = 200; M.color = "#ffe0c0" }
		M = pm(11); M.alpha = 0
	if(n == 5)
		M = pm(11); M.alpha = 0
		M = pm(0); M.render_target = "*game"
		var/obj/screen/R2 = new(); R2.icon = null; R2.screen_loc = "1,1"; R2.render_source = "*game"; R2.plane = 5; R2.color = "#80ff80"; R2.alpha = 200; hud += R2
		R2 = new(); R2.icon = null; R2.screen_loc = "1,1"; R2.render_source = "*game"; R2.plane = 6; R2.blend_mode = BLEND_ADD; R2.alpha = 80; R2.pixel_x = 4; hud += R2
/proc/tslot(label)
	var/obj/thing/O = slot(label)
	var/turf/U = O.loc
	del(O)
	U.icon = 'big.png'
	return U
/proc/set_blend_reset_layers()
	var/obj/thing/O
	var/obj/thing/C
	var/image/I
	var/mutable_appearance/A
	var/matrix/T = matrix(); T.Turn(90)
	O = slot("multiply"); O.icon = 'big.png'; O.blend_mode = BLEND_MULTIPLY
	O = slot("subtract"); O.icon = 'big.png'; O.blend_mode = BLEND_SUBTRACT
	O = slot("add-alpha"); O.icon = 'big.png'; O.blend_mode = BLEND_ADD; O.alpha = 128
	O = slot("overlay-multiply"); O.icon = 'big.png'; I = image('shape.png'); I.blend_mode = BLEND_MULTIPLY; O.overlays += I
	O = slot("overlay-subtract"); O.icon = 'big.png'; I = image('shape.png'); I.blend_mode = BLEND_SUBTRACT; O.overlays += I
	O = slot("overlay-add-alpha"); O.icon = 'big.png'; I = image('shape.png'); I.blend_mode = BLEND_ADD; I.alpha = 128; O.overlays += I
	O = slot("reset-color"); O.icon = 'big.png'; O.color = "#ff4040"; I = image('shape.png'); I.appearance_flags = RESET_COLOR; O.overlays += I
	O = slot("reset-alpha"); O.icon = 'big.png'; O.alpha = 100; I = image('shape.png'); I.appearance_flags = RESET_ALPHA; O.overlays += I
	O = slot("reset-transform"); O.icon = 'big.png'; O.transform = T; I = image('shape.png'); I.appearance_flags = RESET_TRANSFORM; O.overlays += I
	O = slot("overlay-color-alpha"); O.icon = 'big.png'; O.color = "#ff8080"; O.alpha = 200; I = image('shape.png'); I.color = "#80ff80"; I.alpha = 128; O.overlays += I
	O = slot("big64"); O.icon = 'big64.png'
	O = slot("big64-turn"); O.icon = 'big64.png'; O.transform = T
	O = slot("big64-kt-turn"); O.icon = 'big64.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.transform = T; O.overlays += image('shape.png')
	O = slot("big64-outline"); O.icon = 'big64.png'; O.filters += filter(type = "outline", size = 1, color = "#ff0000")
	O = slot("pixel-wz"); O.icon = 'big.png'; O.pixel_w = 6; O.pixel_z = 5
	O = slot("layer-tie"); O.icon = 'big.png'; C = new(O.loc); C.icon = 'shape.png'; C.layer = O.layer
	O = slot("layer-tie-rev"); O.icon = 'shape.png'; C = new(O.loc); C.icon = 'big.png'; C.layer = O.layer
	O = slot("underlay-over"); O.icon = 'shape.png'; O.underlays += image('big.png'); O.underlays += image('adda.png')
	O = slot("overlay-layers"); O.icon = 'big.png'; I = image('shape.png'); I.layer = 2; O.overlays += I; I = image('adda.png'); I.layer = 4; O.overlays += I
	O = slot("overlay-float-order"); O.icon = 'big.png'; I = image('adda.png'); I.layer = FLOAT_LAYER - 1; O.overlays += I; O.overlays += image('addb.png')
	O = slot("nested-overlay"); O.icon = 'big.png'; A = new(); A.icon = 'shape.png'; A.pixel_x = 4; A.overlays += image('adda.png', pixel_x = 4); O.overlays += A
	O = slot("invis"); O.icon = 'big.png'; O.invisibility = 50
	O = slot("alpha0"); O.icon = 'big.png'; O.alpha = 0; O.overlays += image('shape.png')
	O = slot("plain")
/mob/dummy
	icon = 'dirs.dmi'
	icon_state = "d4"
/proc/dslot(label, state = "d4", d = SOUTH)
	var/obj/thing/O = slot(label)
	O.icon = 'dirs.dmi'
	O.icon_state = state
	O.dir = d
	return O
// Directions: which cell of a 1, 4 or 8 direction state is drawn, and what overlays, images and vis_contents take.
/proc/set_directions()
	var/obj/thing/O
	var/image/I
	var/obj/kid/K
	var/mutable_appearance/A
	var/matrix/T = matrix(); T.Turn(90)
	O = dslot("d4-south")
	O = dslot("d4-east", "d4", EAST)
	O = dslot("d4-north", "d4", NORTH)
	O = dslot("d4-west", "d4", WEST)
	O = dslot("d4-ne", "d4", NORTHEAST)
	O = dslot("d4-sw", "d4", SOUTHWEST)
	O = dslot("d8-ne", "d8", NORTHEAST)
	O = dslot("d8-sw", "d8", SOUTHWEST)
	O = dslot("d1-east", "d1", EAST)
	O = dslot("d4-dir0", "d4", 0)
	O = dslot("d4-dir3", "d4", 3)
	O = dslot("d4-dir12", "d4", 12)
	O = slot("mob-d4-east"); O.icon = null; var/mob/dummy/D = new(O.loc); D.dir = EAST
	O = dslot("overlay-inherit", "d1", EAST); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "d4"; A.pixel_x = 6; O.overlays += A
	O = dslot("overlay-north", "d1", EAST); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "d4"; A.pixel_x = 6; A.dir = NORTH; O.overlays += A
	O = dslot("overlay-south", "d1", EAST); O.overlays += image('dirs.dmi', icon_state = "d4", dir = SOUTH, pixel_x = 6)
	O = dslot("underlay-inherit", "d1", EAST); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "d4"; A.pixel_x = 6; O.underlays += A
	O = dslot("image-inherit", "d1", EAST); I = image('dirs.dmi', O, "d4"); I.pixel_x = 6; pictures += I
	O = dslot("image-west", "d1", EAST); I = image('dirs.dmi', O, "d4", dir = WEST); I.pixel_x = 6; pictures += I
	O = dslot("vis-inherit-dir", "d1", EAST); K = new(); K.icon = 'dirs.dmi'; K.icon_state = "d4"; K.pixel_x = 6; K.vis_flags = VIS_INHERIT_ID | VIS_INHERIT_DIR; O.vis_contents += K
	O = dslot("vis-own-dir", "d1", EAST); K = new(); K.icon = 'dirs.dmi'; K.icon_state = "d4"; K.pixel_x = 6; K.dir = NORTH; O.vis_contents += K
	O = dslot("kt-east", "d4", EAST); O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; A = new(); A.icon = 'dirs.dmi'; A.icon_state = "d8"; A.pixel_x = 6; O.overlays += A
	O = dslot("turn-east", "d4", EAST); O.transform = T
	var/turf/U = tslot("turf-d4-east"); U.icon = 'dirs.dmi'; U.icon_state = "d4"; U.dir = EAST
// Icon states: a missing state, movement states, frames, big icons, an overlay without its own icon, step offsets.
/proc/set_icon_states()
	var/obj/thing/O
	var/image/I
	var/mutable_appearance/A
	O = dslot("state-missing", "nope")
	O = dslot("state-missing-nodefault", "nope"); O.icon = 'nodefault.dmi'
	O = dslot("state-empty-nodefault", ""); O.icon = 'nodefault.dmi'
	O = dslot("state-case", "D4", EAST)
	O = dslot("anim4-east", "anim4", EAST)
	O = dslot("once", "once")
	O = dslot("walk-still", "walk", EAST)
	O = dslot("run-still", "run", EAST)
	O = dslot("overlay-state-missing", "d1"); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "nope"; A.pixel_x = 6; O.overlays += A
	O = dslot("overlay-no-icon", "d1", EAST); A = new(); A.icon_state = "d4"; A.pixel_x = 6; O.overlays += A
	O = dslot("overlay-no-icon-no-state", "d4", EAST); A = new(); A.pixel_x = 6; O.overlays += A
	O = dslot("overlay-state-text", "d4", EAST); O.overlays += "d8"
	O = dslot("image-no-icon", "d4", EAST); I = image(null, O, "d8"); I.pixel_x = 6; pictures += I
	O = dslot("big-d4-east", "d4", EAST); O.icon = 'dirs64.dmi'
	O = dslot("big-anim4-west", "anim4", WEST); O.icon = 'dirs64.dmi'
	O = dslot("big-kt-east", "d4", EAST); O.icon = 'dirs64.dmi'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.overlays += image('shape.png')
	O = dslot("big-overlay", "d1"); A = new(); A.icon = 'dirs64.dmi'; A.icon_state = "d4"; O.overlays += A
	O = dslot("png-east", "", EAST); O.icon = 'shape.png'
	O = dslot("once-overlay", "d1"); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "once"; A.pixel_x = 6; O.overlays += A
	O = dslot("anim4-overlay", "d1", WEST); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "anim4"; A.pixel_x = 6; O.overlays += A
	O = slot("mob-walk-still"); O.icon = null; var/mob/dummy/D = new(O.loc); D.icon_state = "walk"; D.dir = WEST
	O = dslot("step", "d4", EAST); O.step_x = 8; O.step_y = 5
	O = dslot("big-step", "d4", EAST); O.icon = 'dirs64.dmi'; O.step_x = 8; O.step_y = 5
	O = dslot("step-pixel", "d4", EAST); O.step_x = 8; O.pixel_x = -3; O.pixel_w = 2
// Layer and plane edge values: a red bar at layer 4 and a green bar at layer 6 show where the yellow box was drawn.
/proc/lslot(label, l = null, p = null)
	var/obj/thing/O = slot(label)
	O.icon = 'shape.png'
	covered(O)
	if(!isnull(l)) O.layer = l
	if(!isnull(p)) O.plane = p
	return O
/proc/set_layers_planes()
	var/obj/thing/O
	var/image/I
	O = lslot("layer-0", 0)
	O = lslot("layer-float", FLOAT_LAYER)
	O = lslot("layer-neg5", -5)
	O = lslot("layer-1000", 1000)
	O = lslot("layer-background", BACKGROUND_LAYER + 7)
	O = lslot("layer-effects", EFFECTS_LAYER + 1)
	O = lslot("layer-effects-under", EFFECTS_LAYER + 1); var/obj/bar/C = new(O.loc); C.pixel_x = 8; C.layer = EFFECTS_LAYER + 2
	O = lslot("layer-topdown", TOPDOWN_LAYER + 5)
	O = lslot("layer-bg-effects", BACKGROUND_LAYER + EFFECTS_LAYER + 5)
	O = lslot("layer-4-fraction", 4.001)
	O = lslot("layer-bg-vs-bg", BACKGROUND_LAYER + 5); C = new(O.loc); C.pixel_x = 8; C.layer = BACKGROUND_LAYER + 3
	O = lslot("plane-neg1", null, -1)
	O = lslot("plane-1-layer-1", 1, 1)
	O = lslot("plane-float", null, FLOAT_PLANE)
	O = lslot("plane-float-1", null, FLOAT_PLANE + 1)
	O = lslot("plane-10000", 1, 10000)
	O = lslot("plane-neg10000", 7, -10000)
	O = lslot("overlay-background"); I = image('adda.png'); I.layer = BACKGROUND_LAYER + 5; O.overlays += I
	O = lslot("overlay-effects"); I = image('adda.png'); I.layer = EFFECTS_LAYER + 1; O.overlays += I
	O = lslot("overlay-float-plane-1", null, 2); I = image('adda.png'); I.plane = FLOAT_PLANE + 1; I.layer = 1; O.overlays += I; C = new(O.loc); C.icon = 'addb.png'; C.plane = 3; C.layer = 2
	O = lslot("overlay-plane-neg"); I = image('adda.png'); I.plane = -1; O.overlays += I
	O = lslot("mob-obj-tie", 4); O.icon = 'adda.png'; var/mob/dummy/D = new(O.loc); D.icon = 'addb.png'; D.icon_state = ""; D.layer = 4
	O = lslot("turf-over-obj", 2); var/turf/U = O.loc; U.icon = 'big.png'; U.layer = 2
	O = lslot("plain")
// Atoms that keep moving, and a two-frame state on an atom made before and after the client arrived (several grabs).
/proc/shuttle(atom/movable/A)
	A.glide_size = 4
	spawn()
		while(1)
			step(A, EAST); sleep(25)
			step(A, WEST); sleep(25)
var/late_done = 0
/proc/late()
	if(late_done) return
	late_done = 1
	spawn(35)
		var/obj/thing/O = new(locate(8, 4, 1)); O.icon = 'dirs.dmi'; O.icon_state = "slow"
		world.log << "LATE [world.time]"
/proc/set_moving_atoms()
	var/obj/thing/O
	var/mob/dummy/D
	var/mutable_appearance/A
	O = slot("walker"); O.icon = null; D = new(O.loc); D.icon_state = "walk"; shuttle(D)
	O = slot("runner"); O.icon = null; D = new(O.loc); D.icon_state = "run"; shuttle(D)
	O = dslot("walker-obj", "walk"); shuttle(O)
	O = dslot("walker-overlay", "d1"); A = new(); A.icon = 'dirs.dmi'; A.icon_state = "walk"; A.pixel_y = 6; O.overlays += A; shuttle(O)
	O = dslot("walker-d4", "d4"); shuttle(O)
	O = dslot("slow", "slow")
// screen_loc forms, one coloured box each, compared as a whole grab: 1 (2 with a 64 px icon, 3 on a smaller view), 15
// percent, lower-case and half-tile forms, 4 to 14 and 16 to 27 one or two boxes outside the view over a floor that tells
// its tiles (25 to 27 with 64 px icons), 0 signed numbers after an operator.
/proc/set_screen_loc(n)
	world.log << "CASE [world.params["set"]]"
	var/list/forms = list("CENTER", "CENTER+2,CENTER-1", "LEFT,TOP", "RIGHT,BOTTOM", "WEST+1,NORTH", "EAST-1:8,SOUTH+1:4",
		"3:16,5:-8", "1.5,8.5", "TOP,LEFT+3", "NORTHWEST", "SOUTHEAST", "RIGHT-2,TOP-2", "map:5,3", "0,6", "19,6", "6,0", "6,13",
		"LEFT-1,BOTTOM+2", "8,2 to 10,2", "CENTER-4,CENTER-1 to CENTER-3,CENTER+1", "12,9 to 11,8", "CENTER:16,CENTER:-48",
		"NORTH,EAST-3", "14,TOP", "RIGHT,5", "LEFT+25%,BOTTOM+75%", "TOP-1:-4,CENTER+4", "16:40,10", "5,CENTER+3:-40",
		"center-6,center", "13 , 4", "EAST-5", "2,11:16 to 4,11:16", "nosuchmap:9,9", "15.25,3.75", "CENTER-2.5,CENTER+2.5")
	if(n >= 4 && n != 15)
		for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) { t.icon = 'white.png'; t.color = rgb(t.x * 13, t.y * 20, 255 - t.x * 6 - t.y * 8) }
		forms = list(list("0,6"), list("19,6"), list("6,13"), list("6,0"), list("0,6", "19,6"), list("-1,6"), list("6,15"), list("0,0"), list("21,6", "6,14"), list("0:16,6"), list("18:8,6"), list(), list("20,6"), list("6,-1"), list("0,6", "20,6"), list("0,13"), list("19,0"), list("-2,6"), list("6,14"), list("19,13"), list("0,0", "19,13"), list("18,6"), list("3,12"), list("18:-40,6"))[n - 3]
	if(n == 15) forms = list("2.5,11 to 4.5,11", "CENTER,2 to CENTER+2,2", "CENTER-1,CENTER to 12,CENTER", "left+2,bottom+1", "LEFT+50%,TOP", "RIGHT-10%,CENTER", "50%,50%",
		"LEFT+25%:8,BOTTOM", "CENTER+10%,3", "3+50%,4", "Center-6,center+2", "east,south+3", "WEST+100%,9", "2,BOTTOM+33%", "7.25,9 to 9.25,10", "CENTER-1:8,8 to CENTER:8,8", "northeast", "bottomleft", "top,CENTER", "1.5,3 TO 2,4", "left,CENTER", "5,6 tO 6,6", "CENTER-3,2 to 9,2")
	if(n == 0) forms = list("CENTER+-1:16,SOUTH+0:5", "CENTER+0:16,SOUTH+0:5", "CENTER-+2,4", "CENTER--3,6", "CENTER++4,8", "5+-1,10", "CENTER+-1.5,11", "12,NORTH-+2", "WEST+-0,7")
	var/k = 0
	for(var/f in forms)
		var/obj/screen/S = new()
		S.icon = (n == 2 || n >= 25) ? 'big64.png' : 'big.png'
		S.name = f
		S.screen_loc = f
		S.color = rgb(60 + (k * 53) % 196, 60 + (k * 97) % 196, 60 + (k * 31) % 196)
		S.layer = 3 + k / 100
		hud += S
		k++
	if(n == 3) client_case = 7
// Relays of an atom's render target that are turned, scaled, filtered or coloured, on the map and on the HUD.
/proc/set_render_target_relays()
	var/obj/thing/O
	var/obj/screen/H
	var/matrix/T = matrix(); T.Turn(45)
	var/matrix/Q = matrix(); Q.Turn(90)
	var/matrix/S = matrix(); S.Scale(2, 1)
	var/matrix/M = matrix(); M.Translate(8, 4)
	var/red = filter(type = "outline", size = 1, color = "#ff0000")
	var/shadow = filter(type = "drop_shadow", x = 3, y = -3, size = 0, color = "#00ff00")
	var/obj/content/Src = new(locate(1, 1, 1)); Src.icon = 'shape.png'; Src.render_target = "*xs"
	var/obj/content/Big = new(locate(1, 1, 1)); Big.icon = 'big64.png'; Big.render_target = "*xb"
	var/obj/content/Ov = new(locate(1, 1, 1)); Ov.icon = 'shape.png'; var/image/OI = image('small.png'); OI.pixel_x = 24; Ov.overlays += OI; Ov.render_target = "*xo"
	O = slot("relay-turn-outline"); O.render_source = "*xs"; O.transform = T; O.filters = red
	O = slot("relay-turn90"); O.render_source = "*xs"; O.transform = Q
	O = slot("relay-scale"); O.render_source = "*xs"; O.transform = S
	O = slot("relay-translate"); O.render_source = "*xs"; O.transform = M
	O = slot("relay-turn-color"); O.render_source = "*xs"; O.transform = T; O.color = "#ff8080"; O.alpha = 128
	O = slot("relay-outline-color"); O.render_source = "*xs"; O.filters = red; O.color = "#ff8080"; O.alpha = 128
	O = slot("relay-shadow"); O.render_source = "*xs"; O.filters = shadow
	O = slot("relay-blur"); O.render_source = "*xs"; O.filters = filter(type = "blur", size = 2)
	O = slot("relay-two-outlines"); O.render_source = "*xs"; O.filters = red; O.filters += filter(type = "outline", size = 1, color = "#ffffff")
	O = slot("relay-turn-offset"); O.render_source = "*xs"; O.transform = T; O.pixel_x = 8; O.pixel_y = 4
	O = slot("relay-big"); O.render_source = "*xb"
	O = slot("relay-big-turn"); O.render_source = "*xb"; O.transform = T
	O = slot("relay-big-outline"); O.render_source = "*xb"; O.filters = red
	O = slot("relay-ov-turn"); O.render_source = "*xo"; O.transform = Q
	O = slot("relay-turn-overlay"); O.render_source = "*xs"; O.transform = T; O.overlays += image('small.png')
	O = slot("relay-outline-overlay"); O.render_source = "*xs"; O.filters = red; O.overlays += image('small.png')
	O = slot("relay-turn-add"); O.render_source = "*xs"; O.transform = T; O.blend_mode = BLEND_ADD
	O = slot("relay-turn-pixelscale"); O.render_source = "*xs"; O.transform = T; O.appearance_flags = PIXEL_SCALE
	O = slot("hud-relay-turn-outline"); H = screen_at(O); H.render_source = "*xs"; H.transform = T; H.filters = red
	O = slot("hud-relay-scale"); H = screen_at(O); H.render_source = "*xs"; H.transform = S
	O = slot("hud-relay-big-turn"); H = screen_at(O); H.render_source = "*xb"; H.transform = T
	O = slot("hud-relay-shadow"); H = screen_at(O); H.render_source = "*xs"; H.filters = shadow
	O = slot("hud-relay-turn-color"); H = screen_at(O); H.render_source = "*xs"; H.transform = T; H.color = "#ff8080"; H.alpha = 128
	O = slot("hud-relay-turn-overlay"); H = screen_at(O); H.render_source = "*xs"; H.transform = T; H.overlays += image('small.png')
// Two atoms with one render_target name: what a relay of that name shows.
/proc/set_shared_render_target()
	var/obj/thing/O
	var/obj/screen/H
	var/obj/content/A = new(locate(1, 1, 1)); A.icon = 'big.png'; A.render_target = "*d1"
	var/obj/content/B = new(locate(2, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d1"
	B = new(locate(2, 2, 1)); B.icon = 'shape.png'; B.render_target = "*d2"
	A = new(locate(1, 2, 1)); A.icon = 'big.png'; A.render_target = "*d2"
	O = slot("dup-big-first"); O.render_source = "*d1"
	O = slot("dup-shape-first"); O.render_source = "*d2"
	O = slot("dup-hud"); H = screen_at(O); H.render_source = "*d1"
	O = slot("dup-visible"); O.render_source = "d3"
	A = new(locate(17, 1, 1)); A.icon = 'big.png'; A.render_target = "d3"
	B = new(locate(18, 1, 1)); B.icon = 'shape.png'; B.render_target = "d3"
	O = slot("dup-same-tile"); O.render_source = "*d4"
	A = new(locate(3, 1, 1)); A.icon = 'big.png'; A.render_target = "*d4"
	B = new(locate(3, 1, 1)); B.icon = 'adda.png'; B.render_target = "*d4"
	O = slot("dup-shape-west-first"); O.render_source = "*d6"
	B = new(locate(7, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d6"
	A = new(locate(8, 1, 1)); A.icon = 'big.png'; A.render_target = "*d6"
	O = slot("dup-shape-west-second"); O.render_source = "*d7"
	A = new(locate(10, 1, 1)); A.icon = 'big.png'; A.render_target = "*d7"
	B = new(locate(9, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d7"
	O = slot("dup-shape-north"); O.render_source = "*d8"
	A = new(locate(11, 1, 1)); A.icon = 'big.png'; A.render_target = "*d8"
	B = new(locate(11, 2, 1)); B.icon = 'shape.png'; B.render_target = "*d8"
	O = slot("dup-shape-layer"); O.render_source = "*d9"
	A = new(locate(12, 1, 1)); A.icon = 'big.png'; A.render_target = "*d9"; A.layer = 5
	B = new(locate(13, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d9"; B.layer = 4
	O = slot("dup-shape-southeast"); O.render_source = "*d10"
	A = new(locate(14, 2, 1)); A.icon = 'big.png'; A.render_target = "*d10"
	B = new(locate(15, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d10"
	O = slot("dup-shape-low-layer"); O.render_source = "*d11"
	B = new(locate(16, 2, 1)); B.icon = 'shape.png'; B.render_target = "*d11"; B.layer = 2.5
	A = new(locate(16, 1, 1)); A.icon = 'big.png'; A.render_target = "*d11"; A.layer = 5
	O = slot("dup-three"); O.render_source = "*d5"
	A = new(locate(4, 1, 1)); A.icon = 'big.png'; A.render_target = "*d5"
	B = new(locate(5, 1, 1)); B.icon = 'shape.png'; B.render_target = "*d5"
	B = new(locate(6, 1, 1)); B.icon = 'adda.png'; B.render_target = "*d5"
// A relay of a render target as an overlay, and targets that are overlays or vis_contents members (tgstation's eyelids:
// a KEEP_TOGETHER mob, hidden target objects in its vis_contents, and an icon-less overlay with render_source).
/obj/lid
	icon = 'shape.png'
	vis_flags = VIS_INHERIT_DIR | VIS_INHERIT_PLANE | VIS_INHERIT_ID
/proc/relay_overlay(name)
	var/mutable_appearance/M = new()
	M.icon = null
	M.render_source = name
	M.layer = FLOAT_LAYER
	return M
// What tgstation does that the rewrite refused: an item with an emissive blocker (a KEEP_APART | KEEP_TOGETHER |
// RESET_COLOR overlay) in an inventory slot's vis_contents, the hunger bar's KEEP_APART outlined underlay, and the
// starlight render target, which has BLEND_ADD and a colour.
/proc/blocker(flags = KEEP_APART | KEEP_TOGETHER | RESET_COLOR)
	var/mutable_appearance/M = new()
	M.icon = 'small.png'
	M.appearance_flags = flags
	M.layer = FLOAT_LAYER
	return M
/proc/set_tgstation_refusals()
	var/obj/thing/O
	var/obj/lid/L
	var/mutable_appearance/M
	var/obj/screen/H
	var/obj/content/Add = new(locate(1, 1, 1)); Add.icon = 'shape.png'; Add.render_target = "*rf1"; Add.blend_mode = BLEND_ADD; Add.color = "#4040c0"
	var/obj/screen/Star = new(); Star.icon = 'shape.png'; Star.screen_loc = "1,1"; Star.render_target = "*rf2"; Star.blend_mode = BLEND_ADD; Star.color = "#c04040"; Star.appearance_flags = KEEP_TOGETHER; hud += Star
	var/obj/content/Mul = new(locate(2, 1, 1)); Mul.icon = 'shape.png'; Mul.render_target = "*rf3"; Mul.blend_mode = BLEND_MULTIPLY; Mul.color = "#4040c0"
	O = slot("rf-vis-blocker"); O.icon = 'big.png'; L = new(); L.overlays += blocker(); O.vis_contents += L
	O = slot("rf-vis-blocker-color"); O.icon = 'big.png'; L = new(); L.color = "#ff0000"; L.overlays += blocker(); O.vis_contents += L
	O = slot("rf-vis-ka"); O.icon = 'big.png'; L = new(); L.color = "#ff0000"; L.overlays += blocker(KEEP_APART); O.vis_contents += L
	O = slot("rf-vis-blocker-plane"); O.icon = 'big.png'; L = new(); L.color = "#ff0000"; M = blocker(); M.plane = 3; L.overlays += M; O.vis_contents += L
	O = slot("rf-vis-blocker-hud"); H = screen_at(O); H.icon = 'big.png'; L = new(); L.color = "#ff0000"; L.overlays += blocker(); H.vis_contents += L
	O = slot("rf-vis-blocker-holder-color"); O.icon = 'big.png'; O.color = "#00ff00"; L = new(); L.overlays += blocker(); O.vis_contents += L
	O = slot("rf-ka-filter-under"); O.icon = 'shape.png'; M = blocker(KEEP_APART); M.pixel_x = -5; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.underlays += M
	O = slot("rf-ka-filter-over"); O.icon = 'shape.png'; M = blocker(KEEP_APART); M.pixel_x = -5; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.overlays += M
	O = slot("rf-ka-filter-root-filter"); O.icon = 'shape.png'; O.filters = filter(type = "outline", size = 1, color = "#00ff00"); M = blocker(KEEP_APART); M.pixel_x = -5; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.underlays += M
	O = slot("rf-ka-filter-hud"); H = screen_at(O); H.icon = 'shape.png'; H.alpha = 128; M = blocker(KEEP_APART); M.pixel_x = -5; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); H.underlays += M
	O = slot("rf-filter-under"); O.icon = 'shape.png'; M = blocker(0); M.pixel_x = -5; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.underlays += M
	O = slot("rf-blocker"); O.icon = 'shape.png'; O.color = "#ff0000"; O.overlays += blocker()
	O = slot("rf-target-add"); O.icon = 'big.png'; O.overlays += relay_overlay("*rf1")
	O = slot("rf-target-add-relay-add"); O.icon = 'big.png'; M = relay_overlay("*rf1"); M.blend_mode = BLEND_ADD; O.overlays += M
	O = slot("rf-target-add-root"); O.icon = null; O.render_source = "*rf1"
	O = slot("rf-target-add-root-add"); O.icon = null; O.render_source = "*rf1"; O.blend_mode = BLEND_ADD
	O = slot("rf-target-star"); O.icon = 'big.png'; O.overlays += relay_overlay("*rf2")
	O = slot("rf-target-star-add"); O.icon = 'big.png'; M = relay_overlay("*rf2"); M.blend_mode = BLEND_ADD; O.overlays += M
	O = slot("rf-target-mul"); O.icon = 'big.png'; O.overlays += relay_overlay("*rf3")
	O = slot("rf-target-mul-root"); O.icon = null; O.render_source = "*rf3"
	O = slot("rf-blocker-root-filter"); O.icon = 'shape.png'; O.color = "#ff0000"; O.filters = filter(type = "outline", size = 1, color = "#00ff00"); O.overlays += blocker()
	O = slot("rf-vis-blocker-filter"); O.icon = 'big.png'; L = new(); L.filters = filter(type = "outline", size = 1, color = "#00ff00"); L.overlays += blocker(); O.vis_contents += L
	O = slot("rf-blocker-kt-parent"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.color = "#ff0000"; O.overlays += blocker()
/proc/small(px = 8)
	var/mutable_appearance/M = new()
	M.icon = 'small.png'
	M.pixel_x = px
	M.layer = FLOAT_LAYER
	return M
/proc/hidden(name, x)
	var/obj/content/C = new(locate(x, 1, 1))
	C.icon = 'shape.png'
	C.render_target = name
	return C
// tgstation's blink: a hidden-target eyelid in the mob's vis_contents whose alpha is animated between 0 and 255.
/proc/blinker(atom/movable/H, name, on = 20, off = 20, layer = null)
	var/obj/lid/L = new()
	if(layer) L.layer = layer
	L.render_target = name
	L.color = "#ff0000"
	H.vis_contents += L
	if(on)
		animate(L, alpha = 0, time = 0, loop = -1)
		animate(time = off)
		animate(alpha = 255, time = 0)
		animate(time = on)
	return L
/proc/set_animated_render_target()
	var/obj/thing/O
	var/obj/lid/L
	var/mutable_appearance/M
	O = slot("rk-blink"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; blinker(O, "*rk1"); O.overlays += relay_overlay("*rk1")
	O = slot("rk-still"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; blinker(O, "*rk2", 0); O.overlays += relay_overlay("*rk2")
	O = slot("rk-blink-layer"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = blinker(O, "*rk3", 20, 20, -28); M = relay_overlay("*rk3"); M.layer = -28; O.overlays += M
	O = slot("rk-still-layer"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = blinker(O, "*rk4", 0); L.layer = -28; M = relay_overlay("*rk4"); M.layer = -28; O.overlays += M
	O = slot("rk-blink-plain"); O.icon = 'big.png'; blinker(O, "*rk5"); O.overlays += relay_overlay("*rk5")
	O = slot("rk-blink-root"); O.icon = 'shape.png'; O.render_target = "*rk6"; animate(O, alpha = 0, time = 0, loop = -1); animate(time = 20); animate(alpha = 255, time = 0); animate(time = 20)
	O = slot("rk-blink-root-relay"); O.render_source = "*rk6"
	O = slot("rk-plain-blink"); O.icon = 'shape.png'; animate(O, alpha = 0, time = 0, loop = -1); animate(time = 20); animate(alpha = 255, time = 0); animate(time = 20)
	O = slot("rk-vis-blink-notarget"); O.icon = 'big.png'; L = blinker(O, null)
	O = slot("rk-blink-layer-late"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = blinker(O, "*rk7"); L.layer = -28; M = relay_overlay("*rk7"); M.layer = -28; O.overlays += M
	O = slot("rk-vis-blink-notarget-kt"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = blinker(O, null)
// Images on a turned KEEP_TOGETHER atom (a lying tgstation mob): progress bars, HUD icons, runechat.
/proc/lying(label, flags = KEEP_TOGETHER, deg = 90)
	var/obj/thing/O = slot(label)
	O.icon = 'big.png'
	O.appearance_flags = flags
	var/matrix/T = matrix(); T.Turn(deg); T.Translate(0, -6)
	O.transform = T
	O.overlays += small()
	return O
/proc/set_turned_mob_images()
	var/obj/thing/O
	var/image/I
	O = lying("rl-kt-image"); picture(O)
	O = lying("rl-kt-image-plane"); I = picture(O); I.plane = 5
	O = lying("rl-kt-image-reset"); I = picture(O); I.appearance_flags = RESET_TRANSFORM | RESET_COLOR | RESET_ALPHA
	O = lying("rl-kt-image-reset-plane"); I = picture(O); I.appearance_flags = RESET_TRANSFORM | RESET_COLOR | RESET_ALPHA | PIXEL_SCALE; I.plane = 5; I.pixel_z = 20
	O = lying("rl-kt-image-apart"); I = picture(O); I.appearance_flags = KEEP_APART
	O = lying("rl-kt-image-apart-reset"); I = picture(O); I.appearance_flags = KEEP_APART | RESET_TRANSFORM
	O = lying("rl-kt-image-override"); I = picture(O); I.override = 1
	O = lying("rl-kt45-image-plane", KEEP_TOGETHER, 45); I = picture(O); I.plane = 5
	O = lying("rl-kt45-image-reset-plane", KEEP_TOGETHER, 45); I = picture(O); I.appearance_flags = RESET_TRANSFORM; I.plane = 5
	O = lying("rl-plain-image-plane", 0); I = picture(O); I.plane = 5
	O = lying("rl-plain-image-reset", 0); I = picture(O); I.appearance_flags = RESET_TRANSFORM
	O = lying("rl-kt-image-anim"); I = picture(O); I.appearance_flags = RESET_TRANSFORM; I.plane = 5; I.alpha = 0; animate(I, pixel_z = 20, alpha = 255, time = 5)
	O = lying("rl-kt-alpha-image-plane"); O.alpha = 128; O.color = "#ff8080"; I = picture(O); I.plane = 5
	O = lying("rl-kt-alpha-image-reset-plane"); O.alpha = 128; O.color = "#ff8080"; I = picture(O); I.plane = 5; I.appearance_flags = RESET_TRANSFORM | RESET_COLOR | RESET_ALPHA
// Taken apart from a sweep of refused shapes: a filtered overlay in and out of a translucent KEEP_TOGETHER group, and a relay
// overlay on a turned, scaled or moved atom.
/proc/hidden_at(loc, name)
	var/obj/content/C = new(loc)
	C.icon = 'shape.png'
	C.render_target = name
	return C
/proc/set_relay_overlay_matrix()
	var/obj/thing/O
	var/obj/content/C
	var/obj/content/A
	var/mutable_appearance/M
	var/matrix/T
	hidden("*rj1", 1)
	O = slot("rj-blur-overlay"); O.icon = 'big.png'; M = small(); M.filters = filter(type = "blur", size = 1); O.overlays += M
	O = slot("rj-blur-overlay-alpha"); O.icon = 'big.png'; O.alpha = 128; M = small(); M.filters = filter(type = "blur", size = 1); O.overlays += M
	O = slot("rj-kt-blur-overlay-opaque"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; M = small(); M.filters = filter(type = "blur", size = 1); O.overlays += M
	O = slot("rj-kt-blur-overlay-edge"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; M = small(-2); M.filters = filter(type = "blur", size = 1); O.overlays += M
	O = slot("rj-kt-outline-overlay"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; M = small(); M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.overlays += M
	O = slot("rj-kt-shadow-overlay"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; M = small(); M.filters = filter(type = "drop_shadow", x = 2, y = -2, size = 0, color = "#ff0000"); O.overlays += M
	O = slot("rj-turn45-relay-overlay"); O.icon = 'big.png'; T = matrix(); T.Turn(45); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; O.overlays += M
	O = slot("rj-scale-relay-overlay"); O.icon = 'big.png'; T = matrix(); T.Scale(1.5, 0.5); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; O.overlays += M
	O = slot("rj-turn-relay-own-scale"); O.icon = 'big.png'; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; T = matrix(); T.Scale(1.5, 0.5); M.transform = T; O.overlays += M
	O = slot("rj-scale-relay-own-turn"); O.icon = 'big.png'; T = matrix(); T.Scale(1.5, 0.5); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; T = matrix(); T.Turn(90); M.transform = T; O.overlays += M
	O = slot("rj-turn-relay-own-move"); O.icon = 'big.png'; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); T = matrix(); T.Translate(6, 0); M.transform = T; O.overlays += M
	O = slot("rj-move-relay-overlay"); O.icon = 'big.png'; T = matrix(); T.Translate(6, 3); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; O.overlays += M
	O = slot("rj-turn-relay-reset"); O.icon = 'big.png'; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; M.appearance_flags = RESET_TRANSFORM; O.overlays += M
	O = slot("rj-turn-relay-underlay"); O.icon = 'big.png'; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; O.underlays += M
	O = slot("rj-turn-relay-outline"); O.icon = 'big.png'; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; M.filters = filter(type = "outline", size = 1, color = "#ff0000"); O.overlays += M
	O = slot("rj-turn-relay-colour"); O.icon = 'big.png'; O.color = "#ff8080"; T = matrix(); T.Turn(90); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; O.overlays += M
	O = slot("rj-turn-relay-pixelscale"); O.icon = 'big.png'; T = matrix(); T.Turn(45); O.transform = T; M = relay_overlay("*rj1"); M.pixel_x = 8; M.appearance_flags = PIXEL_SCALE; O.overlays += M
	O = slot("rj-hidden-add-child-floor"); O.icon = null; C = hidden_at(O.loc, "*rjA"); M = small(); M.blend_mode = BLEND_ADD; C.overlays += M
	O = slot("rj-hidden-child-floor"); O.icon = null; C = hidden_at(O.loc, "*rjB"); C.overlays += small()
	O = slot("rj-hidden-add-child-after"); O.icon = 'big.png'; O.alpha = 128; C = hidden_at(O.loc, "*rjC"); M = small(); M.blend_mode = BLEND_ADD; C.overlays += M
	C = hidden_at(locate(2 + (length(cases) % 6) * 3, 10 - round(length(cases) / 6) * 3, 1), "*rjD"); M = small(); M.blend_mode = BLEND_ADD; C.overlays += M
	O = slot("rj-hidden-add-child-before-kt"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.overlays += small()
	A = new(locate(2 + (length(cases) % 6) * 3, 10 - round(length(cases) / 6) * 3, 1)); A.icon = 'small.png'; A.pixel_x = 8; A.blend_mode = BLEND_ADD
	O = slot("rj-add-obj-before"); O.icon = 'big.png'
	A = new(locate(2 + (length(cases) % 6) * 3, 10 - round(length(cases) / 6) * 3, 1)); A.icon = null; M = small(); M.blend_mode = BLEND_ADD; A.overlays += M
	O = slot("rj-add-overlay-before"); O.icon = 'big.png'
	A = new(locate(2 + (length(cases) % 6) * 3, 10 - round(length(cases) / 6) * 3, 1)); A.icon = null; M = small(); M.blend_mode = BLEND_ADD; A.overlays += M
	O = slot("rj-add-overlay-before-kt"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.overlays += small()
/proc/set_overlay_relays()
	var/obj/thing/O
	var/obj/lid/L
	var/mutable_appearance/M
	var/obj/content/Src = new(locate(1, 1, 1)); Src.icon = 'shape.png'; Src.render_target = "*ov1"
	var/obj/content/Col = new(locate(2, 1, 1)); Col.icon = 'shape.png'; Col.render_target = "*ov2"; Col.color = "#ff0000"
	var/obj/content/Half = new(locate(3, 1, 1)); Half.icon = 'shape.png'; Half.render_target = "*ov3"; Half.alpha = 128
	var/obj/content/Zero = new(locate(4, 1, 1)); Zero.icon = 'shape.png'; Zero.render_target = "*ov4"; Zero.alpha = 0
	O = slot("ov-relay"); O.icon = 'big.png'; O.overlays += relay_overlay("*ov1")
	O = slot("ov-relay-src-color"); O.icon = 'big.png'; O.overlays += relay_overlay("*ov2")
	O = slot("ov-relay-src-alpha"); O.icon = 'big.png'; O.overlays += relay_overlay("*ov3")
	O = slot("ov-relay-src-zero"); O.icon = 'big.png'; O.overlays += relay_overlay("*ov4")
	O = slot("ov-relay-offset"); O.icon = 'big.png'; M = relay_overlay("*ov1"); M.pixel_x = 8; M.pixel_y = 4; O.overlays += M
	O = slot("ov-relay-own-color"); O.icon = 'big.png'; M = relay_overlay("*ov1"); M.color = "#00ff00"; M.alpha = 128; O.overlays += M
	O = slot("ov-relay-kt"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.overlays += relay_overlay("*ov2")
	O = slot("ov-relay-kt-alpha"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; O.alpha = 128; O.overlays += relay_overlay("*ov2")
	O = slot("ov-relay-under"); O.icon = 'big.png'; O.underlays += relay_overlay("*ov2")
	O = slot("ov-vis-src"); O.icon = 'big.png'; L = new(); L.render_target = "*ov5"; L.color = "#ff0000"; O.vis_contents += L; O.overlays += relay_overlay("*ov5")
	O = slot("ov-vis-src-kt"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = new(); L.render_target = "*ov6"; L.color = "#ff0000"; O.vis_contents += L; O.overlays += relay_overlay("*ov6")
	O = slot("ov-vis-src-kt-half"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = new(); L.render_target = "*ov7"; L.color = "#ff0000"; L.alpha = 128; O.vis_contents += L; O.overlays += relay_overlay("*ov7")
	O = slot("ov-vis-src-kt-zero"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = new(); L.render_target = "*ov8"; L.alpha = 0; O.vis_contents += L; O.overlays += relay_overlay("*ov8")
	O = slot("ov-vis-src-east"); O.icon = 'big.png'; O.dir = EAST; O.appearance_flags = KEEP_TOGETHER; L = new(); L.icon = 'dirs.dmi'; L.icon_state = "d4"; L.render_target = "*ov9"; O.vis_contents += L; O.overlays += relay_overlay("*ov9")
	O = slot("ov-vis-src-layer"); O.icon = 'big.png'; O.appearance_flags = KEEP_TOGETHER; L = new(); L.render_target = "*ov10"; L.color = "#ff0000"; L.layer = FLOAT_LAYER - 1; O.vis_contents += L; M = relay_overlay("*ov10"); M.layer = FLOAT_LAYER - 1; O.overlays += M; O.overlays += image('small.png')
	O = slot("ov-target-child"); O.icon = 'big.png'; M = new(); M.icon = 'shape.png'; M.render_target = "*ov11"; O.overlays += M
	O = slot("ov-target-child-relay"); O.render_source = "*ov11"
	O = slot("ov-target-child-visible"); O.icon = 'big.png'; M = new(); M.icon = 'shape.png'; M.render_target = "ov12"; M.pixel_x = 8; O.overlays += M
	O = slot("ov-target-child-visible-relay"); O.render_source = "ov12"
	O = slot("ov-relay-missing"); O.icon = 'big.png'; O.overlays += relay_overlay("*ovnone")
	O = slot("ov-relay-only"); O.icon = null; O.overlays += relay_overlay("*ov2")
	O = slot("ov-relay-turn"); O.icon = 'big.png'; M = relay_overlay("*ov2"); var/matrix/T = matrix(); T.Turn(90); M.transform = T; O.overlays += M
	O = slot("ov-hud-relay"); var/obj/screen/H = screen_at(O); H.icon = 'big.png'; H.overlays += relay_overlay("*ov2")
	O = slot("ov-image-relay"); O.icon = 'big.png'; var/image/I = picture(O); I.icon = null; I.render_source = "*ov2"; I.layer = FLOAT_LAYER
// client.dir: what turns with the map (n is the dir: 1 NORTH, 2 SOUTH, 4 EAST, 8 WEST, 5 NORTHEAST).
var/client_dir = 0
/proc/set_client_dir(n)
	world.log << "CASE [world.params["set"]]"
	client_dir = n
	var/obj/thing/O
	O = new(locate(9, 6, 1)); O.icon = 'big.png'
	O = new(locate(11, 6, 1)); O.icon = 'shape.png'
	O = new(locate(9, 8, 1)); O.icon = 'adda.png'
	O = new(locate(12, 8, 1)); O.icon = 'dirs.dmi'; O.icon_state = "d4"; O.dir = EAST
	O = new(locate(7, 8, 1)); O.icon = 'dirs.dmi'; O.icon_state = "d4"; O.dir = NORTH
	O = new(locate(7, 4, 1)); O.icon = 'big64.png'
	O = new(locate(11, 4, 1)); O.icon = 'big.png'; O.pixel_x = 12
	O = new(locate(13, 5, 1)); O.icon = 'big.png'; var/image/I = image('small.png'); I.pixel_x = 20; O.overlays += I
	O = new(locate(6, 6, 1)); O.icon = 'big.png'; var/matrix/M = matrix(); M.Translate(10, 0); O.transform = M
	O = new(locate(5, 8, 1)); O.icon = 'big.png'; O.maptext = "Hi"
	O = new(locate(13, 7, 1)); O.icon = 'small.png'
	var/obj/screen/H = new(); H.icon = 'big.png'; H.screen_loc = "2,2"; hud += H
	H = new(); H.icon = 'shape.png'; H.screen_loc = "EAST,NORTH"; hud += H
	H = new(); H.icon = 'dirs.dmi'; H.icon_state = "d4"; H.dir = EAST; H.screen_loc = "CENTER-1,CENTER+2"; hud += H
	O = new(locate(8, 5, 1)); O.icon = 'dirs.dmi'; O.icon_state = "d4"; O.dir = EAST; var/obj/A = new(); A.icon = 'dirs.dmi'; A.icon_state = "d4"; A.pixel_y = 14; A.dir = NORTH; O.overlays += A
var/client_case = 0
/proc/set_client(n)
	world.log << "CASE [world.params["set"]]"
	client_case = n
	if(n == 13 || n == 14 || n == 16 || n == 18)
		if(n == 13) set_lighting(5)
		if(n == 14) set_lighting(2)
		if(n == 16) set_plane_mask_filters()
		if(n == 18) set_alpha_masks()
		return
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1)))
		if((t.x * 2 + t.y) % 5 == 0) { var/obj/thing/B = new(t); B.icon = 'big.png' }
		if((t.x + t.y * 3) % 7 == 0) { var/obj/thing/B2 = new(t); B2.icon = 'shape.png'; B2.pixel_x = 8; if(n == 3) B2.appearance_flags = NO_CLIENT_COLOR }
	var/obj/screen/H = new(); H.icon = 'big.png'; H.screen_loc = "3,3"; H.name = "hud"; hud += H
	H = new(); H.icon = 'shape.png'; H.screen_loc = "5,3"; H.appearance_flags = NO_CLIENT_COLOR; hud += H
	if(n == 3) { var/obj/screen/M = pm(4); M.appearance_flags = PLANE_MASTER | NO_CLIENT_COLOR; var/obj/thing/P = new(locate(9, 9, 1)); P.icon = 'big64.png'; P.plane = 4 }
/mob/observer/proc/apply_case()
	var/n = client_case
	if(!n) return
	if(n == 1 || n == 3 || n == 13 || n == 16 || n == 18) client.color = "#ff8080"
	if(n == 14) client.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0.1,0,0,0)
	if(n == 2) client.color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0.1,0,0,0)
	if(n == 4) { client.pixel_x = 8; client.pixel_y = 4 }
	if(n == 5) { client.pixel_w = -6; client.pixel_z = 10 }
	if(n == 6) client.dir = EAST
	if(n == 7) client.view = "9x7"
	if(n == 8) { client.eye = locate(5, 4, 1) }
	if(n == 9) animate(client, color = "#8080ff", time = 60000)
	if(n == 10) { client.dir = SOUTH; client.pixel_x = 8 }
	if(n == 11) client.view = "21x15"
	if(n == 12) { client.color = "#ffffff80" }
	if(n == 19) { animate(client, color = "#ff8080", time = 1); animate(color = "#ff7f80", time = 60000) }
	if(n == 20) { client.color = "#8080ff"; animate(client, color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0.1,0,0,0), time = 1); animate(color = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0.1,0,0.01,0), time = 60000) }
/proc/set_group_blend_add()
	added()
	added(128)
	added(255, TRUE)
	added(128, TRUE)
	added(255, FALSE, TRUE)
	added(128, FALSE, TRUE)
	added(255, TRUE, TRUE)
	added(128, TRUE, TRUE)
	added(255, FALSE, FALSE, TRUE)
	added(128, FALSE, FALSE, TRUE)
	added(255, TRUE, FALSE, TRUE)
	added(128, TRUE, FALSE, TRUE)
	// A group whose root is BLEND_ADD itself.
	var/obj/thing/O = slot("kt-root-add-under"); group(O); O.alpha = 255; O.blend_mode = BLEND_ADD; O.underlays += image('addb.png')
	O = slot("kt-root-add-under-add"); group(O); O.alpha = 255; O.blend_mode = BLEND_ADD; var/image/I = image('adda.png'); I.blend_mode = BLEND_ADD; O.underlays += I
	O = slot("kt-root-add-over"); group(O); O.alpha = 255; O.blend_mode = BLEND_ADD; O.overlays += image('addb.png')
	O = slot("kt-root-add-under-add-half"); group(O); O.blend_mode = BLEND_ADD; I = image('adda.png'); I.blend_mode = BLEND_ADD; O.underlays += I
	O = slot("kt-root-add-under-half"); group(O); O.blend_mode = BLEND_ADD; O.underlays += image('addb.png')
	O = slot("kt-root-add-under-add-shift"); group(O); O.blend_mode = BLEND_ADD; var/mutable_appearance/A = new /mutable_appearance('adda.png'); A.pixel_x = 12; A.blend_mode = BLEND_ADD; O.underlays = list(A)
// One plane and master per case, and a second plane whose master is the mask: the mask is a whole plane, as in worlds
// that mask one plane with another (a 32 px render target would sit at the middle of the view).
/proc/masked(label)
	var/obj/thing/O = slot(label)
	var/k = length(cases)
	O.icon = null
	var/obj/content/B = new(O.loc)
	B.plane = k
	var/obj/screen/M = new()
	M.icon = null
	M.name = "master [label]"
	M.plane = k
	M.appearance_flags = PLANE_MASTER
	M.screen_loc = "1,1"
	hud += M
	var/obj/content/K = new(O.loc)
	K.icon = 'mask.png'
	K.name = "mask [label]"
	K.plane = 100 + k
	var/obj/screen/S = new()
	S.icon = null
	S.name = "mask master [label]"
	S.plane = 100 + k
	S.appearance_flags = PLANE_MASTER
	S.render_target = "*mask[k]"
	S.screen_loc = "1,1"
	hud += S
	return M
/proc/mask_filter(obj/M, x = 0, y = 0, flags = 0)
	var/k = M.plane
	return filter(type = "alpha", render_source = "*mask[k]", x = x, y = y, flags = flags)
/proc/set_plane_mask_filters()
	var/obj/screen/M
	var/list/swap = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	M = masked("mask"); M.filters += mask_filter(M)
	M = masked("mask-x8"); M.filters += mask_filter(M, 8, 0)
	M = masked("mask-y8"); M.filters += mask_filter(M, 0, 8)
	M = masked("mask-inverse"); M.filters += mask_filter(M, 0, 0, MASK_INVERSE)
	M = masked("mask-swap"); M.filters += mask_filter(M, 0, 0, MASK_SWAP)
	M = masked("mask-x8-inverse"); M.filters += mask_filter(M, 8, 0, MASK_INVERSE)
	M = masked("mask-blur"); M.filters += mask_filter(M); M.filters += filter(type = "blur", size = 2)
	M = masked("blur-mask"); M.filters += filter(type = "blur", size = 2); M.filters += mask_filter(M)
	M = masked("mask-outline"); M.filters += mask_filter(M); M.filters += filter(type = "outline", size = 1, color = "#ffffff")
	M = masked("mask-shadow"); M.filters += mask_filter(M); M.filters += filter(type = "drop_shadow", x = 4, y = -4, size = 0, color = "#ffffff")
	M = masked("mask-color"); M.filters += mask_filter(M); M.color = "#ff8080"
	M = masked("mask-matrix"); M.filters += mask_filter(M); M.color = swap
	M = masked("mask-alpha"); M.filters += mask_filter(M); M.alpha = 128
	M = masked("mask-twice"); M.filters += mask_filter(M); M.filters += mask_filter(M, 8, 0)
	M = masked("colorfilter"); M.filters += filter(type = "color", color = swap)
	M = masked("colorfilter-blur"); M.filters += filter(type = "color", color = swap); M.filters += filter(type = "blur", size = 2)
	M = masked("colorfilter-color"); M.filters += filter(type = "color", color = swap); M.color = "#ff8080"
	M = masked("mask-colorfilter"); M.filters += mask_filter(M); M.filters += filter(type = "color", color = swap)
// Where a turned or scaled KEEP_TOGETHER surface ends and what is sampled at its edge: the icon, its opaque box, or the
// box of everything drawn in it.
/proc/kt_turned(label, icon_file = 'big.png', deg = 45, alpha = 128)
	var/obj/thing/O = slot(label)
	O.icon = icon_file
	O.appearance_flags = KEEP_TOGETHER
	O.alpha = alpha
	var/matrix/T = matrix(); T.Turn(deg)
	O.transform = T
	return O
/proc/inner(px = 10, py = 10)
	var/image/I = image('small.png')
	I.pixel_x = px
	I.pixel_y = py
	return I
/proc/set_surface_bounds()
	var/obj/thing/O
	var/image/I
	var/mutable_appearance/A
	var/matrix/T
	var/list/swap = list(0,1,0,0, 1,0,0,0, 0,0,1,0, 0,0,0,1, 0,0,0,0)
	O = kt_turned("ta-kt-turn"); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-opaque-turn", 'big.png', 45, 255); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-alone-turn")
	O = kt_turned("ta-kt-alone-opaque-turn", 'big.png', 45, 255)
	O = kt_turned("ta-kt-full-turn", 'full.png'); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-ell-turn", 'ell.png'); O.overlays += inner(4, 6)
	O = kt_turned("ta-kt-faint-turn", 'faint.png'); O.overlays += inner()
	O = kt_turned("ta-kt-overlay-out-turn"); O.overlays += image('shape.png', pixel_x = 10)
	O = kt_turned("ta-kt-small-turned-turn"); I = inner(); T = matrix(); T.Turn(45); I.transform = T; O.overlays += I
	O = kt_turned("ta-kt-image-out-turn"); I = picture(O); I.layer = FLOAT_LAYER; I.pixel_x = 10
	O = kt_turned("ta-kt-vis-out-turn"); var/obj/kid/K = kid(O); K.pixel_x = 24
	O = kt_turned("ta-kt-scale2"); T = matrix(); T.Scale(2); O.transform = T; O.overlays += image('shape.png')
	O = kt_turned("ta-kt-dot-scale4", 'dot.png', 0, 255); T = matrix(); T.Scale(4); O.transform = T; O.overlays += image('dot2.png')
	O = kt_turned("ta-kt-rim-turn", 'rim.png', 45, 255); O.overlays += inner()
	O = kt_turned("ta-kt-odd-turn", 'odd.png'); O.overlays += inner()
	O = kt_turned("ta-kt-turn10", 'big.png', 10); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-turn30", 'big.png', 30); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-turn60", 'big.png', 60); O.overlays += image('shape.png')
	O = kt_turned("ta-kt-color-filter-turn"); O.overlays += image('shape.png'); O.filters += filter(type = "color", color = swap)
	O = slot("ta-color-filter-turn"); O.icon = 'big.png'; T = matrix(); T.Turn(45); O.transform = T; O.filters += filter(type = "color", color = swap)
	O = kt_turned("ta-kt-hidden-overlay-turn"); I = image('shape.png', pixel_x = 10); I.alpha = 0; O.overlays += I
	O = kt_turned("ta-kt-nested-turn"); A = new(); A.icon = 'shape.png'; A.appearance_flags = KEEP_TOGETHER; A.alpha = 128; A.overlays += image('adda.png'); O.overlays += A
	O = kt_turned("ta-kt-turn-pixelscale"); O.appearance_flags = KEEP_TOGETHER | PIXEL_SCALE; O.overlays += image('shape.png')
	O = kt_turned("ta-kt-big64-turn", 'big64.png'); O.overlays += image('shape.png')
world/New()
	..()
	world.log = file("events.log")
	for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
	switch(params["set"])
		if("images-groups") set_images_groups()
		if("plane-mask-filters") set_plane_mask_filters()
		if("images-groups-more") set_images_groups_more()
		if("group-blend-add") set_group_blend_add()
		if("color-matrix-alpha") set_color_matrix_alpha()
		if("animated-groups") set_animated_groups()
		if("shared-render-target") set_shared_render_target()
		if("alpha-masks") set_alpha_masks()
		if("repeated-filters") set_repeated_filters()
		if("blend-reset-layers") set_blend_reset_layers()
		if("directions") set_directions()
		if("icon-states") set_icon_states()
		if("layers-planes") set_layers_planes()
		if("moving-atoms") set_moving_atoms()
		if("render-target-relays") set_render_target_relays()
		if("overlay-relays") set_overlay_relays()
		if("tgstation-refusals") set_tgstation_refusals()
		if("relay-overlay-matrix") set_relay_overlay_matrix()
		if("animated-render-target") set_animated_render_target()
		if("turned-mob-images") set_turned_mob_images()
		if("surface-bounds") set_surface_bounds()
		if("sweep") set_sweep()
		if("relay-hud") set_relay(4)
		if("relay-hud-turn") set_relay(5)
		if("relay-hud-color") set_relay(6)
		if("relay-hud-outline") set_relay(7)
		if("relay-obj") set_relay(8)
		if("relay-hud-noicon") set_relay(9)
		if("relay-obj-turn") set_relay(10)
		if("relay-obj-color") set_relay(11)
		if("relay-obj-outline") set_relay(12)
		if("plane-alpha-mask") set_plane_alpha_mask(1)
		if("plane-alpha-mask-offset") set_plane_alpha_mask(2)
		if("plane-alpha-mask-large") set_plane_alpha_mask(3)
		if("plane-alpha-mask-screen-source") set_plane_alpha_mask(4)
		if("plane-alpha-mask-visible-source") set_plane_alpha_mask(5)
		if("plane-alpha-mask-inverse") set_plane_alpha_mask(6)
		if("plane-alpha-mask-large-shifted") set_plane_alpha_mask(7)
		if("lighting-multiply") set_lighting(1)
		if("lighting-emissive-mask") set_lighting(2)
		if("lighting-relay") set_lighting(3)
		if("lighting-matrix-blur") set_lighting(4)
		if("lighting-game-relays") set_lighting(5)
		if("lighting-translucent") set_lighting(6)
		if("lighting-translucent-tinted") set_lighting(7)
		if("screen-loc-signs") set_screen_loc(0)
		if("screen-loc-forms") set_screen_loc(1)
		if("screen-loc-forms-64px") set_screen_loc(2)
		if("screen-loc-forms-small-view") set_screen_loc(3)
		if("screen-loc-percent-ranges") set_screen_loc(15)
		if("screen-loc-off-view-1") set_screen_loc(4)
		if("screen-loc-off-view-2") set_screen_loc(5)
		if("screen-loc-off-view-3") set_screen_loc(6)
		if("screen-loc-off-view-4") set_screen_loc(7)
		if("screen-loc-off-view-5") set_screen_loc(8)
		if("screen-loc-off-view-6") set_screen_loc(9)
		if("screen-loc-off-view-7") set_screen_loc(10)
		if("screen-loc-off-view-8") set_screen_loc(11)
		if("screen-loc-off-view-9") set_screen_loc(12)
		if("screen-loc-off-view-10") set_screen_loc(13)
		if("screen-loc-off-view-11") set_screen_loc(14)
		if("screen-loc-off-view-12") set_screen_loc(16)
		if("screen-loc-off-view-13") set_screen_loc(17)
		if("screen-loc-off-view-14") set_screen_loc(18)
		if("screen-loc-off-view-15") set_screen_loc(19)
		if("screen-loc-off-view-16") set_screen_loc(20)
		if("screen-loc-off-view-17") set_screen_loc(21)
		if("screen-loc-off-view-18") set_screen_loc(22)
		if("screen-loc-off-view-19") set_screen_loc(23)
		if("screen-loc-off-view-20") set_screen_loc(24)
		if("screen-loc-off-view-21") set_screen_loc(25)
		if("screen-loc-off-view-22") set_screen_loc(26)
		if("screen-loc-off-view-23") set_screen_loc(27)
		if("client-color") set_client(1)
		if("client-color-matrix") set_client(2)
		if("client-color-exempt") set_client(3)
		if("client-pixel-xy") set_client(4)
		if("client-pixel-wz") set_client(5)
		if("client-dir-east-scene") set_client(6)
		if("client-view-small") set_client(7)
		if("client-eye") set_client(8)
		if("client-color-animated") set_client(9)
		if("client-dir-south-pixel") set_client(10)
		if("client-view-large") set_client(11)
		if("client-color-alpha") set_client(12)
		if("client-color-lighting-relays") set_client(13)
		if("client-matrix-lighting-mask") set_client(14)
		if("client-color-plane-mask") set_client(16)
		if("client-color-alpha-masks") set_client(18)
		if("client-color-tween") set_client(19)
		if("client-matrix-tween") set_client(20)
		if("client-dir-north") set_client_dir(1)
		if("client-dir-south") set_client_dir(2)
		if("client-dir-east") set_client_dir(4)
		if("client-dir-west") set_client_dir(8)
		if("client-dir-northeast") set_client_dir(5)
		else set_images_groups()
/mob/observer
	invisibility = 101
	Login()
		..()
		loc = locate(9,6,1)
		client.images += pictures
		client.screen += hud
		apply_case()
		if(client_dir) client.dir = client_dir
		if(world.params["set"] == "moving-atoms") late()
		world.log << "READY [key] [world.time] [world.timeofday]"

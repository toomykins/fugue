// Static transform fixture: sampling with and without PIXEL_SCALE (set a), transformed children and maptext
// (set b), PIXEL_SCALE between parents, overlays and images (set c). Cases sit on a 3-tile grid.
// Run with -params "set=a" (default), "set=b" or "set=c".
world
    name = "Static transform probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
var/list/cases = list()
/obj/thing
    icon = 'quad.png'
    layer = 5
/proc/T(a)
    var/matrix/M = matrix()
    M.Turn(a)
    return M
/proc/slot(label)
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    cases += O
    world.log << "CASE [label]"
    return O
/proc/ov(icon, px=0, py=0, flags=0, matrix/M=null, layer=6)
    var/mutable_appearance/A = new()
    A.icon = icon; A.layer = layer
    A.pixel_x = px; A.pixel_y = py; A.appearance_flags = flags
    if(M) A.transform = M
    return A
/proc/set_a()  // sampling
    var/obj/O
    for(var/ps in list(0, PIXEL_SCALE))
        var/s = ps ? " ps" : ""
        O = slot("rot45[s]"); O.transform = T(45); O.appearance_flags = ps
        O = slot("scale2[s]"); O.transform = matrix(2,0,0,0,2,0); O.appearance_flags = ps
        O = slot("scale1.5[s]"); O.transform = matrix(1.5,0,0,0,1.5,0); O.appearance_flags = ps
        O = slot("scale0.75[s]"); O.transform = matrix(0.75,0,0,0,0.75,0); O.appearance_flags = ps
        O = slot("half[s]"); O.transform = matrix(0.5,0,0,0,0.5,0); O.appearance_flags = ps
        O = slot("shift.5[s]"); O.transform = matrix(1,0,0.5,0,1,0.5); O.appearance_flags = ps
        O = slot("rot90[s]"); O.transform = T(90); O.appearance_flags = ps
        O = slot("shear[s]"); O.transform = matrix(1,0.5,0,0,1,0); O.appearance_flags = ps
        O = slot("rot30scale[s]"); O.transform = T(30) * 1.25; O.appearance_flags = ps
/proc/set_b()  // children and maptext
    var/obj/O
    O = slot("child16"); O.transform = T(90); O.overlays += ov('small.png', 8, 0)
    O = slot("child48"); O.transform = T(90); O.overlays += ov('big.png', 0, 0)
    O = slot("child48off"); O.transform = matrix(0.75,0,0,0,0.75,0); O.pixel_x = 4; O.overlays += ov('big.png', 6, -4)
    O = slot("deep"); O.transform = T(90)
    var/mutable_appearance/A = ov('small.png', 10, 0, 0, T(45))
    A.overlays += ov('small.png', 0, 10, 0, matrix(1,0,0,0,0.5,0), 7)
    O.overlays += A
    O = slot("reset"); O.transform = T(90); O.overlays += ov('small.png', 10, 4, RESET_TRANSFORM)
    O = slot("childown"); O.transform = matrix(2,0,0,0,1,0); O.overlays += ov('small.png', 0, 8, 0, T(90))
    O = slot("mt-rot"); O.transform = T(90); O.maptext = "<span style='color:#ffffff'>Abc</span>"
    O = slot("mt-scale"); O.transform = matrix(2,0,0,0,2,0); O.maptext = "<span style='color:#ffffff'>Ab</span>"
    O = slot("mt-off"); O.transform = T(90); O.maptext = "<span style='color:#ffff00'>Xy</span>"; O.maptext_x = 8; O.maptext_y = 12
    O = slot("mt-wide"); O.transform = T(45); O.maptext_width = 80; O.maptext = "<span style='color:#00ffff'>Wide text</span>"
    O = slot("mt-shift"); O.transform = matrix(1,0,10,0,1,6); O.maptext = "<span style='color:#ffffff'>Mv</span>"
    O = slot("mt-plain"); O.maptext = "<span style='color:#ffffff'>Abc</span>"
    O = slot("off-rot"); O.transform = T(90); O.pixel_x = 8; O.pixel_y = 4
    O = slot("childoffrot"); O.transform = T(90); O.overlays += ov('small.png', 12, 6); O.underlays += ov('small.png', -8, -6, 0, null, 4)
var/list/pictures = list()
/proc/set_c()  // PIXEL_SCALE between parents, children and images
    var/obj/O
    O = slot("ps-parent"); O.transform = matrix(2,0,0,0,2,0); O.appearance_flags = PIXEL_SCALE; O.overlays += ov('small.png', 10, 0)
    O = slot("ps-child"); O.transform = matrix(2,0,0,0,2,0); O.overlays += ov('small.png', 10, 0, PIXEL_SCALE)
    O = slot("ps-rot-parent"); O.transform = T(30); O.appearance_flags = PIXEL_SCALE; O.overlays += ov('small.png', 10, 0)
    O = slot("ps-rot-child"); O.transform = T(30); O.overlays += ov('small.png', 10, 0, PIXEL_SCALE)
    O = slot("img-ps-target"); O.transform = T(30); O.appearance_flags = PIXEL_SCALE
    var/image/I = image('small.png', O); I.pixel_x = 8; pictures += I
    O = slot("img-ps-own"); O.transform = T(30)
    I = image('small.png', O); I.pixel_x = 8; I.appearance_flags = PIXEL_SCALE; pictures += I
    O = slot("img-plain"); O.transform = T(30)
    I = image('small.png', O); I.pixel_x = 8; pictures += I
    O = slot("reset-ps"); O.transform = matrix(2,0,0,0,2,0); O.appearance_flags = PIXEL_SCALE; O.overlays += ov('small.png', 10, 0, RESET_TRANSFORM, matrix(1.5,0,0,0,1.5,0))
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/T in block(locate(1,1,1), locate(18,12,1))) T.icon = 'floor.png'
    if(params["set"] == "b") set_b()
    else if(params["set"] == "c") set_c()
    else set_a()
/mob
    Login()
        ..()
        loc = locate(9,6,1)
        invisibility = 101
        client.images += pictures
        world.log << "READY [key]"

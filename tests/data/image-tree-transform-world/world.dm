// Images Fugue refused on transformed targets or with their own matrix: an image's own transform (with and without a
// transformed target, RESET_TRANSFORM and offsets), image maptext and image overlays/underlays on transformed targets.
// Cases sit on a 3-tile grid; run with -params "set=a" (default).
world
    name = "Image tree transform probe"
    maxx = 18
    maxy = 12
    maxz = 1
    view = "18x12"
    fps = 10
    mob = /mob/observer
var/list/cases = list()
var/list/pictures = list()
/obj/thing
    icon = 'small.png'
    layer = 5
/proc/slot(label)
    var/i = length(cases)
    var/obj/thing/O = new(locate(2 + (i % 6) * 3, 10 - round(i / 6) * 3, 1))
    O.name = label
    cases += O
    world.log << "CASE [label]"
    return O
/proc/turned(angle)
    var/matrix/M = matrix()
    M.Turn(angle)
    return M
/proc/scaled(x, y)
    var/matrix/M = matrix()
    M.Scale(x, y)
    return M
/proc/picture(obj/O)
    var/image/I = image('shape.png', O)
    I.layer = 6
    pictures += I
    return I
/proc/set_a()
    var/obj/thing/O
    var/image/I
    var/text = "<span style='color:#ffffff'>Ab</span>"
    // The image's own matrix on a plain target.
    O = slot("own-turn"); I = picture(O); I.transform = turned(90)
    O = slot("own-scale"); I = picture(O); I.transform = scaled(2, 1)
    O = slot("own-turn-pixel"); I = picture(O); I.transform = turned(90); I.pixel_x = 8; I.pixel_y = 4
    // Own and target matrix together: Turn(90) then Scale(2,1) is wide, Scale(2,1) then Turn(90) is tall.
    O = slot("own-turn-target-scale"); O.transform = scaled(2, 1); I = picture(O); I.transform = turned(90)
    O = slot("own-scale-target-turn"); O.transform = turned(90); I = picture(O); I.transform = scaled(2, 1)
    O = slot("own-reset"); O.transform = scaled(2, 1); I = picture(O); I.transform = turned(90); I.appearance_flags = RESET_TRANSFORM
    // Maptext on an image on a turned target.
    O = slot("maptext-turn"); O.transform = turned(90); I = picture(O); I.maptext = text
    O = slot("maptext-only-turn"); O.transform = turned(90); I = picture(O); I.icon = null; I.maptext = text
    O = slot("maptext-own-turn"); I = picture(O); I.maptext = text; I.transform = turned(90)
    // Image trees on a turned target.
    O = slot("overlay-turn"); O.transform = turned(90); I = picture(O); I.overlays += image('small.png', pixel_x = 14)
    O = slot("underlay-turn"); O.transform = turned(90); I = picture(O); I.underlays += image('small.png', pixel_x = -10, pixel_y = -10)
    O = slot("overlay-scale"); O.transform = scaled(2, 1); I = picture(O); I.overlays += image('small.png', pixel_x = 14)
    O = slot("overlay-own-turn"); I = picture(O); I.overlays += image('small.png', pixel_x = 14); I.transform = turned(90)
    O = slot("overlay-reset"); O.transform = turned(90); I = picture(O); var/mutable_appearance/A = new(); A.icon = 'small.png'; A.pixel_x = 14; A.appearance_flags = RESET_TRANSFORM; I.overlays += A
    O = slot("overlay-target-overlay"); O.overlays += image('small.png', pixel_y = 14); O.transform = turned(90); I = picture(O); I.overlays += image('small.png', pixel_x = 14)
    O = slot("plain-turn"); O.transform = turned(90); picture(O)
world/New()
    ..()
    world.log = file("events.log")
    for(var/turf/t in block(locate(1,1,1), locate(18,12,1))) t.icon = 'floor.png'
    set_a()
/mob/observer
    invisibility = 101
    Login()
        ..()
        loc = locate(9,6,1)
        client.images += pictures
        world.log << "READY [key]"

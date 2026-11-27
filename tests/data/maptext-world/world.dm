world
    name = "Maptext fixture"
    hub = ""
    view = "14x10"
    maxx = 20
    maxy = 20
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'black.png'

/obj/label
    maptext_width = 96
    maptext_height = 64
    mouse_opacity = 1
    Click(location, control, params)
        text2file("CLICK [name] [params]", "events.log")

/client/proc/label(name, placement, text)
    var/obj/label/o = new
    o.name = name
    o.screen_loc = placement
    o.maptext = text
    screen += o
    return o

/client/New()
    ..()
    mob.loc = locate(10,10,1)
    label("default", "1,8", "Hello")
    label("explicit", "5,8", "<span style='font-family:Arial;font-size:12px;color:white'>Hello</span>")
    label("bottom-right", "9,8", "<span style='font-family:Arial;font-size:12px;text-align:right;vertical-align:bottom;color:red'>Hi</span>")
    label("multiline", "1,5", "<b>Bold</b><br><i>Italic</i><br>&lt;ok&gt;")
    var/obj/label/o = label("overlay", "5,5", "<span style='font-size:24px;color:white'>XX</span>")
    o.icon = 'red.png'
    o.overlays += 'red.png'
    o.maptext_width = 32
    o.maptext_height = 32
    o = label("offset", "9,5", "<span style='font-size:12px;color:lime'>Offset</span>")
    o.maptext_x = -7
    o.maptext_y = 9
    label("center", "1,2", "<span style='font-size:12px;text-align:center;vertical-align:middle'>Center</span>")
    o = label("zero", "5,2", "Hidden")
    o.maptext_width = 0
    label("font", "9,2", "<font face='Arial' size='3' color='#00ffff'>Font</font>")
    o = label("tint", "12,2", "<span style='font-size:12px;color:white'>Tint</span>")
    o.maptext_width = 64
    o.maptext_height = 0
    o.color = "#0000ff"
    o.alpha = 128
    spawn(10)
        text2file("READY", "events.log")

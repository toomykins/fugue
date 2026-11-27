world
    name = "Maptext style fixture"
    hub = ""
    view = "12x8"
    maxx = 20
    maxy = 20
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'gray.png'

/world/New()
    ..()
    log = file("observations.jsonl")

/obj/label
    maptext_width = 96
    maptext_height = 64
    mouse_opacity = 0

/client/proc/label(placement, text)
    var/obj/label/o = new
    o.screen_loc = placement
    o.maptext = text
    screen += o

/client/New()
    ..()
    mob.loc = locate(10,10,1)
    label("1,6", "<span style='font-family:Arial;font-size:24px;vertical-align:middle;text-align:center'>X</span>")
    label("5,6", "<span class='outlined' style='font-family:Arial;font-size:24px;vertical-align:middle;text-align:center'>X</span>")
    label("9,6", "<span class='caption'><span style='text-align:center;vertical-align:middle'>Fixture<br />2 players<br />04:00 in<br /></span></span>")
    label("1,4", "<span style='font-family:Arial;font-size:24pt;vertical-align:middle;text-align:center'>X</span>")
    label("5,4", "<span style='font-family:Arial;font-size:18pt;vertical-align:middle;text-align:center'>X</span>")
    label("1,2", "<span style='font-family:Arial;font-size:12px;line-height:1.0;vertical-align:top'>A<br />B</span>")
    label("5,2", "<span style='font-family:Arial;font-size:12px;line-height:1.5;vertical-align:top'>A<br />B</span>")
    label("9,2", "<span class='caption'><span style='text-align:center;vertical-align:middle;font-size:18px'>Soon</span><br /><span style='text-align:center;vertical-align:middle'>2 players</span></span>")
    spawn(10)
        var/last_probe = ""
        while(src)
            var/text = file2text("probe-request.json")
            if(text && text != last_probe)
                last_probe = text
                probe_reply(json_decode(text))
            sleep(2)

/client/proc/probe_reply(list/request)
    var/action = request["action"]
    if(action != "reset" && action != "fit" && action != "measure")
        return
    if(action == "reset" || action == "fit")
        var/color = action == "reset" ? "lime" : "red"
        var/css = "img.icon { width:auto; height:auto } .outlined { -dm-text-outline:1px black } .caption { font-family:Arial; font-size:8px; color:[color]; line-height:1.0; -dm-text-outline:1px black }"
        winset(src, "main.map", list2params(list("style" = css)))
    if(action == "measure")
        // Stock retains an existing text raster after a style-only change.
        // Change the text without changing visible content to admit the new style.
        for(var/obj/label/o in screen)
            o.maptext += " "
    var/list/measurement = list("map" = winget(src, "main.map", "size"), "dpi" = winget(src, null, "dpi"), "style" = winget(src, "main.map", "style"))
    sleep(3)
    var/count = 0
    for(var/client/C)
        count++
    world.log << "PROBE [json_encode(list("token" = request["token"], "phase" = request["phase"], "action" = action, "clients" = count, "ready" = 1, "measurement" = measurement))]"

world
    name = "Screen map scope fixture"
    hub = ""
    view = "7x5"
    maxx = 15
    maxy = 15
    maxz = 1
    turf = /turf/floor

/turf/floor
    icon = 'black.png'

/obj/ink
    icon = 'red.png'
    plane = 60

/obj/master
    plane = 60
    appearance_flags = PLANE_MASTER
    render_target = "*plate"
    screen_loc = "1,1"

/obj/relay
    plane = 70
    render_source = "*plate"
    screen_loc = "1,1"

/obj/marker
    icon = 'green.png'
    plane = 80
    screen_loc = "1:8,1:4"

/world/New()
    ..()
    log = file("observations.jsonl")
    new /obj/ink(locate(8,8,1))

/proc/budget_icon(state)
    var/icon/result = icon('stress.dmi', "s0")
    result.Blend(icon('stress.dmi', state), ICON_OVERLAY)
    return result

/client/New()
    ..()
    mob.loc = locate(8,8,1)
    screen += new /obj/master
    screen += new /obj/relay
    var/obj/marker/marker = new
    screen += marker
    var/initial_request = file2text("probe-request.json")
    spawn(5)
        var/obj/master/foreign
        var/obj/ink/preview
        var/obj/master/preview_master
        var/obj/relay/preview_relay
        var/last = initial_request
        while(src)
            var/text = file2text("probe-request.json")
            if(text && text != last)
                last = text
                var/list/request = json_decode(text)
                var/phase = request["phase"]
                if(phase != "startup-ready")
                    if(foreign)
                        screen -= foreign
                        del(foreign)
                    marker.screen_loc = "1:8,1:4"
                    if(phase == "preview-leaf" || phase == "preview-group" || phase == "preview-invalid")
                        foreign = new
                        foreign.screen_loc = phase == "preview-invalid" ? "preview:bogus" : "preview:1,1"
                        foreign.alpha = 0
                        if(phase == "preview-group")
                            foreign.appearance_flags |= KEEP_TOGETHER
                        screen += foreign
                    else if(phase == "preview-marker")
                        marker.screen_loc = "preview:1:8,1:4"
                    else if(phase == "explicit-short")
                        marker.screen_loc = "map:1:8,1:4"
                    else if(phase == "explicit-qualified")
                        marker.screen_loc = "main.map:1:8,1:4"
                    else if(phase == "named-open" || phase == "named-reopen" || phase == "budget-open")
                        if(preview)
                            screen -= preview
                            screen -= preview_master
                            screen -= preview_relay
                            del(preview)
                            del(preview_master)
                            del(preview_relay)
                        winset(src, "preview", "is-visible=false")
                        winset(src, "embedded", "parent=main;type=map;pos=256,0;size=160x160")
                        preview = new
                        preview.icon = phase == "budget-open" ? budget_icon("s1") : 'green.png'
                        if(phase == "budget-open")
                            fcopy(preview.icon, "budget-green.dmi")
                        preview.screen_loc = "embedded:1,1"
                        preview_master = new
                        preview_master.screen_loc = "embedded:1,1"
                        preview_relay = new
                        preview_relay.screen_loc = "embedded:1,1"
                        screen += preview
                        screen += preview_master
                        screen += preview_relay
                    else if(phase == "budget-update" || phase == "budget-reuse")
                        preview.icon = budget_icon(phase == "budget-update" ? "s2" : "s1")
                        if(phase == "budget-update")
                            fcopy(preview.icon, "budget-blue.dmi")
                    else if(phase == "budget-safety")
                        var/icon/oversize = budget_icon("s1")
                        oversize.Scale(1024,1024)
                        preview.icon = oversize
                    else if(phase == "named-update")
                        preview.icon = 'blue.png'
                    else if(phase == "named-group")
                        preview.icon = 'green.png'
                        preview.appearance_flags |= KEEP_TOGETHER
                        var/mutable_appearance/escaped = new
                        escaped.icon = 'blue.png'
                        escaped.plane = 80
                        escaped.pixel_x = 16
                        escaped.appearance_flags = KEEP_TOGETHER | KEEP_APART
                        preview.overlays += escaped
                    else if(phase == "named-second-tile")
                        preview.overlays.Cut()
                        preview.appearance_flags &= ~KEEP_TOGETHER
                        preview.icon = 'blue.png'
                        preview.screen_loc = "embedded:2,1"
                    else if(phase == "named-close")
                        winset(src, "embedded", "parent=")
                        screen -= preview
                        screen -= preview_master
                        screen -= preview_relay
                        del(preview)
                        del(preview_master)
                        del(preview_relay)
                // A query round trip plus a settling interval follows every write.
                var/list/measurement = list("map" = winget(src,"map","size"), "preview" = winget(src,"preview","size"), "marker" = marker.screen_loc, "embedded" = winget(src,"embedded","size"), "embedded_view" = winget(src,"embedded","view-size"))
                sleep(5)
                var/count = 0
                for(var/client/C)
                    count++
                world.log << "PROBE [json_encode(list("token" = request["token"], "phase" = phase, "action" = request["action"], "ready" = 1, "clients" = count, "measurement" = measurement))]"
            sleep(2)

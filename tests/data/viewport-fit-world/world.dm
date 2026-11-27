world
    name = "Viewport fit fixture"
    hub = ""
    view = "19x15"
    maxx = 1
    maxy = 1
    maxz = 1

/world/New()
    ..()
    log = file("observations.jsonl")

/client/New()
    ..()
    spawn(10)
        var/last = ""
        var/last_probe = ""
        while(src)
            var/probe_text = file2text("probe-request.json")
            if(probe_text && probe_text != last_probe)
                last_probe = probe_text
                probe_reply(json_decode(probe_text))
            var/request = file2text("request.txt")
            if(request && request != last)
                last = request
                if(findtext(request, "fit-") == 1)
                    fit(request)
                else
                    measure(request)
            sleep(2)

/client/proc/measurement()
    return list("key" = key, "dpi" = winget(src, null, "dpi"), "main" = winget(src, "mainwindow.split", "size"), "map" = winget(src, "mapwindow", "size"), "splitter" = winget(src, "mainwindow.split", "splitter"))

/client/proc/measure(phase)
    var/list/result = measurement()
    result["phase"] = phase
    world.log << "MEASURE [json_encode(result)]"

/client/proc/probe_reply(list/request)
    var/action = request["action"]
    var/phase = request["phase"]
    if(action == "reset")
        winset(src, "mainwindow.split", "splitter=50")
    else if(action == "fit")
        fit(phase)
    else if(action != "measure")
        return
    // Complete a skin-query round trip, not just a native window-visible check.
    var/list/observed = measurement()
    var/count = 0
    for(var/client/C)
        count++
    // json_encode's numeric 1 is used for ready; runner requires this exact value.
    var/list/result = list("token" = request["token"], "phase" = phase, "action" = action, "clients" = count, "ready" = 1, "measurement" = observed)
    world.log << "PROBE [json_encode(result)]"

/client/proc/fit(phase)
    // TG's zero-zoom fit rule, isolated from game initialization and saved state.
    var/list/sizes = params2list(winget(src, "mainwindow.split;mapwindow", "size"))
    var/list/main_size = splittext(sizes["mainwindow.split.size"], "x")
    var/list/map_size = splittext(sizes["mapwindow.size"], "x")
    var/width = text2num(main_size[1])
    var/height = text2num(map_size[2])
    var/desired = min(round(height * 19 / 15), width - 300)
    var/pct = 100 * (desired + 4) / width
    world.log << "FIT [json_encode(list("phase" = phase, "sizes" = sizes, "desired" = desired, "initial_percent" = pct))]"
    winset(src, "mainwindow.split", "splitter=[pct]")
    var/delta
    for(var/safety in 1 to 10)
        map_size = splittext(winget(src, "mapwindow", "size"), "x")
        var/got = text2num(map_size[1])
        if(got == desired)
            break
        if(isnull(delta))
            delta = 100 * (desired - got) / width
        else if((delta > 0 && got > desired) || (delta < 0 && got < desired))
            delta = -delta/2
        pct += delta
        winset(src, "mainwindow.split", "splitter=[pct]")
    measure(phase)

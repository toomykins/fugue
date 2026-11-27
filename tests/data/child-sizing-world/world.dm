world
    name = "CHILD sizing fixture"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/world/New()
    ..()
    log = file("observations.jsonl")

/client/New()
    ..()
    spawn(5)
        var/last = ""
        while(src)
            var/text = file2text("probe-request.json")
            if(text && text != last)
                last = text
                probe_reply(json_decode(text))
            sleep(2)

/client/proc/probe_reply(list/request)
    var/phase = request["phase"]
    if(request["action"] == "reset")
        winset(src, "main.leftonly", "left=first;right=\"\"")
        winset(src, "main.rightonly", "left=\"\";right=second")
    else if(phase == "both")
        winset(src, "main.leftonly", "right=extra_right")
        winset(src, "main.rightonly", "left=extra_left")
    else if(phase == "clear")
        winset(src, "main.leftonly", "right=\"\"")
        winset(src, "main.rightonly", "left=\"\"")
    else if(phase == "swap")
        winset(src, "main.leftonly", "left=\"\";right=first")
        winset(src, "main.rightonly", "right=\"\";left=second")
    else if(phase == "fraction-winset")
        winset(src, "main.fraction", "splitter=97.5")
    var/list/observed = list("dpi" = winget(src, null, "dpi"))
    for(var/id in list("main", "main.leftonly", "main.rightonly", "main.nested", "wrapper.only", "main.fraction", "first.content", "second.content", "chat.browser", "upper.content", "lower.content"))
        var/list/props = list()
        for(var/prop in list("pos", "size", "left", "right", "splitter", "is-vert", "show-splitter"))
            props[prop] = winget(src, id, prop)
        observed[id] = props
    var/count = 0
    for(var/client/C)
        count++
    world.log << "PROBE [json_encode(list("token" = request["token"], "phase" = phase, "action" = request["action"], "clients" = count, "ready" = 1, "measurement" = observed))]"

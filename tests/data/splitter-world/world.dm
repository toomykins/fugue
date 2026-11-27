world
    name = "Splitter fixture"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/world/New()
    ..()
    log = file("measurements.jsonl")

/client/proc/measure(phase)
    sleep(3)
    var/list/data = list("phase" = phase, "dpi" = winget(src, null, "dpi"))
    for(var/id in list("main", "main.outer", "nested", "nested.split", "row", "row.menu", "row.hotkeys", "row.emotes", "info", "info.label"))
        var/list/props = list()
        for(var/prop in list("pos", "size", "inner-size", "min-size", "can-resize", "splitter", "show-splitter"))
            props[prop] = winget(src, id, prop)
        data[id] = props
    world.log << "MEASURE [json_encode(data)]"

/client/New()
    ..()
    spawn(5)
        measure("file")
        for(var/show in list("false", "true"))
            winset(src, "nested.split", "show-splitter=[show]")
            for(var/p in list(0, 1, 2, 3, 5, 10, 50, 98, 100))
                winset(src, "nested.split", "splitter=[p]")
                measure("split-[show]-[p]")
        winset(src, "nested.split", "show-splitter=false;splitter=2")
        for(var/s in list("640x240", "640x880", "1000x440"))
            winset(src, "main", "size=[s]")
            measure("resize-[s]")
        winset(src, "main", "size=640x440")
        for(var/s in list("0x0", "0x20", "0x60"))
            winset(src, "row", "min-size=[s]")
            measure("minimum-[s]")
        world.log << "SPLITTER_DONE"

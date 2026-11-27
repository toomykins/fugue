// Pointer hover probe: which of MouseEntered, MouseExited and MouseMove stock reports, and when. Every mouse proc logs
// world.time, the atom and its arguments to events.log. Compile switches (prepare.py <dir> [CLIENT_ENTER] [CLIENT_MOVE])
// add client/MouseEntered or client/MouseMove; world.params "lag=<ds>" sets world.tick_lag after start.
world
    name = "Hover probe"
    maxx = 7
    maxy = 7
    maxz = 1
    view = 3
    fps = 20
    mob = /mob/observer
    turf = /turf/floor
    New()
        log = file("events.log")
        ..()
        var/lag = text2num(params["lag"])
        if(lag) tick_lag = lag
        new /obj/enter(locate(1,7,1))
        new /obj/move(locate(2,7,1))
        new /obj/both(locate(3,7,1))
        new /obj/none(locate(4,7,1))
        new /obj/enter2(locate(5,7,1))
        new /obj/big(locate(1,4,1))
        new /obj/slider(locate(5,4,1))
        new /obj/glider(locate(6,5,1))
        new /obj/hopper(locate(5,2,1))
        new /obj/exitonly(locate(1,1,1))
        new /obj/dragonly(locate(2,1,1))
        new /obj/downonly(locate(3,1,1))
        new /obj/wheelonly(locate(4,1,1))
        new /obj/moveexit(locate(5,1,1))
        new /obj/clickonly(locate(6,1,1))
        new /obj/enteronly(locate(6,2,1))
        world.log << "START tick_lag=[tick_lag]"

proc/hoverlog(what, atom/A, location, control, params)
    world.log << "[world.time] [what] [A ? A.name : "null"] loc=[location] ctl=[control] [params]"

/mob/observer
    name = "observer"
    Login()
        ..()
        loc = locate(3,4,1)
        client.show_popup_menus = 0
        world.log << "READY [key]"

/turf/floor
    icon = 'floor.png'
    name = "floor"
// Presses, releases and clicks are logged by the client, so that the atoms' own flags come from their hover procs alone.
/client/Click(object, location, control, params)
    hoverlog("CLICK", object, location, control, params)
    ..()
/client/MouseDown(object, location, control, params)
    hoverlog("DOWN", object, location, control, params)
    ..()
/client/MouseUp(object, location, control, params)
    hoverlog("UP", object, location, control, params)
    ..()
/client/MouseDrop(src_object, over_object, src_location, over_location, src_control, over_control, params)
    hoverlog("DROP", src_object, over_location, over_control, "over=[over_object] [params]")
    ..()

// MouseEntered and MouseExited only.
/obj/enter
    name = "enter"
    icon = 'red.png'
    MouseEntered(location, control, params)
        hoverlog("ENTER", src, location, control, params)
    MouseExited(location, control, params)
        hoverlog("EXIT", src, location, control, params)
/obj/enter2
    parent_type = /obj/enter
    name = "enter2"
    icon = 'orange.png'
// MouseMove only.
/obj/move
    name = "move"
    icon = 'green.png'
    MouseMove(location, control, params)
        hoverlog("MOVE", src, location, control, params)
// All three.
/obj/both
    name = "both"
    icon = 'yellow.png'
    MouseEntered(location, control, params)
        hoverlog("ENTER", src, location, control, params)
    MouseExited(location, control, params)
        hoverlog("EXIT", src, location, control, params)
    MouseMove(location, control, params)
        hoverlog("MOVE", src, location, control, params)
// None of them.
/obj/none
    name = "none"
    icon = 'grey.png'
// A 64x64 icon over four tiles, with all three.
/obj/big
    parent_type = /obj/both
    name = "big"
    icon = 'big.png'
// Moves by 2 px every half second on the server (pixel_x 0..6 and back), with all three.
/obj/slider
    parent_type = /obj/both
    name = "slider"
    icon = 'magenta.png'
    New()
        ..()
        spawn()
            var/step = 2
            while(1)
                sleep(5)
                if(pixel_x + step > 6 || pixel_x + step < 0) step = -step
                pixel_x += step
// Glides pixel_x 0..6 and back on the client (animate, looped), with all three.
/obj/glider
    parent_type = /obj/both
    name = "glider"
    icon = 'blue.png'
    New()
        ..()
        spawn(1)
            animate(src, pixel_x = 6, time = 10, loop = -1)
            animate(pixel_x = 0, time = 10)
// Steps between (5,2) and (5,3) every two seconds on the server, with all three.
/obj/hopper
    parent_type = /obj/both
    name = "hopper"
    icon = 'cyan.png'
    New()
        ..()
        spawn()
            while(1)
                sleep(20)
                loc = locate(5, y == 2 ? 3 : 2, 1)
// Flag probes: one proc each, to read the appearance flags each gives on the wire.
/obj/exitonly
    name = "exitonly"
    icon = 'white.png'
    MouseExited(location, control, params)
        hoverlog("EXIT", src, location, control, params)
/obj/dragonly
    name = "dragonly"
    icon = 'white.png'
    MouseDrag(over_object, src_location, over_location, src_control, over_control, params)
        hoverlog("DRAG", src, over_location, over_control, "over=[over_object] [params]")
/obj/downonly
    name = "downonly"
    icon = 'white.png'
    MouseDown(location, control, params)
        hoverlog("ODOWN", src, location, control, params)
/obj/wheelonly
    name = "wheelonly"
    icon = 'white.png'
    MouseWheel(delta_x, delta_y, location, control, params)
        hoverlog("WHEEL", src, location, control, params)
/obj/moveexit
    name = "moveexit"
    icon = 'white.png'
    MouseMove(location, control, params)
        hoverlog("MOVE", src, location, control, params)
    MouseExited(location, control, params)
        hoverlog("EXIT", src, location, control, params)
/obj/clickonly
    name = "clickonly"
    icon = 'white.png'
    Click(location, control, params)
        hoverlog("OCLICK", src, location, control, params)
/obj/enteronly
    name = "enteronly"
    icon = 'white.png'
    MouseEntered(location, control, params)
        hoverlog("ENTER", src, location, control, params)

#ifdef CLIENT_ENTER
/client/MouseEntered(object, location, control, params)
    hoverlog("C-ENTER", object, location, control, params)
    ..()
/client/MouseExited(object, location, control, params)
    hoverlog("C-EXIT", object, location, control, params)
    ..()
#endif
#ifdef CLIENT_MOVE
/client/MouseMove(object, location, control, params)
    hoverlog("C-MOVE", object, location, control, params)
    ..()
#endif

#ifdef EXTRA
// Second round (prepare.py <dir> EXTRA): empty space in the map, a second map, a stat panel and a grid, modifiers and
// right-click menus. Four turfs at 6,6 6,7 7,6 7,7 have no icon (with all three procs, to show whether stock names
// them); island, with all three, stands on the one at 7,6. Every obj has the verb poke, and right-click menus are on.
/turf/void
    name = "void"
    icon = null
    MouseEntered(location, control, params)
        hoverlog("ENTER", src, location, control, params)
    MouseExited(location, control, params)
        hoverlog("EXIT", src, location, control, params)
    MouseMove(location, control, params)
        hoverlog("MOVE", src, location, control, params)
/obj/island
    parent_type = /obj/both
    name = "island"
    icon = 'white.png'
/obj/verb/poke()
    set src in view()
    hoverlog("POKE", src, null, null, "")
// map2: hud2 (all three) at 1,1, nothing at 2,1, and at 3,1 a holder without icon showing the world's `both` through
// vis_contents. The stat panel "Probe" lists statrow (all three) and `both`; the grid lists gridrow (all three) and `both`.
/obj/hud2
    parent_type = /obj/both
    name = "hud2"
    icon = 'red.png'
    screen_loc = "map2:1,1"
/obj/holder
    name = "holder"
    screen_loc = "map2:3,1"
/obj/statrow
    parent_type = /obj/both
    name = "statrow"
    icon = 'orange.png'
/obj/gridrow
    parent_type = /obj/both
    name = "gridrow"
    icon = 'magenta.png'
var/obj/statrow/statrow = new
var/obj/gridrow/gridrow = new   // referenced, so that it outlives being shown
proc/theboth()
    for(var/obj/both/B in locate(3,7,1)) return B
/world/New()
    ..()
    for(var/xy in list("6,6", "6,7", "7,6", "7,7"))
        var/list/p = splittext(xy, ",")
        new /turf/void(locate(text2num(p[1]), text2num(p[2]), 1))
    new /obj/island(locate(7,6,1))
/mob/observer/Login()
    ..()
    client.show_popup_menus = 1
    client.screen += new /obj/hud2
    var/obj/holder/h = new
    h.vis_contents += theboth()
    client.screen += h
    src << output(gridrow, "grid:1,1")
    src << output(theboth(), "grid:1,2")
/client/Stat()
    if(statpanel("Probe"))
        stat("row", statrow)
        stat(theboth())
#endif

#ifdef PANEL
// Third round (prepare.py <dir> EXTRA PANEL): more of the stat panel. "Probe" goes on with plain (no mouse procs),
// bare (all three, no icon), a text row, and swap, which shows swapa or swapb (both with all three); a second tab
// "Other" lists other (all three). Keys, through the skin's macros: F2 shows the other tab (client.statpanel), F3
// swaps the swap row, F4 adds or removes a row at the end whose text changes every tick.
/obj/plain
    name = "plain"
    icon = 'grey.png'
/obj/bare
    parent_type = /obj/both
    name = "bare"
    icon = null
/obj/other
    parent_type = /obj/both
    name = "other"
    icon = 'cyan.png'
/obj/swapa
    parent_type = /obj/both
    name = "swapa"
    icon = 'green.png'
/obj/swapb
    parent_type = /obj/both
    name = "swapb"
    icon = 'blue.png'
var/obj/plain/plain = new
var/obj/bare/bare = new
var/obj/other/other = new
var/obj/swapa/swapa = new
var/obj/swapb/swapb = new
var/swapped = 0
var/ticking = 0
/client/Stat()
    ..()
    if(statpanel("Probe"))
        stat("plain", plain)
        stat("bare", bare)
        stat("text", "just text")
        stat("swap", swapped ? swapb : swapa)
        if(ticking) stat("time", world.time)
    if(statpanel("Other"))
        stat("other", other)
/client/verb/flip()
    set hidden = 1
    statpanel = statpanel == "Other" ? "Probe" : "Other"
    world.log << "[world.time] FLIP [statpanel]"
/client/verb/swap()
    set hidden = 1
    swapped = !swapped
    world.log << "[world.time] SWAP [swapped]"
/client/verb/tick()
    set hidden = 1
    ticking = !ticking
    world.log << "[world.time] TICK [ticking]"
#endif

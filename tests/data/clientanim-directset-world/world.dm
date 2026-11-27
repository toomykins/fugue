// One client-offset tween, observed with a world-space clock and a screen phase cue.
world
    name = "Client direct-set probe"
    view = 5
    fps = 10
    maxx = 21
    maxy = 21
    maxz = 1
    turf = /turf/floor
    mob = /mob/player

turf/floor
    icon = 'probe.dmi'
    icon_state = "floor"
mob/player
    name = "probe"
    icon = 'probe.dmi'
    icon_state = "player"
obj/capture_clock
    icon = 'probe.dmi'
    icon_state = "clock"
    layer = 20
obj/capture_phase
    icon = 'probe.dmi'
    icon_state = "idle"
    screen_loc = "1,1"
    layer = 20
client
    var/obj/capture_phase/capture_phase

var/global/observer_count = 0
var/global/obj/capture_clock/world_clock
proc/note(message)
    text2file("[message]\n", "events.log")

mob/player/Login()
    ..()
    loc = locate(11,11,1)
    client.fps = 60
    client.capture_phase = new
    client.screen += client.capture_phase
    observer_count++
    // READY identifies login order without retaining account names.
    note("READY client-[observer_count]")

world/New()
    ..()
    world_clock = new(locate(6,16,1))
    spawn() capture()

proc/capture()
    // The recorder creates this fresh trigger only after both windows have settled.
    while(observer_count < 2 || !fexists("capture.txt"))
        world << "@[world.time]"
        sleep(1)
    var/token = file2text("capture.txt")
    note("TRIGGER [token]")
    for(var/tick in 0 to 30)
        // Mark the whole following update batch with its server tick.
        world << "@[world.time]"
        if(tick == 0)
            world << "CAPTURE start"
            note("ACTION start [world.time]")
            animate(world_clock, pixel_x = 300, time = 30)
            for(var/client/C)
                C.capture_phase.icon_state = "running"
                animate(C, pixel_x = 32, time = 20)
        if(tick == 5)
            world << "CAPTURE assigned"
            note("ACTION assigned [world.time]")
            for(var/client/C)
                C.pixel_x = 6
                C.capture_phase.icon_state = "assigned"
        if(tick == 20)
            world << "CAPTURE settled"
            note("ACTION settled [world.time]")
            for(var/client/C)
                C.capture_phase.icon_state = "settled"
        if(tick == 30)
            world << "CAPTURE end"
            note("ACTION end [world.time]")
        sleep(1)

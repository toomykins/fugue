// stockprobe.py --live adds this file to the world it runs, after the world's own sources. stockprobe-case.txt holds
// the case to show: a serial number on the first line, then world.params in the form of DreamDaemon's -params. The
// params replace world.params before the world's own world/New() runs, and when the probe writes another case the world
// reboots to show it. A world that reads world.params earlier than world/New() (the initial value of a global, New() of
// an atom placed on a map) does not see the case.
/world/New()
    var/case = file2text("stockprobe-case.txt")
    var/cut = findtext(case, "\n")
    if(cut) params = params2list(copytext(case, cut + 1))
    ..()
    // After the world's own New(), which is where a probe world points world.log at events.log. Nobody has logged in
    // yet, so a READY after this line belongs to this case.
    world.log << "LIVE [copytext(case, 1, cut)]"
    spawn() stockprobe_next_case(case)

/proc/stockprobe_next_case(shown)
    while(file2text("stockprobe-case.txt") == shown) sleep(world.tick_lag)
    world.Reboot()

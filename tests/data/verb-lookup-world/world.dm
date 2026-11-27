world
    name = "Verb lookup fixture"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/world/New()
    ..()
    log = file("observations.log")

/client
    show_verb_panel = FALSE
    var/calls = 0

/client/New()
    ..()
    verbs -= /client/verb/late_probe
    spawn(10)
        src << browse({"<html><body>Verb lookup fixture<script>function run(c){location.href='byond://winset?command='+encodeURIComponent(c);}</script></body></html>"}, "window=main.browser")
        sleep(10)
        call_probe("hidden-first", "Probe-Action")
        call_probe("hidden-repeat", "Probe-Action")
        call_probe("hidden-unknown", "Missing-Probe")
        show_verb_panel = TRUE
        sleep(5)
        call_probe("visible", "Probe-Action")
        show_verb_panel = FALSE
        verbs += /client/verb/late_probe
        sleep(5)
        call_probe("hidden-late", "Late-Probe")
        call_probe("hidden-text", "Probe-Text hello")
        world.log << "DONE"

/client/proc/call_probe(phase, command)
    world.log << "PHASE [phase] command=[command] before=[calls]"
    src << output(command, "main.browser:run")
    sleep(10)
    world.log << "RESULT [phase] calls=[calls]"

/client/verb/probe_action()
    set name = "Probe Action"
    calls++
    world.log << "INVOKED Probe Action [calls]"

/client/verb/probe_text(value as text)
    set name = "Probe Text"
    calls++
    world.log << "INVOKED Probe Text [value] [calls]"

/client/verb/late_probe()
    set name = "Late Probe"
    calls++
    world.log << "INVOKED Late Probe [calls]"

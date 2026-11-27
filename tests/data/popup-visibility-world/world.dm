world
    name = "Popup visibility main"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/client
    var/pings = 0
    var/closes = 0
    var/page = {"<html><head><title>Visibility Fixture</title></head><body>Popup page<script>
        function visibility(v) { location.href = 'byond://winset?id=probe&is-visible=' + v; }
        function ping() { location.href = 'byond://?ping=1'; }
        function title(v) { document.title = v; }
    </script></body></html>"}

/client/proc/phase(name)
    sleep(5)
    var/visible = winget(src, "probe", "is-visible")
    var/title = winget(src, "probe", "title")
    world.log << "PHASE [json_encode(list("phase" = name, "visible" = visible, "title" = title, "pings" = pings, "closes" = closes))]"
    sleep(15)

/client/New()
    ..()
    spawn(5)
        src << browse(page, "window=probe;size=240x160;can_resize=0")
        winset(src, "probe", "on-close=probe-closed")
        phase("shown")
        winset(src, "probe", "is-visible=0")
        phase("world-hidden")
        src << output("", "probe.browser:ping")
        phase("hidden-page-called")
        src << output("true", "probe.browser:visibility")
        phase("page-shown")
        winset(src, "label", "text=unrelated")
        phase("unrelated-write")
        winset(src, "probe", "is-visible=0")
        phase("same-world-hide")
        src << browse(page, "window=probe;size=240x160;can_resize=0")
        phase("rebrowse")
        winset(src, "probe", "is-visible=1")
        phase("world-shown")
        src << output("false", "probe.browser:visibility")
        phase("page-hidden")
        winset(src, "probe", "is-visible=1")
        phase("same-world-show")
        winset(src, "probe", "title=World")
        phase("world-title")
        src << output("Page", "probe.browser:title")
        phase("page-title")
        winset(src, "label", "text=another")
        phase("unrelated-title")
        winset(src, "probe", "title=World")
        phase("same-world-title")
        src << browse(null, "window=probe")
        phase("closed")
        world.log << "POPUP_PROBE_DONE"

/client/Topic(href, list/href_list)
    if(href_list["ping"])
        pings++
        return
    return ..()

/client/verb/probe_closed()
    set name = "probe-closed"
    set hidden = 1
    closes++

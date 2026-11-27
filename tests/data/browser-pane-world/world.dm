world
    name = "Unattached browser pane fixture"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/client
    var/ready_count = 0
    var/first_token

/client/New()
    ..()
    spawn(5)
        var/visible = winget(src, "chatbrowser", "is-visible")
        world.log << "BEFORE browser.is-visible=[visible]"
        src << browse({"<html><body><p id='state'>Loaded before attachment</p><script>
            var token = String(Math.random());
            function ping() { location.href = 'byond://?mounted=1&token=' + token; }
            window.onload = function() { location.href = 'byond://?ready=1&token=' + token; };
        </script></body></html>"}, "window=chatbrowser")

/client/Topic(href, list/href_list)
    if(href_list["ready"])
        ready_count++
        if(ready_count == 1)
            first_token = href_list["token"]
            world.log << "READY before attachment"
            spawn(5)
                winset(src, "main.split", "left=chatpane")
                sleep(5)
                src << output("", "chatbrowser:ping")
        return
    if(href_list["mounted"])
        var/same_document = first_token == href_list["token"]
        world.log << "MOUNTED ready_count=[ready_count] same_document=[same_document]"
        world.log << "BROWSER_PANE_PROBE_DONE"
        return
    return ..()

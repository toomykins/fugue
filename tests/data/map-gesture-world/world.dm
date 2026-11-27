// Synthetic stock/Fugue callback oracle. Compile/run in a separate build directory.
world
    name = "MAP gesture oracle"
    maxx = 7
    maxy = 7
    view = 7
    fps = 20
    turf = /turf/floor
    New()
        ..()
        log = file("callbacks.log")
        new /obj/source(locate(3,4,1))
        new /obj/destination(locate(5,4,1))
        var/obj/blocker/B = new(locate(3,4,1))
        spawn()
            var/last
            while(1)
                var/text = file2text("control.txt")
                if(text && text != last)
                    last = text
                    if(text == "popup-off" || text == "popup-on")
                        for(var/client/C)
                            C.show_popup_menus = (text == "popup-on")
                        world.log << "POPUP [text]"
                    else
                        B.mouse_opacity = text2num(text)
                        world.log << "OPACITY [B.mouse_opacity]"
                sleep(1)
proc/label(atom/A)
    if(!A) return "null"
    return "[A.type]@([A.x],[A.y],[A.z])"
proc/report(event,atom/A,atom/B,sl,ol,sc,oc,p)
    world.log << "[event] src=[label(A)] over=[label(B)] sl=[sl] ol=[ol] sc=[sc] oc=[oc] params=[p]"
turf/floor
    icon = 'floor.png'
obj/source
    icon = 'source.png'
obj/destination
    icon = 'destination.png'
obj/blocker
    name = "world click-through blocker"
    icon = 'destination.png'
    alpha = 100
    plane = 5
    mouse_opacity = 0
obj/hudsource
    icon = 'source.png'
    screen_loc = "2,2"
    plane = 10
obj/huddestination
    icon = 'destination.png'
    screen_loc = "6,2"
    plane = 10
mob
    icon = 'mob.png'
    Login()
        ..()
        loc=locate(4,4,1)
        client.screen += new /obj/hudsource
        client.screen += new /obj/huddestination
        world.log << "READY [key]"
client
    Click(atom/A,l,c,p)
        report("Click",A,null,l,null,c,null,p)
    DblClick(atom/A,l,c,p)
        report("DblClick",A,null,l,null,c,null,p)
    MouseDown(atom/A,l,c,p)
        report("Down",A,null,l,null,c,null,p)
    MouseUp(atom/A,l,c,p)
        report("Up",A,null,l,null,c,null,p)
    MouseDrag(atom/A,atom/B,sl,ol,sc,oc,p)
        report("Drag",A,B,sl,ol,sc,oc,p)
    MouseDrop(atom/A,atom/B,sl,ol,sc,oc,p)
        report("Drop",A,B,sl,ol,sc,oc,p)

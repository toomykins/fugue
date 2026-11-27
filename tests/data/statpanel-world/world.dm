// Isolated INFO/stat-panel fixture, not a right-click verb menu.
world
    name = "INFO thumbnail fixture"
    maxx = 5
    maxy = 5
    maxz = 1
    view = 2
    turf = /turf/floor
/turf/floor
    name = "floor"
    icon = 'base.png'
/obj/token
    name = "token"
    icon = 'base.png'
    New()
        ..()
        overlays += image('overlay.png')
/mob
    var/turf/listed_turf
    var/obj/token/pocket
    Login()
        ..()
        world.log << "LOGIN key=[key]"
        loc = locate(3,3,1)
        new /obj/token(loc)
        pocket = new /obj/token(src) // not a visible MAP atom
        pocket.name = "pocket token"
    Stat()
        statpanel("Controls")
        stat("Hint", "Alt-click a nearby floor/token for contents")
        if(listed_turf && statpanel(listed_turf.name))
            statpanel(listed_turf.name, null, listed_turf)
            for(var/atom/A in listed_turf)
                if(A != src)
                    statpanel(listed_turf.name, null, A)
            stat("In inventory", pocket)
            stat("Text only", "42")
    verb/ChangeAppearance()
        pocket.color = pocket.color ? null : "#88ff88"
    verb/ReplaceRow()
        del(pocket)
        pocket = new /obj/token(src)
        pocket.name = "pocket token"
    verb/ClearRows()
        listed_turf = null
/atom/Click(location, control, params)
    world.log << "CLICK key=[usr.key] ref=\ref[src] name=[name] location=[location] control=[control] params=[params]"
    if(findtext(params, "alt=1"))
        var/turf/T = istype(src, /turf) ? src : loc
        if(istype(T) && get_dist(T, usr) <= 1)
            usr:listed_turf = usr:listed_turf == T ? null : T
            if(usr:listed_turf)
                usr.client.statpanel = T.name
/atom/DblClick(location, control, params)
    world.log << "DOUBLE key=[usr.key] ref=\ref[src] name=[name] location=[location] control=[control] params=[params]"

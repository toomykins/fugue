// Isolated built-in lighting/blackness probe; optional synthetic plane composition.
#ifndef DARKNESS_WORLD_SIZE
#define DARKNESS_WORLD_SIZE 9
#endif
world
    name = "Darkness fixture"
    maxx = DARKNESS_WORLD_SIZE
    maxy = DARKNESS_WORLD_SIZE
    maxz = 1
    view = 4
    fps = 20
    turf = /turf/floor
    area = /area/room
/area/room
    luminosity = 1
/turf/floor
    icon = 'floor.png'
/obj/target
    icon = 'target.png'
/obj/lamp
    icon = 'lamp.png'
var/obj/lamp/lamp
var/obj/target/target
var/phase = ""
/obj/coverage_master
    plane = 0
    appearance_flags = PLANE_MASTER
    screen_loc = "SOUTHWEST"
/obj/coverage_source
    plane = 5
    screen_loc = "SOUTHWEST"
/obj/coverage_backdrop
    plane = -5
    screen_loc = "1,1 to 9,9"
    icon = 'backdrop.png'
    mouse_opacity = 0
/atom/Click(location, control, params)
    world.log << "CLICK [type] [x],[y] [params]"
/proc/setup(config)
    var/list/p = params2list(config)
    for(var/area/A in world)
        A.luminosity = p["ambient"] ? text2num(p["ambient"]) : 1
    for(var/turf/T in world)
        T.opacity = p["wall"] == "1" && T.x == (p["wallx"] ? text2num(p["wallx"]) : 6) && !(p["door"] == "open" && T.y == 5)
        T.icon = T.opacity ? 'wall.png' : 'floor.png'
        if(p["flooricon"] == "none") T.icon = null
        T.plane = p["turfplane"] ? text2num(p["turfplane"]) : 0
    if(!lamp) lamp = new(locate(3,5,1))
    if(!target) target = new(locate(8,5,1))
    target.loc = locate(p["targetx"] ? text2num(p["targetx"]) : 8, p["targety"] ? text2num(p["targety"]) : 5, 1)
    target.pixel_x = p["offset"] ? text2num(p["offset"]) : 0
    target.plane = p["plane"] ? text2num(p["plane"]) : 0
    target.layer = p["layer"] ? text2num(p["layer"]) : 3
    target.icon = p["wide"] == "1" ? 'wide.png' : 'target.png'
    lamp.loc = locate(p["lampx"] ? text2num(p["lampx"]) : 3, p["lampy"] ? text2num(p["lampy"]) : 5, 1)
    lamp.luminosity = p["lamp"] ? text2num(p["lamp"]) : 0
    lamp.invisibility = p["lamp_invisible"] == "1" ? 50 : 0
    for(var/client/C)
        C.mob.loc = locate(p["eyex"] ? text2num(p["eyex"]) : 5, p["eyey"] ? text2num(p["eyey"]) : 5, 1)
        C.mob.see_in_dark = p["see"] ? text2num(p["see"]) : 0
        C.mob.sight = 0
        if(p["sight"] == "blackness") C.mob.sight = SEE_BLACKNESS
        if(p["sight"] == "turfs") C.mob.sight = SEE_TURFS
        if(p["sight"] == "objs") C.mob.sight = SEE_OBJS
        if(p["sight"] == "through") C.mob.sight = SEE_THRU
        if(p["sight"] == "self") C.mob.sight = SEE_SELF
        if(p["sight"] == "ghost") C.mob.sight = SEE_TURFS | SEE_OBJS | SEE_MOBS
        if(p["sight"] == "mobs") C.mob.sight = SEE_MOBS
        if(p["sight"] == "turfobj") C.mob.sight = SEE_TURFS | SEE_OBJS
        if(p["sight"] == "mobobj") C.mob.sight = SEE_MOBS | SEE_OBJS
        if(p["sight"] == "turfmob") C.mob.sight = SEE_TURFS | SEE_MOBS
        if(p["black"] == "1") C.mob.sight |= SEE_BLACKNESS
        C.mob.plane = p["selfplane"] ? text2num(p["selfplane"]) : 0
        C.mob.pixel_x = p["selfoffset"] ? text2num(p["selfoffset"]) : 0
        for(var/obj/H in C.screen)
            if(H.type == /obj) H.plane = p["hudplane"] ? text2num(p["hudplane"]) : 0
        for(var/obj/coverage_master/M in C.screen) del(M)
        for(var/obj/coverage_source/S in C.screen) del(S)
        for(var/obj/coverage_backdrop/B in C.screen) del(B)
        if(p["backdrop"] == "1")
            var/obj/coverage_backdrop/B = new
            if(p["backplane"]) B.plane = text2num(p["backplane"])
            if(p["backlayer"]) B.layer = text2num(p["backlayer"])
            C.screen += B
        if(p["master"])
            var/obj/coverage_master/M = new
            if(p["masterplane"]) M.plane = text2num(p["masterplane"])
            if(p["master"] == "alpha") M.alpha = 128
            if(p["master"] == "zero") M.alpha = 0
            if(p["master"] == "add") M.blend_mode = BLEND_ADD
            if(p["master"] == "multiply") M.blend_mode = BLEND_MULTIPLY
            if(p["master"] == "matrix") M.color = list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 0.25,0,0.5,0)
            if(p["master"] == "target" || p["master"] == "targetalpha")
                M.render_target = p["targetname"] ? p["targetname"] : "*coverage"
                if(p["master"] == "targetalpha") M.alpha = 128
                var/obj/coverage_source/S = new
                S.render_source = M.render_target
                S.mouse_opacity = 0
                if(p["source"] != "none") C.screen += S
                else del(S)
            C.screen += M
        C.eye = C.mob
        C.perspective = EYE_PERSPECTIVE
        C.view = p["view"] ? text2num(p["view"]) : 4
        winset(C,"main", "title=\"Darkness fixture [config]\"")
    world.log << "PHASE [config]"
world/New()
    ..()
    world.log = file("events.log")
    spawn()
        while(TRUE)
            var/config = file2text("phase.txt")
            if(config && config != phase)
                phase = config
                setup(config)
            sleep(1)
/mob/Login()
    ..()
    icon = 'observer.png'
    client.statpanel = "Targets"
    var/obj/H = new
    H.icon = 'hud.png'
    H.screen_loc = "NORTHEAST"
    client.screen += H
    setup(phase ? phase : "ambient=1&see=0&sight=normal")
    world.log << "READY [key]"
/mob/Stat()
    if(statpanel("Targets"))
        stat("Phase",phase)
        stat("see_in_dark",see_in_dark)
        stat(null,target)
        stat(null,lamp)

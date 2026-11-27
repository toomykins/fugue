world
    name = "Vis material fixture"
    view = "7x7"
    maxx = 7
    maxy = 7
    maxz = 1
    turf = /turf/floor
turf/floor
    icon = 'floor.png'
obj/sample
    icon = 'basis.png'
    plane = 1
    mouse_opacity = 0
obj/master
    appearance_flags = PLANE_MASTER
    plane = 1
    screen_loc = "1,1"
mob/Login()
    ..()
    loc = locate(4,4,1)
    invisibility = 101
    spawn() client.run_probe()
client/var/obj/sample/owned_sample
client/var/obj/sample/owned_mask
client/var/obj/owned_holder
client/Del()
    del owned_sample
    del owned_mask
    del owned_holder
    return ..()
client/proc/run_probe()
    var/obj/sample/S = new
    owned_sample = S
    var/turf/T = locate(4,4,1)
    T.vis_contents = list(S)
    var/obj/master/M = new
    screen += M
    var/obj/sample/K = new(locate(4,4,1))
    K.plane = 3
    owned_mask = K
    var/obj/master/N = new
    N.plane = 3
    N.render_target = "*mask"
    screen += N
    var/obj/H = new(locate(4,4,1))
    owned_holder = H
    H.color = "#4080c0"
    H.alpha = 64
    var/last = ""
    while(src)
        var/raw = file2text("probe-request.json")
        if(raw && raw != last)
            last = raw
            var/list/request = json_decode(raw)
            var/phase = request["phase"]
            S.alpha = 255
            S.color = list(0.5,0.125,0,0, 0,0.75,0.125,0, 0.125,0,0.25,0, 0.25,0.5,0.75,0, 0,0,0,1)
            S.appearance_flags = 0
            T.color = null
            T.alpha = 255
            M.appearance_flags = PLANE_MASTER
            M.render_target = null
            M.blend_mode = BLEND_DEFAULT
            M.filters = null
            T.vis_contents = findtext(phase,"holder") ? list() : list(S)
            H.vis_contents = findtext(phase,"holder") ? list(S) : list()
            for(var/obj/relay/R in screen)
                screen -= R
                del R
            if(findtext(phase,"reset"))
                T.color = "#4080c0"
                T.alpha = 64
                S.appearance_flags = RESET_COLOR | RESET_ALPHA | RESET_TRANSFORM
            if(findtext(phase,"half"))
                S.color = list(0.5,0.125,0,0, 0,0.75,0.125,0, 0.125,0,0.25,0, 0.25,0.5,0.75,0, 0,0,0,0.5)
            if(findtext(phase,"multiply")) M.blend_mode = BLEND_MULTIPLY
            if(findtext(phase,"capture"))
                M.appearance_flags |= KEEP_TOGETHER
                M.render_target = "*basis"
                var/obj/relay/R = new
                screen += R
            if(findtext(phase,"mask"))
                M.filters += filter(type="alpha", render_source=findtext(phase,"missing") ? "*missing" : "*mask", flags=findtext(phase,"inverse") ? MASK_INVERSE : 0)
            if(findtext(phase,"color"))
                M.filters += filter(type="color", color=list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0.125,0,0,0))
                M.filters += filter(type="color", color=list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, -0.125,0,0,0))
            if(findtext(phase,"alpha-color"))
                M.filters = list(filter(type="color", color=list(0,0,0,0.3333333, 0,0,0,0.3333333, 0,0,0,0.3333333, 0,0,0,0, 1,1,1,0)))
                if(findtext(phase,"opaque"))
                    M.filters += filter(type="color", color=list(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0, 0,0,0,1))
            sleep(2)
            var/mask_count = 0
            for(var/obj/sample/member in world)
                if(member.plane == 3) mask_count++
            var/list/measurement = list("map"=winget(src,"main.map","size"),"phase"=phase,"mask_members"=mask_count)
            var/count = 0
            for(var/client/C) count++
            text2file("PROBE [json_encode(list("token"=request["token"],"phase"=phase,"action"=request["action"],"clients"=count,"ready"=(count == 1 && mask_count == 1),"measurement"=measurement))]\n","observations.jsonl")
        sleep(1)
obj/relay
    screen_loc = "SOUTHWEST"
    render_source = "*basis"
    plane = 2

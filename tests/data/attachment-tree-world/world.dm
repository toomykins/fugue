world
    name = "Attachment tree fixture"
    view = "7x7"
    maxx = 7
    maxy = 7
    maxz = 1
    turf = /turf/floor
turf/floor
    icon = 'floor.png'
obj/holder
    screen_loc = "3,3"
    mouse_opacity = 0
obj/child
    icon = 'blue.png'
    plane = 1
    layer = 2
    mouse_opacity = 0
mob/Login()
    ..()
    loc = locate(4,4,1)
    invisibility = 101
    spawn() client.run_probe()
client/var/list/owned = list()
client/var/probe_token
client/Click(atom/object,location,control,params)
    var/list/event = list("token"=probe_token,"target"=object?.name,"location_null"=isnull(location),"params"=params2list(params))
    text2file("[json_encode(event)]\n","clicks.jsonl")
client/Del()
    for(var/obj/O in owned)
        del O
    return ..()
client/proc/run_probe()
    var/obj/holder/H = new
    var/obj/child/C = new
    var/obj/child/N = new
    owned = list(H,C,N)
    var/last = ""
    while(src)
        var/raw = file2text("probe-request.json")
        if(raw && raw != last)
            last = raw
            var/list/request = json_decode(raw)
            var/phase = request["phase"]
            probe_token = request["token"]
            screen = list()
            H.loc = null
            H.color = null
            H.alpha = 255
            H.blend_mode = BLEND_DEFAULT
            H.vis_contents = list(C)
            C.loc = null
            C.icon = findtext(phase,"iconless") ? null : 'blue.png'
            C.screen_loc = null
            N.color = null
            N.alpha = 255
            N.appearance_flags = 0
            N.pixel_y = 32
            if(findtext(phase,"tail"))
                C.vis_contents = list(N)
            else
                C.vis_contents = list()
            C.color = null
            C.alpha = 255
            C.mouse_opacity = phase == "input" ? 1 : 0
            C.pixel_x = 0
            C.pixel_y = 0
            C.transform = null
            C.blend_mode = BLEND_DEFAULT
            C.appearance_flags = findtext(phase,"group") ? KEEP_TOGETHER : 0
            var/mutable_appearance/A = new /mutable_appearance('red.png')
            A.pixel_x = 12
            if(findtext(phase,"apart"))
                A.appearance_flags = KEEP_APART
            if(findtext(phase,"reset"))
                A.appearance_flags |= RESET_COLOR | RESET_ALPHA
            if(findtext(phase,"nested"))
                A.appearance_flags |= KEEP_TOGETHER
                A.alpha = 128
                var/mutable_appearance/B = new /mutable_appearance('green.png')
                B.pixel_y = 12
                A.overlays += B
            if(findtext(phase,"inner-layer")) A.layer = 1
            if(findtext(phase,"inner-add")) A.blend_mode = BLEND_ADD
            C.underlays = findtext(phase,"under") ? list(A) : list()
            C.overlays = findtext(phase,"under") ? list() : list(A)
            if(findtext(phase,"half")) C.alpha = 128
            if(findtext(phase,"tint")) C.color = "#8040c0"
            if(findtext(phase,"matrix"))
                C.color = list(0,1,0,0, 0,0,1,0, 1,0,0,0, 0,0,0,1, 0.125,0,0,0)
            if(findtext(phase,"add")) C.blend_mode = BLEND_ADD
            if(findtext(phase,"subtract")) C.blend_mode = BLEND_SUBTRACT
            if(findtext(phase,"multiply")) C.blend_mode = BLEND_MULTIPLY
            if(findtext(phase,"tail-reset"))
                N.appearance_flags = RESET_COLOR | RESET_ALPHA
            if(findtext(phase,"rotate"))
                C.transform = matrix(0,-1,0,1,0,0)
            if(findtext(phase,"shear"))
                C.transform = matrix(1,0.5,0,0,1,0)
            if(findtext(phase,"mirror"))
                C.transform = matrix(-1,0,0,0,1,0)
            if(findtext(phase,"shift"))
                C.transform = matrix(1,0,7,0,1,5)
            if(findtext(phase,"scale"))
                C.transform = matrix(0.5,0,0,0,0.5,0)
            if(findtext(phase,"holder"))
                H.color = "#80c040"
                H.alpha = 128
                H.blend_mode = BLEND_ADD
            if(findtext(phase,"offset"))
                H.pixel_x = 5
                H.pixel_y = 7
                C.pixel_x = 9
                C.pixel_y = 3
            else
                H.pixel_x = 0
                H.pixel_y = 0
            if(findtext(phase,"direct"))
                C.screen_loc = "3,3"
                screen += C
            else if(findtext(phase,"world"))
                H.loc = locate(3,3,1)
            else
                screen += H
            sleep(2)
            var/count = 0
            for(var/client/other) count++
            var/list/measurement = list("map"=winget(src,"main.map","size"),"phase"=phase)
            text2file("PROBE [json_encode(list("token"=request["token"],"phase"=phase,"action"=request["action"],"clients"=count,"ready"=(count == 1),"measurement"=measurement))]\n","observations.jsonl")
        sleep(1)

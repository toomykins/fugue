world
    name = "Captain local fixture"
    view = 4
    maxx = 9
    maxy = 9
    fps = 10
    turf = /turf/floor
    mob = /mob/viewer
world/New()
    ..()
    log = file("observations.jsonl")
turf/floor
    icon = 'z.dmi'
    icon_state = "floor"
    name = "floor"
obj/shape
    icon = 'z.dmi'
    icon_state = "root"
atom/Click(location,control,params)
    world.log << "CLICK [name] loc=[location] [params]"
atom/MouseEntered(location,control,params)
    world.log << "ENTER [name] loc=[location] [params]"
var/global/built=0
mob/viewer/Login()
    ..()
    loc = locate(5,5,1)
    sight = SEE_TURFS | SEE_OBJS | SEE_MOBS
    // Screen membership belongs to each client, even when world geometry
    // already exists for the next stock/native/wire guest.
    var/obj/P=new
    P.plane=-5
    P.appearance_flags=PLANE_MASTER
    P.screen_loc="1,1"
    client.screen+=P
    if(built) return
    built=1
    for(var/i=1,i<=4,i++)
        var/turf/T=locate(2*i,7,1)
        T.name = "producer[i]"
        T.icon_state = "root"
        T.render_target = (i<=2 ? "*capture[i]" : "capture[i]")
        T.vis_flags = VIS_INHERIT_ID
        if(i%2 == 0) T.appearance_flags = KEEP_TOGETHER
        var/obj/shape/U=new
        U.icon_state="under"
        U.plane=FLOAT_PLANE
        var/obj/shape/O=new
        O.icon_state="over"
        O.plane=FLOAT_PLANE
        T.underlays += U
        T.overlays += O
        var/obj/R=new(locate(2*i,5,1))
        R.name="relay[i]"
        R.render_source=T.render_target
        R.appearance_flags=PASS_MOUSE
        del U
        del O
    // Ordinary self-source child: independent from root-only capture.
    var/turf/T=locate(2,3,1)
    T.icon_state="root"
    T.name="selfproducer"
    T.render_target="*self"
    T.vis_flags=VIS_INHERIT_ID
    var/obj/O=new
    O.render_source="*self"
    O.pixel_x=32
    O.plane=13
    T.overlays+=O
    del O
    // Same ID flag on actual vis members, compared with a default-flag member.
    for(var/i=1,i<=2,i++)
        var/obj/shape/H=new(locate(5+2*(i-1),2,1))
        H.name="holder[i]"
        var/obj/shape/C=new
        C.name="child[i]"
        C.icon_state="over"
        C.vis_flags=(i==1 ? VIS_INHERIT_ID : 0)
        H.vis_contents+=C
    // Reduced arrival tree: all geometry is independently generated.
    var/turf/F=locate(8,3,1)
    F.name="reduced"
    F.icon_state="root"
    F.render_target="full"
    F.plane=-5
    F.vis_flags=VIS_INHERIT_ID
    var/obj/shape/U=new
    U.icon_state="under"
    U.plane=-21
    U.layer=10002
    var/obj/B=new
    B.name="blocker"
    B.render_source="full"
    B.plane=13
    B.color="#000000"
    B.invisibility=101
    B.appearance_flags=KEEP_TOGETHER|KEEP_APART|RESET_COLOR|RESET_TRANSFORM
    U.overlays+=B
    var/obj/shape/M=new
    M.plane=10
    M.layer=10
    M.vis_flags=VIS_INHERIT_ID
    M.blend_mode=BLEND_MULTIPLY
    M.color=list(255,255,255,0,255,255,255,0,255,255,255,0,0,0,0,0,0,0,0,255)
    U.overlays+=M
    var/obj/shape/G=new
    G.icon_state="floor"
    G.color="#8589fa"
    G.plane=10
    G.layer=15
    G.screen_loc="1,1"
    G.blend_mode=BLEND_ADD
    G.appearance_flags=KEEP_TOGETHER
    var/obj/shape/C=new
    C.plane=FLOAT_PLANE
    C.layer=FLOAT_LAYER
    C.vis_flags=VIS_INHERIT_ID
    C.blend_mode=BLEND_MULTIPLY
    C.color=list(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,255,0,0,0,0)
    G.overlays+=C
    U.overlays+=G
    F.underlays+=U
    var/obj/R=new(locate(6,3,1))
    R.render_source="full"
    R.plane=14
    R.name="fullrelay"
    del U
    del B
    del M
    del G
    del C
    world.log << "READY"
client/New()
    ..()
    spawn(3)
        var/last=""
        while(src)
            var/t=file2text("probe-request.json")
            if(t && t!=last)
                last=t
                var/list/r=json_decode(t)
                var/list/m=list("map"=winget(src,"main.map","size"),"dpi"=winget(src,null,"dpi"))
                var/count=0
                for(var/client/C) count++
                world.log << "PROBE [json_encode(list("token"=r["token"],"phase"=r["phase"],"action"=r["action"],"clients"=count,"ready"=1,"measurement"=m))]"
            sleep(1)

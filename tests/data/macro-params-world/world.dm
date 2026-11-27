world
    name = "Macro parameter probe"
    hub = ""
    view = "7x5"
    maxx = 7
    maxy = 5
    maxz = 1

/world/New()
    ..()
    log = file("observations.jsonl")

/client/New()
    ..()
    spawn(5)
        winset(src, "main.map", "focus=true")
        world.log << "READY [json_encode(list("dpi" = winget(src, null, "dpi"), "size" = winget(src, "main.map", "size")))]"

/client/verb/KeyDown(k as text, mx as num, my as num, sx as num, sy as num)
    set hidden = 1
    world.log << "KEYDOWN [json_encode(list(k,mx,my,sx,sy))]"

/client/verb/KeyUp(k as text, mx as num, my as num, sx as num, sy as num)
    set hidden = 1
    world.log << "KEYUP [json_encode(list(k,mx,my,sx,sy))]"

/client/verb/Pairs(pos as text, dims as text, qualified as text, upper as text)
    set hidden = 1
    world.log << "PAIRS [json_encode(list(pos,dims,qualified,upper))]"

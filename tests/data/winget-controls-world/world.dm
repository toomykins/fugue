world
    name = "Winget control-list probe"
    hub = ""
    maxx = 1
    maxy = 1
    maxz = 1

/client/New()
    ..()
    spawn(10)
        var/list/controls = list("alpha", "alpha;beta", "main.alpha;main.beta", "beta;alpha", "alpha;missing", "missing;alpha", "missing;absent", "alpha;alpha", "alpha;", ";alpha", "alpha;;beta", "alpha; beta", " alpha;beta ", "alpha&beta", "alpha,beta", "alpha beta", "ALPHA;beta", "main;alpha", "main.*", "missing.*;alpha", "main.*;alpha", "alpha;main.*", ";", "")
        for(var/control in controls)
            for(var/prop in list("size", "size;text", "text", "nonesuch", "size;nonesuch"))
                var/answer = winget(src, control, prop)
                text2file(json_encode(list("control" = control, "params" = prop, "answer" = answer)), "answers.jsonl")
        world.log << "WINGET_PROBE_DONE"

# flick() targets

Independent DreamSeeker/DreamDaemon **516.1689**, 2026-11-23, port 18710. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and two icons whose flick states last 60 s. The flicks fire 2 s
after the second client logs in, so stock and Fugue can be grabbed on one server.
`entitymap_test::flickWireIdentityAndIngest` and `flickGeneratedAndMobTargets` consume the conclusions.

## Independent conclusions

| case | stock draws |
|---|---|
| an obj made this tick, an obj held before | the flick |
| a mob held before | the flick |
| a mob made this tick | its own state: no flick |
| a turf, with or without an icon change in the same tick | the flick |
| a screen object | the flick |
| an obj flicked on another z level, then moved into view | the flick |
| the same flick twice; one flick, then another state | the last flick |
| a `KEEP_TOGETHER` obj at half alpha with a client image, flicked | the flick and the image in one surface |
| the same, not flicked: a float-layer image on a target at layer 5, 3 or 7 | the image over the target at 5 and 3, under it at 7 |
| `flick('other.dmi', obj)` with a matching state name | its own state (not looked into) |

Wire, s2c 0x6c: `[variant u8][0: state \0 | 1: res u32 | 2: state \0, res u32][kind u8][ref u32][variant 0: res u32]`
`[key u16]`. Kind 0 is a mob, 1 a turf (ref = x + maxx * (y + maxy * z), zero-based, no key), 2 an obj. An obj and a mob are preceded by s2c 0xc7 (kind 2 or 3): it makes
an obj's record and binds its appearance, and only checks a mob's. A flick whose record is absent, or held under
another key, is kept for 20 ticks and starts when the record appears; Fugue drops it. No capture shows that hold
changing what is drawn: in 516.1689 a mob flicked before its record arrived never shows the flick, whether the mob
was made that tick (the table above) or existed off-view and moved into view 5, 15 or 30 ticks later, and Fugue
draws the same.

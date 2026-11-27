# Keyboard MAP parameter substitution

Independent DreamSeeker/DreamDaemon **516.1688** fixture for the TG five-argument
keyboard macro failure. `shell_input` tests `mapMacroParameters` and
`fiveArgumentKeyMacrosReachVerbPackets` cover the recovered rules and normal
verb dispatch (narrow and wide slots). No game-name/key-name special case.

## Stock observations (2026-11-02)

The MAP is at (30,20), size 300×200, in a 480×320 client with view 7×5.
The native driver moves the cursor to these **MAP-local** points:

| Point | Bare mouse-pos | Bare size | Quoted mouse-pos | Quoted size |
| --- | --- | --- | --- | --- |
| (100,50) | 100 50 | 300 200 | 100,50 | 300,200 |
| (0,0) | 0 0 | 300 200 | 0,0 | 300,200 |
| (298,198) | 298 198 | 300 200 | 298,198 | 300,200 |
| (-10,-10) | -10 -10 | 300 200 | -10,-10 | 300,200 |
| (120,240) | 120 240 | 300 200 | 120,240 | 300,200 |

Each point checks W press/release, F1 press/release, and a four-text-argument
`Pairs` verb. `map.mouse-pos` and `main.map.mouse-pos` agree;
`MAP.size` resolves case-insensitively. Position is zero-based, top-left,
unclamped, and includes letterboxing: it is **not** inverse-projected world,
icon or screen-loc geometry.

Both sequential native clients passed **25/25 receipts in five phases**:
stock probe `20261102T003626Z-aa4b5677`, Fugue probe
`20261102T003634Z-00a09473`, owned server
`20261102T003623Z-f32649cb` on 15487. Runtime results remain under
`build/macro-params-final/`, not Git. No native failures/retries.

State/geometry policy: stock fresh guest connection with its existing native
preferences; Fugue fresh isolated state directory. Both waited for the fixture's
skin-query READY response. No maximize, resize or refit. At **144 DPI** stock's
480×320 client and 300×200 MAP were physical pixels; Fugue's client was 720×480
physical, with a 300×200 **Qt logical-pixel** MAP. Cursor offsets were scaled
accordingly; this checks matching control-local units, not equal physical geometry
or general stock DPI parity.

## Reproduction

Stage only `world.dme`, `world.dm`, and `skin.dmf` under a new `build/`
directory; compile there with DreamMaker. Launch a separate DreamDaemon on an
unused port with `-safe -invisible`, then connect stock and Fugue sequentially
with separate client state. Collection is manual: place the pointer at the
recorded MAP-local positions, issue the macros declared in `skin.dmf`, and wait
for server receipts before moving again. Record native geometry/DPI and distinguish
physical pixels from Qt logical coordinates. No screenshot oracle is needed.
Close only the clients/server you started. See
[fixture collection](../../../docs/BUILDING.md#reproducing-fixtures).

## Bounds and investigation history

The implementation covers keyboard macro `size`/`mouse-pos` for laid-out MAP
controls by bare/qualified case-insensitive ID, in Qt logical coordinates. Other
properties, unresolved controls, default-control aliases, popup MAPs, general
skin-expression expansion and non-keyboard command expansion are not claimed.
Unsupported placeholders remain literal rather than inventing coordinates.

An initial exploratory `Record(as command_text)` probe produced one empty row
at (0,0) and one delayed row sampled after the next cursor move. Those rows are
not oracles; the final fixture uses typed arguments and receipt-gated phases.
The first C++ verification failed on a test's incorrect argument type masks and
a test-window focus leak (35 passes, 2 failures). Both test issues were corrected;
the subsequent focused run passed 37/37, with zero skips.

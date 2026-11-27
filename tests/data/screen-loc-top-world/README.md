# TOP screen-location alias

Independent stock DreamDaemon / DreamSeeker 516.1688 fixture; no game assets.
Stage with `python -B tests/data/screen-loc-top-world/prepare.py <directory>`, compile
`world.dme` with Dream Maker, then launch the staged world manually on an unused
local port with `-safe -invisible -logself`. Connect a separate
guest. Each `PHASE` log line follows a one-second settling period and leaves two
seconds to capture the owned window. All objects are ordinary `client.screen` roots.

On a 7x5 view with 32-pixel tiles, each consecutive NORTH/TOP pair has an identical
exact-green native pixel mask:

| Placement pair | Logical bottom-left position |
| --- | --- |
| `NORTH,CENTER` / `TOP,CENTER` | (96,128) |
| `NORTH:-70,CENTER:-54` / `TOP:-70,CENTER:-54` | (42,58) |
| `CENTER:-61,NORTH` / `CENTER:-61,TOP` | (35,128) |
| `NORTH-1:-8,CENTER:+29` / `TOP-1:-8,CENTER:+29` | (125,88) |
| `WEST,NORTH TO EAST,NORTH` / `WEST,TOP TO EAST,TOP` | (32*i,128), i=0..6 |

Expected equivalences are transcribed in `EntityMapTest::screenLocationTopAlias`
and `screenTopPlacementAndReuse`. The unconsumed capture-path report is not retained.
Before the correction, Fugue's five TOP phases contained no green pixels while all
five NORTH controls rendered. Afterward all five paired masks matched.
Stock and Fugue have different native window sizing/DPI scaling here: this proves
alias equivalence within each client, **not** cross-client full-window pixel parity.

Only uppercase TOP as an ordinary vertical NORTH alias is added. Single TOP,
other aliases/corner spellings, automatic view expansion and general render-source
anchoring are not covered. Existing grammar, range/work and coordinate bounds apply.
The first observer attempt timed out because the server was launched without
`-logself`; that setup failure is separate from the before-fix rendering failures.
Both initial owned processes were stopped before the correctly logged run.

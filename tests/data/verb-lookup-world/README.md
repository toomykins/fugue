# Negotiated verb-ID widths — stock 516.1688

## Why this fixture exists

TG's `Fit-Viewport` command was rejected locally even though the server advertised
it. Its 0x0011 starts `71 75 00 00 46 69 74 ...`: **u32 ID 30065**, then `Fit
Viewport`. Reading a u16 ID misreads the two zero bytes as empty name/description.
The client's 0x0026 appearance also uses wide verb IDs; fixing only the verb name
parser leaves its source-membership table misaligned.

The experiment deliberately separates procedure-table size, verb-ID magnitude,
`show_verb_panel`, and command invocation. No game files or engine binaries change.

## Reproduce

```
python -B tests/data/verb-lookup-world/prepare.py build/verb-lookup/small 0
python -B tests/data/verb-lookup-world/prepare.py build/verb-lookup/mid 33000
python -B tests/data/verb-lookup-world/prepare.py build/verb-lookup/wide-low 66000 after
python -B tests/data/verb-lookup-world/prepare.py build/verb-lookup/wide-high 66000
```

Compile the staged DMEs. Padding is generated under the build directory, grouped
under dummy datum types to avoid DM's per-parent child-node limit. It is not sent
as tens of thousands of advertised verbs. The `after` option keeps probe verb IDs
low while still making the world's procedure table large.

Launch each synthetic server and a separate stock client manually on checked
unused ports. An optional standalone local capture proxy is:

```
python -B tests/data/verb-lookup-world/capture.py OUTPUT_JSONL LISTEN_PORT OWNED_SERVER_PORT
```

Connect the separately owned client to LISTEN_PORT. The proxy forwards one local
session, retains no credential bodies or nonce/key material, and refuses certificate
rekey rather than guessing. It records verb/appearance evidence plus bounded small
frame data and client-message metadata. Do not use it for unrelated user sessions.
Stock command bodies in the retained corpus have unused /client src-ref bytes;
regressions normalize only those four bytes, comparing the remaining bytes exactly.

The world drives `byond://winset?command=` through its browser and logs every
invocation. Phases cover hidden panel, repeated call, unknown name, visible panel,
re-added verb membership, and a text argument. `Late Probe` is initially advertised
before its removal in New(); this is a membership-refresh test, not a claim of a
previously unseen late advertisement.

## Stock results

`stock-observations.json` retains selected unmodified plaintext frames, handshake
header only, and the server's invocation log. Synthetic servers had no hub/auth
challenge. Desktop: Windows, DreamSeeker/DreamDaemon 516.1688.

| Case | Third handshake flag | 0x003b bit 0x40000000 | Probe Action ID / 0x0011 slot | Result |
| --- | --- | --- | --- | --- |
| small: 0 padding | 1 | clear | 3 / u16 | five intended calls execute; unknown does not |
| mid: 33000 before | 1 | set | 33003 / u16 | same |
| wide_low: 66000 after | 3 | set | 3 / u32 | same; c2s verb slot is also u32 |
| wide_high: 66000 before | 3 | set | 66004 / u32 | stock transmits truncated ID 468 in a u32 slot; none execute |

The third handshake flag's **0x02 bit**, not 0x003b's high flag, selects wide verb
wire slots. A low numeric ID must still occupy four bytes in that mode. Both
0x0011 dictionary IDs and 0x0026 verb-array entries widen; the array count remains
u16. C2s 0x0002 sub-2 uses the same negotiated width. `show_verb_panel=false` does
not suppress these advertisements or make the commands unavailable.

The >65535 stock failure is **not** generalized into an encoder rule. Fugue retains
full IDs in dictionary/source/scope/menu paths, but explicitly refuses dispatch
above 65535 rather than aliasing them or claiming an unverified extension. The
verified TG fix concerns low IDs in wide wire slots.

## Evidence owners and regressions

All paths below are under `build/probes/`:

- Small server `20261025T091534Z-d4d0feaf`, stock replay
  `20261025T091905Z-df41c249`, proxy `20261025T091905Z-4ed9e556`.
- Mid server `20261025T092140Z-15808843`, stock
  `20261025T092140Z-6a1673fd`, proxy `20261025T092140Z-343bb7f5`.
- Wide/low server `20261025T092348Z-141a1dc3`, stock
  `20261025T092348Z-172d3285`, proxy `20261025T092348Z-dd7bb3bf`.
- Wide/high server `20261025T091905Z-3bd29237`, stock
  `20261025T091905Z-31893330`, proxy `20261025T091905Z-a93ee283`.
- Fixed Fugue wide/low native replay `20261025T093153Z-e52da812`: five calls
  execute and `Missing-Probe` is refused. Server log appends a separate matching
  phase sequence in `build/verb-lookup/wide-low/observations.log`.
- Read-only TG pre-fix capture: `build/verb-lookup/tg-fugue-wire.jsonl` (only inspect
  the named verb when investigating this issue); Fugue owner
  `20261025T091617Z-195139aa`, proxy `20261025T091617Z-2a6306cc`.

`tests/verbwidth_test.cpp` replays all four corpora through the real handshake
parser, InputModel, model decoders, appearance/source membership and stat scope.
It checks stock command bytes, pending text-argument completion, narrow↔wide
reconfiguration, malformed/trailing input rejection and high-ID non-aliasing.
The existing narrow-world verb-ranking/override tests remain passing.

The first padding generator exceeded DM's child-node limit; the grouped generator
compiled with zero errors/warnings. That failed compile and its stopped launches
are not successful wire evidence. All fixture clients/servers were stopped;
proxies exited on disconnect. These wire checks do not establish full live-game UI parity.

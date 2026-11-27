# Stock multi-control winget fixture

`dreamseeker-answers.jsonl` contains 120 replies recorded from stock DreamDaemon
and DreamSeeker 516.1688 on Windows, using this world and skin (2026-10-25).
Each line contains the exact control string, property string and returned answer.
No game source or assets are required.

To reproduce, copy these sources to a disposable build directory, compile
`world.dme` with Dream Maker, and host the DMB on an unused local port. Launch a
separate stock DreamSeeker with `byond://localhost:<port> -new -guest`. After login,
the world writes `answers.jsonl` and logs `WINGET_PROBE_DONE`. Use a new output file
for each client; the world appends. Stop only the processes you created.

The stock main client area was 640x480. Native client/window sizing differences
can change main-window values independently of the query grammar. The C++ test
uses that exact client size; labels have fixed geometry and text includes encoding
edge cases. It exercises the actual SkinFeed reply path, including request IDs,
zero/absent flags and deferred replies, without needing BYOND at test time.

Original local recording: `build/winget-controls-probe/stock-answers.jsonl`;
owned stock process: `build/probes/20261025T042308Z-d1ce322f/`.

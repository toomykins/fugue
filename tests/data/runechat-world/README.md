# Runechat replay

This fixture retains the small world's source, 89 decrypted server-to-client
records in `frames.txt`, and three resources in `res/`. The world places a player
beside a speaking NPC, adds a speech bubble and styled maptext to `client.images`,
asks the client to measure the text, animates its alpha, and removes the images.
The wire contains `SAY 1` through `SAY 3` and `MEASURE 96x16 -> 16` output.

The stock client/server version, capture date, frame arrival times, window size,
DPI, and font/display conditions were not retained. There are no stock screenshots
or native presentation/input observations here. The existing numeric and colour
expectations remain assertions in `shell/tests/runechat_test.cpp`; this cleanup
does not turn them into independently captured stock results.

The portable `runechat` suite replays production session and scene preparation with
the retained resources and map-control style. Its checkpoints are found from wire
meaning rather than file line numbers:

- **Fading in:** after `SAY 1`, immediately before the first image animation's
  empty-step end record. All intervening camera updates remain in this prefix.
- **Visible:** the same prefix including that animation-end record.
- **Bubble deleted:** through the first image deletion after `SAY 2`, before the
  next bubble is added.
- **Measure request:** the first retained `MeasureText` request.

Every included frame receives the same synthetic arrival time,
`1,000,000,000,000,000,000 ns`. Fade samples occur 50, 200 and 250 ms later; visible
and deletion samples occur 10 seconds later. These are the historical replay
clocks, not measured stock timing. Each test starts with fresh scene caches; only
successive fade samples share caches. The marker check inserts `0xff 0x16` before
the first message in a copy of the appearance records and compares the resulting
text image with the unmodified replay.

The assertions cover world-relative placement, gold text and black outlines,
alpha progression without rerasterization, deletion, marker invisibility, and a
measurement reply using the drawing layout. Glyphs are rasterized by Qt; these
checks do not claim stock GDI glyph or metric parity. No additional GPU or native
integration check is established by this fixture. Stock pixel, timing, or native
claims need separate observations with their capture conditions retained.

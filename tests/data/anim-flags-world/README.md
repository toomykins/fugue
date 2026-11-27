# animate() flags

Independent DreamSeeker/DreamDaemon **516.1688**, 2026-11-14, port 18626. No game assets or screenshots in Git.
`prepare.py <dir>` writes the world, a 576x384 skin and the small solid icons. The cases start one second apart once a
client logs in, so their animate packets arrive in CASE order. `tools/stockprobe/stockprobe.py pair` (the recorded run took 36 grabs in a row, five seconds after login) grabs
stock and Fugue at 1:1 every 500 ms; the facts in `stock.json` were read from the box's position and colour on each
case tile, and the wire records from Fugue's 0x00f0 decoder with a temporary trace (not committed).
`entitymap_test::animationSliceLikeStock` consumes them.

## Independent conclusions

- **ANIMATION_SLICE** is the only one of the three that reaches the client as a flag. The server resends the chain
  behind a leading slice step: from = to = 0xffff, flag 0x8, `time` = the slice's length, `loop` = how often it plays,
  and the trailing f32 = where the slice starts in the sequence (the call's `delay`). The sequence then plays that
  window over and over from the slice call (`slice`: a 1 s window from 1 s into a 4 s back-and-forth, so pixel_x stays
  within 16..32); when the loops run out it holds at the window's end, not at the settled appearance (`slice-once`
  holds at 31-32 px, measured as 31 and 32 in two runs).
- **ANIMATION_CONTINUE** arrives as an ordinary chain started in the past (delay -10) whose new step follows the old
  one; a call without it arrives as the old step plus a head step starting now (`replace`).
- **ANIMATION_END_LOOP** arrives as an empty chain at the call, and stock stops at once on the settled appearance.
- **Parallel colour steps** blend linearly towards their target as a multiplier on the base colour, in step with the
  sequence they run beside (`parallel-color`, `parallel-color-base`); Fugue already agreed.

## Fugue after the change

The decoder keeps the trailing f32 (`AnimStep::extBits`), admission accepts flag 0x8 on a well-formed leading slice
step only, and `composeChain` evaluates the sequence behind it at the slice's time. Sliced atoms used to be refused and
drew nothing; they now loop within the same window. CONTINUE, END_LOOP and parallel colour already matched.

## Not covered

Grabs are not paired by a clock reading, so the slice's phase is compared through its window and step size, not frame
by frame. A slice of a chain with parallel or later head sequences, nested slices and slices with easing.

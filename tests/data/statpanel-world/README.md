# INFO thumbnail fixture

This fixture uses server `Stat()` rows, not a right-click verb menu. Alt-click a
nearby floor/token to list its contents. A second atom row names an inventory
object which is not a visible MAP atom. Tokens have a blue base and yellow overlay.
`ChangeAppearance`, `ReplaceRow`, and `ClearRows` exercise updates without changing
the visible row name. Click logs include the actual target reference, client key,
location, control and params.

Prepare outside the source tree (Python standard library only):

```sh
python -B tests/data/statpanel-world/prepare.py build/statpanel-world
# Using the locally installed BYOND compiler:
DreamMaker build/statpanel-world/world.dme
```

Launch DreamDaemon from that runtime directory on a checked, unused port, then
launch a separate native client manually. Do not use or stop the
user's port-1337 server. Stock clients may inherit the running pager's identity;
do not log the user out or change their settings to work around this.

Checks:

- Alt-click creates a contents tab with floor/token thumbnails and a text-only row.
- Both icon and text clicks name the same atom reference and the INFO control.
- With an icon, coordinates are one-based, bottom-origin and **unclamped** on the
  text. Without an icon, no icon coordinates are sent.
- Left/middle, Ctrl/Shift/Alt and click/double-click/click retain their wire meaning;
  right clicks send no INFO atom click.
- Appearance changes replace pixels; replacement/clear invalidates pending clicks.
- At effective scale 100/150/200%, the thumbnail cell is 32/48/64 physical pixels.

Automated worker/widget regressions are in `shell/tests/statview_test.cpp` and
`shell/tests/prompt_test.cpp`. Delayed resource delivery is tested at the worker
read callback, not simulated by delaying the whole server.

This reproduction source was retained after the initial disposable native probe;
its prepare/compile/run steps were not exercised at that checkpoint. The worker
and widget tests above are separate from native fixture validation. See
[INFO rendering](../../../docs/RENDERING.md#maptext-and-info) for the current scope.

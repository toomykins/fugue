# Architecture

## Modules

| Directory | Contents |
|---|---|
| `net/` | wire protocol: framing, sequence numbers, body cipher, handshake, resource ids and cipher, the session, resource cache and download pump. No Qt GUI. |
| `proto/` | message decoders: one struct per message in `messages.h`, read in wire order by one `dec_<dir>_<type>_<name>.cpp` per message type, which also renders it as canonical JSON for the decoder registry and the development tools |
| `model/` | the world model built from the decoded message structs (map, atoms, appearances, HUD, stat panel, verbs, browsers, chat) |
| `skin/` | `.dmf` skin parser, layout, `winset` / `winget` rules, macros |
| `audio/` | sound: Ogg Vorbis (vendored stb_vorbis), WAV, tracker modules (libopenmpt), the channel mixer, and the output sinks (Core Audio on macOS, external PCM players on Linux) |
| `shell/` | the Qt Widgets/WebEngine window, worker scene preparation, QRhi MAP renderer, stat panel, input, HTML browser panes and loopback origin |
| `account/` | account files, `key.txt` handling, login records, hub primitives |
| `pager/` | the launcher: server list, sign-in, join logic, DreamSeeker command-line compatibility, window UI |
| `app/` | the `fugue-qt` executable: the `pager` and `client` modes |
| `tools/` | the canonical JSON writer |
| `tests/`, `*/tests/` | tests and their recorded data |
| `platform/` | shared Linux/macOS socket and close-on-exec pipe helpers |
| `packaging/flatpak/` | Flatpak manifest, desktop entry, AppStream metadata, icons and their sources, repository publishing script |
| `packaging/macos/` | app bundle metadata, icon generation, Qt/WebEngine deployment and dependency relocation |
| `cmake/` | the `fugue_module()` helper |

`pager/` and `account/` have their own `README.md` with a file table.

## Threads

* The GUI thread owns widgets and QRhi presentation resources. MAP rendering and picking consume immutable published
  frames and a shared model-free composition plan, never the world model.
* `SessionWorker` owns the network session, the world model and the resource store. It is the model's only writer and
  only reader, and the only writer of the socket. It ingests network frames, prepares MAP scenes and publishes immutable
  values through signals and latest-frame mailboxes. GUI actions reach it as plain values posted into a mutex-guarded
  inbox; the worker revalidates input identities and lifetimes. See [map rendering](RENDERING.md).
* Audio has a decoder thread and an output thread; neither touches the model or the socket.
* Sign-in and the hub token mint block on a short hub connection with a deadline, on a worker thread.

## Hub traffic

Fugue never polls the hub or any other byond.com endpoint. It dials the hub for a sign-in (one connection per request
from the player, no automatic retry) and, when a hub-registered server asks for it, to confirm the player's sign-in.
Those requests are rate-limited and back off after failures (see `HubThrottle` in `pager/hublink.h`). The server list is a local file,
not fetched.

## The HTML surface

Many BYOND games draw part of their interface as HTML: `browse()` windows, a chat output pane, browser controls in the
skin. Fugue hosts that HTML in Qt WebEngine. `SessionWorker` turns `browse`, `browse_rsc` and output-to-browser messages
into signals; `shell::BrowseHost` on the GUI thread owns every page.

A page goes into the skin's BROWSER control of that name if one exists (never floated); otherwise into a top-level
dialog transient for the main window. A page for a control the skin declares `is-visible = false` runs windowless, and
when the control is shown it takes over that running page rather than reloading it. `output(file, "win.ctl")` arrives
as a file push with `##action=output_file` and is that control's page; it never opens a window of its own.
`output(value, "win.ctl:fn")` calls `fn` in the page, whatever the function is called; only the chat page's own
`:output` is chat. Dotted member calls bind their receiver and JSON-quote arguments.
Runtime BROWSER controls participate in skin layout and retain hidden preloaded pages.
Closing a popup runs the world's `on-close` command,
and a closed popup is hidden, not destroyed, as in stock DreamSeeker. A popup's title follows the page's `<title>` and
the world's `winset`; the loopback address is never shown.

Pages are served from a loopback origin, `http://127.0.0.1:<port>/<per-session secret>/<file>`, with the Host header
checked. It serves pages and `browse_rsc` files by name or alias. A name the world announced whose bytes are still
arriving is held for a bounded time, because a page never retries a 404'd stylesheet or image; the number of held
requests is capped so the browser always keeps connections free.

A page talks to the client by `byond://` navigation (a Topic, `winset`, `winget` with a callback) or by a bridge POST
(`cef_to_byond`). For a `byond://` navigation to reach the client, the profile needs a URL-scheme handler for it as well
as a registered scheme. Calls from the server into a page are queued until the page's own DOM-ready handlers have run,
then flushed in order. Ctrl+F opens a find bar only once the world has enabled it through `browser-options`, as stock
DreamSeeker does. Trusted pages also receive ordered Promise-based uppercase
`BYOND.winget/winset` and synchronous session-origin `domainStorage` caching.
HTML hover tooltips use the same input/browser/skin path. Domain storage is session-local, not persistent native storage.

## The resource cache

A world ships its icons, sounds and HTML assets in a resource file that the server sends to every client on join.
Fugue keeps what it receives in an on-disk cache (`net/rscstore`) so a second join downloads nothing.

* The cache is content-addressed by BYOND's own resource id, a CRC-32 over the file bytes. A store refuses bytes that do
  not hash to the id and deletes a file that fails re-hashing, so an entry cannot go stale, is shared by every world and
  server that sends the same file, and cannot be poisoned.
* Layout: `<dir>/<2 hex>/<8 hex>` holds the raw bytes and a `names` sidecar maps ids to names. Directories are mode 0700
  and files 0600, writes go to a temporary file renamed into place, and the cache is trimmed to 1 GiB by least recent
  use. The location is `--rsc-dir`, else `<--state-dir>/rsc`, else `$HOME/.fugue/rsc`.
* A warm join declares only what the cache lacks. An entry with no recorded name is still declared, because the
  server's offer is the only source of the name a `browse_rsc` page refers to.

On a cold join the server answers each resource declaration with one offer, sent after a single pull, so declaring one id
per message would cost a round trip per file. The download pump (`net/respump`) therefore declares ids in batches of 1 to
32 ids (about 1 MiB), keeps one batch waiting behind the one being answered, and matches each offer to its batch by content.
Files the client needs now (a playing sound, the window icon, `browse_rsc` assets) are declared ahead of
the background fill. A declaration past its expiry keeps its place in the batch order, because dropping it would shift every
later offer onto the wrong batch.

`fugue-qt client --connect host:port --prefetch` pulls the whole list into the cache and exits without entering the
world. Joining while a prefetch runs stops it first; every file is already stored, so the next pass resumes.

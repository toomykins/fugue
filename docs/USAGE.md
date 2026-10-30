# Usage

## Commands

```bash
fugue-qt                              # release build: the launcher window (server list, sign-in)
fugue-qt pager byond://host:port      # the launcher with a server preselected
fugue-qt pager -join host:port        # headless: join and stream the client's log
fugue-qt client --connect host:port   # open the game window directly (as a guest)
fugue-qt --help                       # options (also `pager --help`, `client --help`)
fugue-qt webcheck                     # self-test: starts WebEngine, runs one page and prints "webcheck ok" (install checks)
fugue-qt --version                    # prints e.g. "fugue-qt 0.5.6" (accepted by every mode; also `fugue-qt version`)
```

On macOS, use `Fugue.app/Contents/MacOS/Fugue` in place of `fugue-qt`, or open the app from Finder.

* `pager -list` prints the saved server list and a readiness checklist. `pager -dry-run -join host:port` prints the
  command that would run.
* `pager -warm -join host:port` downloads the world's whole resource list into the cache and exits. The launcher
  window has the same action, **Download resources**.
* `pager -login` signs in with a BYOND account. The password comes from `$FUGUE_BYOND_PASSWORD` or a prompt and is
  stored nowhere. The launcher window has the same sign-in.
* `pager -install-desktop` adds Fugue to the Linux application menu and makes it the handler for `byond://` links. It
  writes only under `$XDG_DATA_HOME` (default `~/.local/share`). On Windows it registers the URL handler under
  `HKEY_CURRENT_USER\Software\Classes\byond` instead (no administrator rights or Start menu shortcut), as
  `"<exe>" pager --url "%1"`. In `--url` mode the link must be the only argument; a link holding a quote, a space or a
  control character is refused rather than split into pager flags.
  On macOS, copy `Fugue.app` to Applications and open it to register `byond://` links instead.
* Installed or symlinked under the name `dreamseeker`, the binary accepts DreamSeeker's own command line, for example
  `dreamseeker "byond://host:port" -pager`, so existing launch scripts and `byond://` handlers work.
* Guests and signed-in players both work; signing in lets servers that require a BYOND key accept you.
* A game started from the launcher, or as `dreamseeker`, plays as the signed-in account. `client --connect`,
  `--state-dir` and `--screenshot` runs play as a guest unless `--account` names one.
* Closing the launcher window while a game runs hides it to a tray icon (Open Fugue, Exit) and the game keeps running;
  the window comes back when the game ends. A desktop with no tray, such as GNOME without an AppIndicator extension,
  minimizes it instead. **Exit**, in the gear menu or the tray (or Cmd+Q on macOS), asks before it closes a running game.
* Starting Fugue again, or clicking a `byond://` link, while the launcher runs shows the running launcher (and opens the
  link there) instead of starting a second one.
* Logout keeps the saved sign-in (`identity-<key>`); **Preferences > Clear cache** removes saved sign-ins, the
  resource cache and the login-certificate cache, and keeps `pager.json` and the machine id.

## Experimental GPU rendering

A build configured with `-DFUGUE_BUILD_GPU=ON` can draw the map with the GPU: start with `--renderer=gpu`, or pick
**Client > Renderer** in the window-icon menu or in Options & Messages, which switches without reconnecting, or tick
**Use GPU rendering (experimental)** in the pager's Preferences, which is the same setting and applies from the next
join. The same menu has **Performance metrics**, an overlay of per-map timings. Both choices are kept in `display.ini`
in the state directory (`~/.fugue`, or `--state-dir`); `--renderer` overrides the saved choice for one launch. A list
the GPU cannot draw is drawn on the CPU. Recording the GPU commands runs on the GUI thread, so a heavy scene can delay
input. A build without GPU support refuses `--renderer=gpu`.

## For server operators

Fugue announces itself in two ways. Its `client.computer_id` always satisfies `text2num(computer_id) % 1000 == 523`, and
`winget(src, null, "fugue-version")` answers `fugue-qt <version>` (a development build answers `fugue-qt <version>-dev`),
where stock DreamSeeker answers the empty string. `winget` waits for the client's reply, so call it from a `spawn()`:

```dm
/client/New()
	. = ..()
	spawn()
		var/v = winget(src, null, "fugue-version")
		if(v)
			log_admin("[key] is using [v]")
```

Both are disclosure, not enforcement (see [SECURITY.md](../SECURITY.md)).

## State directories

All under `$HOME` on Linux/macOS, or `%USERPROFILE%` on Windows when `HOME` is unset. Thus a normal Windows install uses
`C:\Users\<user>\.fugue`. An explicit `HOME` still wins, including in tests. Windows also searches
`Documents/BYOND/cfg/key.txt` and `Documents/BYOND/key.txt` for existing keys.

The table uses `~` for this home directory:

| Path | Contents |
|---|---|
| `~/.fugue/` | `pager.json` (server list, settings), `identity-<key>` (cached sign-ins), `key.txt`, `byond-sid` and `sid` (machine ids; `sid` is the one servers see, and lives under `$XDG_STATE_HOME/fugue/` when that is set), `cauth.txt` (login-certificate cache), `macros.dmf` and `macros/<host>_<port>/macros.dmf` (global and per-server user macros), `qt/` (launcher flags such as the remembered guest choice), `rsc/` (the shared resource cache), `audio.log`, `session.log` (why each game session ended), `client.log` |
| `~/.byond/cfg/key.txt` | an existing BYOND key, imported at sign-in. If that file exists, sign-in updates it in place. |

## Environment

A command-line flag, where one exists, wins.

| Variable | Meaning |
|---|---|
| `FUGUE_AUDIO` | audio sink; same as `--audio` |
| `FUGUE_GPU_BACKEND` | GPU builds only: renderer backend, `d3d11`, `metal` or `opengl` (default: the platform's) |
| `FUGUE_GPU_SOFTWARE` | GPU builds only: `1` prefers a software GPU adapter (D3D11 WARP) |
| `FUGUE_SCENE_PROFILE` | `1`: log the main map's scene-assembly time and appearance-cache hits once a second |
| `FUGUE_SCENE_TRACE` | path: a CSV row per map per assembly (time, cache hits, parses, cache size) |
| `FUGUE_IDENTITY` | path of the identity file to log in with |
| `FUGUE_PAGER_CONFIG` | path of `pager.json` |
| `FUGUE_BYOND_SID` | path of the `byond-sid` file |
| `FUGUE_KEYTXT` | path of the `key.txt` to use |
| `FUGUE_CLIENT` | client binary the launcher starts (default: itself) |
| `FUGUE_BYOND_PASSWORD` | sign-in password for `pager -login`; read from the environment only and never passed on to the client |
| `FUGUE_FONT_DIR` | a folder holding DreamSeeker's bitmap font files, looked in first (see below) |

## Map text fonts

DreamSeeker draws map text in Windows bitmap fonts: Small Fonts (`smalle.fon`, the face most SS13 servers use for
chat over heads and screen text), MS Sans Serif (`sserife.fon`, the default) and Courier (`coure.fon`). Fugue does not
include them. It reads the host's copies from `$FUGUE_FONT_DIR`, `~/.fugue/fonts`, Windows' font folder or Wine's
font folder (`/usr/share/wine/fonts`, from the `fonts-wine` package on Debian and Ubuntu; `/opt/wine-*/share/wine/fonts`;
on macOS also Homebrew's and the Wine app bundles' font folders, `~/Library/Fonts` and `/Library/Fonts`),
and prints `[maptext] bitmap face from <path>` for each one it finds. Without them, text is laid out the same way but
drawn in a stand-in face. Wine's files only have Small Fonts at 7pt, so smaller Small Fonts text is drawn in the
stand-in. A file whose character widths differ from the ones Fugue lays the text
out with is not used for that size, so letters never overlap or drift apart. Under Flatpak, `~/.fugue` is
`~/.var/app/io.github.toomykins.Fugue/.fugue`.

## Audio

`--audio` or `FUGUE_AUDIO` selects the sink:

* `auto` (default): Core Audio on macOS; the default playback device via waveOut on Windows; on Linux, tries `pw-play`, then `paplay`, then `aplay`
* `none`
* `wav:<path>`: write a WAV file instead of playing
* `cmd:prog,arg,...`: Linux/macOS only; pipe raw signed 16-bit little-endian stereo PCM to a command

Every `[audio]` line (which sink opened, why earlier candidates failed, refused files) is also written to `audio.log`
in the state directory, which helps when Fugue is started from a menu with no terminal.

When the connection drops, Fugue does what DreamSeeker does: it says so in the output pane and tries once more after
10 seconds, but not after a kick or a timeout. After a world reboot it logs in again by itself. Client > Reconnect (in
the menu behind the window icon at the top left) or a `.reconnect` command reconnects by hand. Each session's end, and
why, is appended to `session.log` in the state directory.

// The pager's launch side: join = re-exec THIS binary as `<self> client ...` with argv only, plus the desktop entry.
// The client never reads a password. The pager hands over argv flags and the path of the 0600 identity file, never the
// identity bytes, a password or an env secret. The child's environment is the pager's minus every password variable.
// Child flags: --connect, --account, --hub, --identity-file.
// InstallDesktop registers the byond:// handler by writing $XDG_DATA_HOME/applications/mimeapps.list, not by running
// xdg-mime (which writes under XDG_CONFIG_HOME).
#pragma once
#include <QByteArray>
#include <QProcessEnvironment>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "account/store.h"

namespace fugue::pager {

// The flags after `<self> client`. NEVER carries a password or identity bytes.
std::vector<std::string> buildArgs(const account::Config& cfg, const account::Server& srv);

// argv prefix + buildArgs + passthrough. Prefix: explicit/$FUGUE_CLIENT, else {selfExe, "client"}.
bool buildCommand(const account::Config& cfg, const account::Server& srv, const std::string& selfExe,
                  const std::vector<std::string>& passthrough, std::vector<std::string>* argv, std::string* err);

// The line `-dry-run` prints: the exact argv, space-joined. It contains no secret by construction; if the pager holds a
// password variable (FUGUE_BYOND_PASSWORD) it is mentioned by NAME only, as a note that it is withheld from the child.
std::string displayCommand(const std::vector<std::string>& argv);

// The environment a child gets: the pager's, minus every password variable.
QProcessEnvironment childEnvironment();
extern const char* const kPasswordVars[];  // names only

// Runs this binary again as argv0 + args with childEnvironment() plus extraEnv ("NAME=value") and only stdin/stdout/stderr
// open: in place of this process (returns only on failure), or detached.
bool runSelf(const std::string& argv0, const std::vector<std::string>& args, bool detach, std::string* err = nullptr,
             const std::vector<std::string>& extraEnv = {});

// ~/.fugue/client.log (HOME-relative); "" if no HOME.
std::string clientLogPath();

// "Play as guest" is remembered, so a guest is not sent to the Sign in window on every launch. A marker file under
// ~/.fugue/qt (never in pager.json, which is rewritten without unknown keys). Written when guest is picked, removed by a
// successful sign-in. It matters only while no account is saved.
std::string guestChoicePath();  // ~/.fugue/qt/pager-guest; "" if no HOME
bool guestRemembered();
bool rememberGuest(bool on, std::string* err = nullptr);

// Preferences > "Sign in automatically when Fugue starts" (stock BYOND's login dialog has the same option). ON by default: a
// saved account with a saved sign-in opens signed in. OFF (a marker file next to pager-guest; a setting, so Clear cache keeps
// it): the pager opens on the Sign in window with the account picked, and plays as nobody until the player confirms (no
// password: the identity is kept).
std::string autoSignInOffPath();  // ~/.fugue/qt/pager-no-autosignin; "" if no HOME
bool autoSignIn();
bool setAutoSignIn(bool on, std::string* err = nullptr);

// Headless join: run argv, stream its merged output to `out` (and client.log, 0600), forward SIGINT/SIGTERM as SIGINT to the
// child, return the child's exit code. Prints "[pager] client pid N" first so a caller can inspect /proc/N.
int runHeadless(const std::vector<std::string>& argv, FILE* out);

// ---- desktop integration ----
// The app id: the Flatpak's .desktop file has the same name, so a desktop that matches
// windows to .desktop files by app id (GNOME; any Wayland compositor) finds this one and its Icon=.
const char* const kAppId = "io.github.toomykins.Fugue";
const char* const kDesktopFile = "io.github.toomykins.Fugue.desktop";
const char* const kLegacyDesktopFile = "fugue.desktop";  // a second Fugue entry: removed only if its content is ours
std::string desktopQuote(const std::string& s);
std::string desktopEntry(const std::string& exe);
// The Windows byond:// handler command: `"<exe>" pager --url "%1"`.
std::string urlHandlerCommand(const std::string& exe);
// `--url <link>`, the handler's whole argument list: the link, or nullopt when there is anything else, or the link holds
// a quote, whitespace or a control character (it could have been split into extra arguments).
std::optional<std::string> handlerUrl(const std::vector<std::string>& args, std::string* err = nullptr);
struct DesktopResult {
    std::string desktopPath, mimeappsPath, err;
    std::vector<std::string> iconPaths;
    bool ok = false;
};
// Writes ONLY under $XDG_DATA_HOME (default ~/.local/share): applications/<app id>.desktop,
// applications/mimeapps.list, and each of `icons` (size, PNG bytes) as icons/hicolor/<s>x<s>/apps/<app id>.png
// - the Flatpak's own hicolor layout, so Icon=<app id> resolves the same way.
DesktopResult installDesktop(const std::string& exe, const std::vector<std::pair<int, QByteArray>>& icons = {});

}  // namespace fugue::pager

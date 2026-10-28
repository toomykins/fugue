// skin/winset.h - winset / winget string handling as pure functions (no socket, no UI).
//
// winset/winget are answered locally and never sent as a Topic; a winset params string splits on ';' AND '&' (and whitespace); a multi-property
//   winget is ONE verbatim query answered `k=v;k=v;...`; every winget gets a reply.
// Stock DreamDaemon: the query string
//   `size;inner-size;statusbar;titlebar;pos` goes on the wire verbatim and is answered
//   `size=900x640;inner-size=900x640;statusbar=false;titlebar=true;pos=16%2c-6` (values percent-encoded).
// Stock DreamSeeker's tokenizer (measured; docs/protocol/GAME-CLIENT-SERVER-516.md, "Winset params"): white space and '=' (and, for a
//   world's winset, '?' ':' '!') separate tokens, ';' and '&' end a pair, and the tokens of a pair alternate key, value.
// Attribution rules: a pair's first token is the key; with no id the target is the text before the LAST '.';
//   keys are trimmed and lower-cased; a double-quoted run is one token and its OUTER quotes come off the value
//   (so `text="Box Station"` survives a whitespace delimiter); nothing url-decodes (except parsePair, which
//   reads `%2c` as the comma it encodes).
#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace fugue::skin {

struct WinsetPair {
    std::string raw, target, key, value;
    bool ok = false;
};

// escapes: a backslash inside quotes escapes the next byte.
std::vector<std::string> splitOutsideQuotes(std::string_view s, bool escapes = false);
// id = the winset's control operand ("" when each key carries its own [window.]elem.key path). urlEncoded: the props
// came from the WORLD (a 0x00a8 winset) and each value is url-decoded after the split: '+' is a space, %XX a byte. A
// "..." value is not url-decoded; its \n is a CRLF, \t a tab, and any other escaped byte stands for itself.
// Stock DreamSeeker 516 decodes them so that `pos=100%2c120` reads back "100,120", `text=a+b` reads "a b", and
// `background-color=%23ff0000` answers "#ff0000" (re-encoded). vg's list2params winsets need it. A world value also
// loses its text-macro markers (net::plainText): stock shows `title="[area.name]"` without them.
std::vector<WinsetPair> splitWinset(std::string_view id, std::string_view props, bool urlEncoded = false);

// `.winset"..."` / `.winset "..."` -> the bare params inside the quotes; false for anything else.
bool winsetBody(std::string_view cmd, std::string& body);
// The on-show fragment filter: winsetBody, tokenise, drop every fragment still holding a bare '?' (a ternary
// clause this client cannot evaluate), re-join with ';'.
bool onShowParams(std::string_view cmd, std::string& params);

// A skin-initiated `.winset` body (a BUTTON's, macro's or menu's command) in DreamSeeker's conditional form,
// `cond ? a b : c d`, parsed into the clauses to evaluate in order. A wire winset() never carries this form; only the
// client command does (DM reference, ".winset (client command)"). A condition may use != for "does not match".
// A `?` starts a new clause even with no ';' before it, so a pair after one reads a new condition and closes any open
// clause. A quoted value ends at its closing quote and resolves backslash escapes. A `:` is the else-switch only inside
// a clause's then-branch; elsewhere it is value text (urls, `byond://`).
struct WinsetClause {
    bool conditional = false;
    WinsetPair cond;              // conditional only: target/key/value to test
    bool negate = false;          // `!=`
    std::vector<WinsetPair> then, otherwise;   // unconditional clause: `then` only
};
std::vector<WinsetClause> parseClientWinset(std::string_view body);
// Does `body` hold a `?` outside a quoted value (i.e. need parseClientWinset rather than splitWinset)?
bool hasConditional(std::string_view body);

// Multi-property winget, in DreamSeeker's shape (the params are parsed as in stock). The names are wingetParamNames(params). One name answers its bare value, "" if `lookup` does
// not hold it. Several answer `k=v;k=v` in query order with every name listed: a name `lookup` does not hold is written
// `k=` (`is-visible=true;nonesuch=`, a missing control `size=;pos=`), and each value is wingetEncode'd.
std::string wingetAnswer(std::string_view params, const std::function<bool(const std::string&, std::string&)>& lookup);

// Stock DreamSeeker 516's reading of a winget params string:
//   * items split on ';' and '&' (`dpi&hwmode` is two), never ',' (`dpi,hwmode` is one unknown name, answered "");
//   * an item's name is its first token: leading whitespace skipped, then up to whitespace or '='. Names are
//     case-insensitive and come back lower-cased. An empty name is dropped;
//   * what is left decides the shape: one name gives the bare value (`dpi;` answers `1`), several give `k=v;k=v`.
std::vector<std::string> wingetParamNames(std::string_view params);
// A value inside a `k=v;k=v` answer, as DreamSeeker encodes it: letters, digits, `.`, `-` and `_` stay; everything
// else is %xx in lower-case hex, and a space is `+`. A lone value is not encoded.
std::string wingetEncode(std::string_view v);

// `[[inner-size]]` -> "W H" (space separated, width first); other parameters are left in place.
std::string expandSkinParams(std::string_view cmd, int w, int h);

// "A,B" / "AxB" (seps is the set of accepted separator characters), reading %2c / %2C as ','.
bool parsePair(std::string_view v, std::string_view seps, long long& a, long long& b);

}  // namespace fugue::skin

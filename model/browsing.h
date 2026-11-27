// browse() plumbing that needs no widget: the params string, the s2c frames that feed a browser page (Browse,
// Output to a browser control, BrowseRsc), the ##action=browse_file offer directive, and the byond://
// URL a page navigates to. Pure C++/std, so the session thread, the GUI and the tests share one implementation.
//
// Parsing rules: ';' and '&' separate params, and a comma also starts a new key=; '-' and '_' fold together; booleans
// are tri-state. The output window is everything before the LAST dot, and a ".browser" suffix with no function is a
// whole-page replace. Output args split on '&' first, then each part is decoded. A page's byond:// query splits on
// ';' and '&', and its values are unescaped tolerantly. Frame layouts are the wire decoders'
// (Browse [html\0][params\0][flag], Output [value\0][control\0], BrowseRsc repeating [ordinal u32][name\0]).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "net/bytes.h"

namespace fugue::model {

struct BrowseParams {
    std::string window, file, raw;
    int w = 0, h = 0, x = 0, y = 0;
    bool posSet = false;
    int canResize = -1, canMinimize = -1, titlebar = -1, autoFormat = -1;  // -1 = not stated
    // display=0: the page is only saved under file= and shown nowhere. Without it, a page with no window= lands in the
    // default browser control (the chat) and replaces it.
    int display = -1;
    std::map<std::string, std::string> extra;
    bool resizable() const { return canResize != 0; }
    bool hasTitlebar() const { return titlebar != 0; }
    bool minimizable() const { return canMinimize != 0; }
};
BrowseParams parseBrowseParams(const std::string& s);

// "##action=browse_file;options=window%3dx" -> opts "window=x". false for any other offer (load_rsc included).
bool browseFileOptions(const std::string& offerParams, std::string& opts);
// output(file, "win.ctl") is a file push too: "##action=output_file;window=win.ctl" -> control "win.ctl". The file is
// the page for that browser control. false for any other offer.
bool outputFileControl(const std::string& offerParams, std::string& control);
// "file=NAME" out of a decoded options string ("" when absent).
std::string browseFileAlias(const std::string& opts);

struct BrowseFrame {
    std::string html, params;
    bool close = false;
};  // html and params without text-macro markers
bool parseBrowseFrame(ByteSpan body, BrowseFrame& out);

struct OutputFrame {
    std::string value, control;
};  // value has ONE trailing '\n' removed, NOT url-decoded
bool parseOutputFrame(ByteSpan body, OutputFrame& out);

struct RscRecord {
    uint32_t ordinal = 0;
    std::string name;
};
// S2C::BrowseRsc's records. A body cut short keeps the records before the cut (proto::decodeBrowseRsc refuses it).
std::vector<RscRecord> parseBrowseRscFrame(ByteSpan body);

// output(value, "window.control:function") -> a call into the page's JS. false when it is not page traffic (plain chat,
// the chat's own ":output", or the function-less ".browser" replace form).
struct PageCall {
    std::string window, fn;
    // The whole addressed control, before the colon ("rpane.hosttracker"). `window` drops the last dotted part, which
    // is right for a browse window's browser ("stat.stat") but wrong for a skin window's control: two pages can share
    // one window ("rpane.hosttracker" and "rpane.hosttracker2").
    std::string target;
    std::vector<std::string> args;  // one per list2params parameter, each decoded individually AFTER the split
};
// Local .output calls may address a function named output directly; server chat
// keeps its historical special routing unless includeOutput is explicitly true.
bool parsePageCall(const std::string& control, const std::string& rawValue, PageCall& out, bool includeOutput = false);

// link(url), S2C::Link [url\0]. "?..." and "byond://?..." are a Topic for this world; other byond:// is another world;
// http, https, ftp and mailto open outside the client; anything else is refused.
struct LinkFrame {
    enum Kind { Topic, World, External, Refused } kind = Refused;
    std::string url;
};
bool parseLinkFrame(ByteSpan body, LinkFrame& out);

// A byond:// (or "?...") URL a page navigated to.
struct ByondUrl {
    std::string raw, path;                                    // path: "" = a Topic, "winset", "winget", ...
    std::vector<std::pair<std::string, std::string>> params;  // decoded, in order
    bool has(const std::string& k) const;
    std::string get(const std::string& k) const;
};
ByondUrl parseByondUrl(const std::string& href);
// '+' -> space and %XX; a '%' not followed by two hex digits is kept (tolerant).
std::string unescapeQuery(const std::string& s);

}  // namespace fugue::model

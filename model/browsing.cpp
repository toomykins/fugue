#include "model/browsing.h"
#include "model/htmltext.h"
#include "net/textmacro.h"
#include "proto/messages.h"

namespace fugue::model {
namespace {
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (a < b && sp(s[a])) a++;
    while (b > a && sp(s[b - 1])) b--;
    return s.substr(a, b - a);
}
bool startsPair(const std::string& s, size_t from) {
    for (size_t i = from; i < s.size(); i++) {
        if (s[i] == '=') return true;
        if (s[i] == ';' || s[i] == '&') return false;
    }
    return false;
}
int boolOf(const std::string& v) {
    std::string l;
    for (char c : v) l += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (l == "1" || l == "true" || l == "yes" || l == "on") return 1;
    if (l == "0" || l == "false" || l == "no" || l == "off") return 0;
    return -1;  // unrecognised = unstated, never "false"
}
bool pair(const std::string& v, char sep, int& a, int& b) {
    size_t p = v.find(sep);
    if (p == std::string::npos) return false;
    try {
        a = std::stoi(trim(v.substr(0, p)));
        b = std::stoi(trim(v.substr(p + 1)));
    } catch (...) { return false; }
    return true;
}
int hexv(char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
}  // namespace

std::string unescapeQuery(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+')
            o += ' ';
        else if (s[i] == '%' && i + 2 < s.size() + 0 && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
            o += char(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
            i += 2;
        } else
            o += s[i];
    }
    return o;
}

BrowseParams parseBrowseParams(const std::string& s) {
    BrowseParams p;
    p.raw = s;
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ';' || c == '&') {
        } else if (c == ',') {
            if (!startsPair(s, i + 1)) continue;
        } else
            continue;
        parts.push_back(s.substr(start, i - start));
        start = i + 1;
    }
    parts.push_back(s.substr(start));
    for (auto& raw : parts) {
        std::string part = trim(raw);
        if (part.empty()) continue;
        size_t eq = part.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(part.substr(0, eq)), v = trim(part.substr(eq + 1));
        for (auto& ch : k) {
            if (ch >= 'A' && ch <= 'Z')
                ch = char(ch - 'A' + 'a');
            else if (ch == '_')
                ch = '-';
        }
        if (k == "window")
            p.window = v;
        else if (k == "file")
            p.file = v;
        else if (k == "size") {
            int a, b;
            if (pair(v, 'x', a, b)) {
                p.w = a;
                p.h = b;
            }
        } else if (k == "pos") {
            int a = 0, b = 0;
            if (pair(v, ',', a, b)) {
                p.x = a;
                p.y = b;
            }
            p.posSet = true;
        } else if (k == "can-resize")
            p.canResize = boolOf(v);
        else if (k == "can-minimize")
            p.canMinimize = boolOf(v);
        else if (k == "titlebar")
            p.titlebar = boolOf(v);
        else if (k == "auto-format")
            p.autoFormat = boolOf(v);
        else if (k == "display")
            p.display = boolOf(v);
        else
            p.extra[k] = v;
    }
    return p;
}

bool browseFileOptions(const std::string& offerParams, std::string& opts) {
    if (offerParams.rfind("##", 0) != 0) return false;
    bool isBF = false;
    std::string raw;
    std::string rest = offerParams.substr(2);
    size_t st = 0;
    while (st <= rest.size()) {
        size_t e = rest.find(';', st);
        if (e == std::string::npos) e = rest.size();
        std::string kv = rest.substr(st, e - st);
        size_t eq = kv.find('=');
        std::string k = trim(eq == std::string::npos ? kv : kv.substr(0, eq)),
                    v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        if (k == "action")
            isBF = v == "browse_file";
        else if (k == "options")
            raw = v;
        st = e + 1;
    }
    if (!isBF) return false;
    // Url-encoded only sometimes (window%3dbrowseroutput, or window=lobbybrowser). Tolerant: a '+' also decodes to
    // a space, and text that does not decode is kept verbatim.
    opts = decodeChatHTML(raw);
    return true;
}

bool outputFileControl(const std::string& offerParams, std::string& control) {
    if (offerParams.rfind("##", 0) != 0) return false;
    bool isOF = false;
    std::string raw;
    std::string rest = offerParams.substr(2);
    size_t st = 0;
    while (st <= rest.size()) {
        size_t e = rest.find(';', st);
        if (e == std::string::npos) e = rest.size();
        std::string kv = rest.substr(st, e - st);
        size_t eq = kv.find('=');
        std::string k = trim(eq == std::string::npos ? kv : kv.substr(0, eq)),
                    v = eq == std::string::npos ? "" : kv.substr(eq + 1);
        if (k == "action")
            isOF = v == "output_file";
        else if (k == "window")
            raw = v;
        st = e + 1;
    }
    control = decodeChatHTML(raw);
    return isOF && !control.empty();
}

std::string browseFileAlias(const std::string& opts) {
    size_t st = 0;
    while (st <= opts.size()) {
        size_t e = opts.find(';', st);
        if (e == std::string::npos) e = opts.size();
        std::string kv = opts.substr(st, e - st);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && trim(kv.substr(0, eq)) == "file") return kv.substr(eq + 1);
        st = e + 1;
    }
    return "";
}

bool parseBrowseFrame(ByteSpan body, BrowseFrame& out) {
    proto::Browse m;
    try {
        m = proto::decodeBrowse(body);
    } catch (const std::exception&) { return false; }
    out.close = m.close;
    // Stock drops every text-macro marker from both strings before the page is built (an output() to a browser
    // control keeps them).
    out.html = net::plainText(m.html);
    out.params = net::plainText(m.params);
    return true;
}

bool parseOutputFrame(ByteSpan body, OutputFrame& out) {
    proto::Output m;
    try {
        m = proto::decodeOutput(body);
    } catch (const std::exception&) { return false; }
    out.value = std::move(m.value);
    out.control = std::move(m.control);
    return true;
}

bool parseLinkFrame(ByteSpan body, LinkFrame& out) {
    try {
        out.url = proto::decodeLink(body).url;
    } catch (const std::exception&) { return false; }
    std::string scheme;
    size_t colon = out.url.find(':');
    if (colon != std::string::npos && out.url.find_first_of("/?#") > colon)
        for (char c : out.url.substr(0, colon)) scheme += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (out.url.rfind("?", 0) == 0 || (scheme == "byond" && out.url.compare(colon, 4, "://?") == 0))
        out.kind = LinkFrame::Topic;
    else if (scheme == "byond")
        out.kind = LinkFrame::World;
    else if (scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "mailto")
        out.kind = LinkFrame::External;
    else
        out.kind = LinkFrame::Refused;
    return true;
}

std::vector<RscRecord> parseBrowseRscFrame(ByteSpan body) {
    std::vector<RscRecord> v;
    proto::Reader r(body);
    try {
        while (r.remaining() >= 4) {
            RscRecord rec;
            rec.ordinal = r.u32();
            rec.name = r.cstr();
            v.push_back(std::move(rec));
        }
    } catch (const proto::DecodeError&) {}
    return v;
}

bool parsePageCall(const std::string& control, const std::string& rawValue, PageCall& out, bool includeOutput) {
    // Any "control:function" calls into the page, whatever the function; stock DreamSeeker never shows it as text.
    // The one exception is chat: vg's "browseroutput:output" is the chat page's own output() and stays chat.
    size_t colon = control.find(':');
    if (colon == std::string::npos) return false;
    std::string ctrl = control.substr(0, colon), fn = control.substr(colon + 1);
    if (fn.empty() || (!includeOutput && fn == "output")) return false;
    size_t dot = ctrl.rfind('.');
    std::string win = dot == std::string::npos ? ctrl : ctrl.substr(0, dot);
    if (win.empty()) return false;
    out.window = win;
    out.target = ctrl;
    out.fn = fn;
    out.args.clear();
    size_t st = 0;
    while (st <= rawValue.size()) {
        size_t e = rawValue.find('&', st);
        if (e == std::string::npos) e = rawValue.size();
        out.args.push_back(decodeChatHTML(rawValue.substr(st, e - st)));
        st = e + 1;
    }
    return true;
}

bool ByondUrl::has(const std::string& k) const {
    for (auto& kv : params)
        if (kv.first == k) return true;
    return false;
}
std::string ByondUrl::get(const std::string& k) const {
    for (auto& kv : params)
        if (kv.first == k) return kv.second;
    return "";
}

ByondUrl parseByondUrl(const std::string& href) {
    ByondUrl u;
    std::string s = href;
    if (s.rfind("byond://", 0) == 0)
        s = s.substr(8);
    else if (s.rfind("byond:", 0) == 0)
        s = s.substr(6);
    u.raw = "byond://" + s;
    size_t q = s.find('?');
    std::string path = q == std::string::npos ? s : s.substr(0, q);
    std::string query = q == std::string::npos ? "" : s.substr(q + 1);
    size_t a = 0, b = path.size();
    while (a < b && path[a] == '/') a++;
    while (b > a && path[b - 1] == '/') b--;
    u.path = path.substr(a, b - a);
    size_t st = 0;
    while (st <= query.size()) {
        size_t e = query.find_first_of(";&", st);
        if (e == std::string::npos) e = query.size();
        std::string kv = query.substr(st, e - st);
        if (!kv.empty()) {
            size_t eq = kv.find('=');
            std::string k = unescapeQuery(eq == std::string::npos ? kv : kv.substr(0, eq));
            std::string v = eq == std::string::npos ? "" : unescapeQuery(kv.substr(eq + 1));
            u.params.emplace_back(std::move(k), std::move(v));
        }
        st = e + 1;
    }
    return u;
}

}  // namespace fugue::model

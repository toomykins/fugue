#include "shell/inputrouter.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "skin/winset.h"

namespace fugue::shell {
namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
std::string dmEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') {
            out += "\\n";
            continue;
        }
        if (c == '\\' || c == '"' || c == '[' || c == ']') out += '\\';
        out += c;
    }
    return out;
}
std::string jsonString(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n')
            out += "\\n";
        else if (c < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", c);
            out += b;
        } else
            out += char(c);
    }
    return out + '"';
}
bool isNumber(const std::string& s) {
    if (s.empty() || std::isspace((unsigned char)s[0])) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end == s.c_str() + s.size();
}
bool isInteger(const std::string& s) {
    size_t i = !s.empty() && (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); i++)
        if (!std::isdigit((unsigned char)s[i])) return false;
    return true;
}
// "WxH" / "X,Y": the two numbers of a size or position.
bool splitPair(const std::string& v, std::string& a, std::string& b) {
    size_t i = v.find_first_of("x,");
    if (i == std::string::npos) return false;
    a = trim(v.substr(0, i));
    b = trim(v.substr(i + 1));
    return isInteger(a) && isInteger(b);
}
// Parameters whose value is text even where it reads like a number or a pair.
bool textParam(const std::string& p) {
    static const char* const names[] = {"text",  "title", "command",     "id",          "parent",       "type", "name",
                                        "image", "style", "font-family", "current-tab", "saved-params", "tabs"};
    for (const char* n : names)
        if (p == n) return true;
    return p.rfind("on-", 0) == 0;
}

// One embedded value written as `fmt`.
std::string formatEmbedded(const std::string& v, const std::string& param, const std::string& fmt) {
    std::string a, b;
    const bool text = textParam(param);
    const bool pair = !text && splitPair(v, a, b);
    const bool boolean = !text && (v == "true" || v == "false");
    const bool number = !text && isNumber(v);
    if (fmt == "raw") return v;
    if (fmt == "escaped") return dmEscape(pair ? a + "," + b : v);
    if (fmt == "string") return '"' + dmEscape(v) + '"';
    if (fmt == "params") return skin::wingetEncode(pair ? a + "," + b : v);
    if (fmt == "json" || fmt == "json-dm") {
        std::string j = pair ? "{\"x\":" + a + ",\"y\":" + b + "}" : (boolean || number) ? v : jsonString(v);
        return fmt == "json" ? j : dmEscape(j);
    }
    if (pair) return a + " " + b;
    if (boolean) return v == "true" ? "1" : "0";
    if (number) return v;
    return '"' + dmEscape(v) + '"';
}
}  // namespace

std::string expandEmbedded(const std::string& command, const std::string* star, const SkinLookup& lookup) {
    std::string out;
    size_t i = 0;
    for (;;) {
        size_t p = command.find("[[", i);
        size_t e = p == std::string::npos ? p : command.find("]]", p + 2);
        if (e == std::string::npos) return out + command.substr(i);
        out += command.substr(i, p - i);
        i = e + 2;
        std::string expr = trim(command.substr(p + 2, e - p - 2)), fmt;
        const bool quoted = p > 0 && command[p - 1] == '"' && i < command.size() && command[i] == '"';
        size_t as = lower(expr).rfind(" as ");
        if (as != std::string::npos) {
            fmt = lower(trim(expr.substr(as + 4)));
            expr = trim(expr.substr(0, as));
        }
        if (fmt.empty()) fmt = quoted ? "escaped" : "arg";
        if (expr == "*") {
            if (star) out += formatEmbedded(*star, "text", fmt);
            continue;
        }
        if (!lookup) {
            out += command.substr(p, i - p);
            continue;
        }
        // [[id.param]], [[param]] (the initiating control), and a trailing .x / .y for one number of a pair.
        std::vector<std::string> parts;
        for (size_t a = 0;;) {
            size_t d = expr.find('.', a);
            parts.push_back(expr.substr(a, d == std::string::npos ? d : d - a));
            if (d == std::string::npos) break;
            a = d + 1;
        }
        int comp = -1;
        if (parts.size() > 1 && (lower(parts.back()) == "x" || lower(parts.back()) == "y")) {
            comp = lower(parts.back()) == "x" ? 0 : 1;
            parts.pop_back();
        }
        std::string param = lower(parts.back()), control;
        parts.pop_back();
        for (const std::string& s : parts) control += (control.empty() ? "" : ".") + s;
        std::string v, a, b;
        if (!lookup(control, param, v)) v.clear();
        if (comp >= 0 && splitPair(v, a, b)) v = comp == 0 ? a : b;
        out += formatEmbedded(v, param, fmt);
    }
}

Route routeCommand(const std::string& command, const std::string& keyName, bool up, const SkinLookup& lookup) {
    std::string cmd = expandEmbedded(command, keyName.empty() ? nullptr : &keyName, lookup);
    std::string line = cmd.substr(std::min(cmd.size(), cmd.find_first_not_of(" \t\r\n")));
    cmd = trim(cmd);
    Route r;
    if (cmd.empty()) return r;
    if (cmd[0] != '.') {
        r.kind = Route::Command;
        r.text = line;
        return r;
    }  // trailing spaces belong to a command_text argument
    size_t sp = cmd.find_first_of(" \t");
    std::string verb = lower(cmd.substr(0, sp));
    static const struct {
        const char* name;
        int dir;
    } dirs[] = {{".north", 1},     {".south", 2},     {".east", 4},       {".west", 8},  {".northeast", 5},
                {".northwest", 9}, {".southeast", 6}, {".southwest", 10}, {".center", 0}};
    // A `.northwest` macro is the stock move command, not a walk. The server answers it by calling client.Northwest(),
    // which SS13 forks override: vg's Northwest() treats the hotkey as a drop. Stock DreamSeeker never routes it to
    // the held-key MoveKey verb, so it is sent as the sub-0 move.
    // With anything after it the command does nothing (stock 516.1689: `.north 3` calls no client proc).
    for (auto& d : dirs)
        if (verb == d.name && sp == std::string::npos) {
            r.kind = Route::Move;
            r.dir = d.dir;
            r.down = !up;
            return r;
        }
    if (verb == ".screenshot") {
        r.kind = Route::Screenshot;
        r.text = sp == std::string::npos ? "" : lower(trim(cmd.substr(sp)));
        return r;
    }
    if (verb == ".quit") {
        r.kind = Route::Quit;
        return r;
    }
    if (verb == ".reconnect") {
        r.kind = Route::Reconnect;
        return r;
    }
    // Client-side dialogs (skin/usermacros.h).
    if (verb == ".macros") {
        r.kind = Route::Macros;
        return r;
    }
    if (verb == ".options") {
        r.kind = Route::Options;
        return r;
    }
    // `.output control text`, as DM's output(text, control): a "control:fn" control calls into that browser's page.
    // Either operand may be quoted.
    if (verb == ".output" && sp != std::string::npos) {
        std::string rest = trim(cmd.substr(sp));
        auto token = [](std::string& s) {
            std::string out;
            if (!s.empty() && s[0] == '"') {
                size_t i = 1;
                for (; i < s.size() && s[i] != '"'; i++) {
                    if (s[i] == '\\' && i + 1 < s.size()) i++;
                    out.push_back(s[i]);
                }
                s = s.substr(std::min(s.size(), i + 1));
            } else {
                size_t e = s.find_first_of(" \t");
                out = s.substr(0, e);
                s = e == std::string::npos ? std::string() : s.substr(e);
            }
            size_t b = s.find_first_not_of(" \t");
            s = b == std::string::npos ? std::string() : s.substr(b);
            return out;
        };
        r.text = token(rest);
        r.arg = !rest.empty() && rest[0] == '"' ? token(rest) : rest;
        if (!r.text.empty()) {
            r.kind = Route::Output;
            return r;
        }
    }
    if (verb == ".winset") {
        // The macro swap is detected for the decision, but the whole string is passed on (winsetBody), so every other
        // key in it (focus, is-checked, left and right) still applies.
        std::string body;
        if (!skin::winsetBody(cmd, body)) body = trim(cmd.substr(sp == std::string::npos ? cmd.size() : sp));
        r.winsetBody = body;
        std::string l = lower(cmd);
        size_t i = l.find(".macro=");
        if (i != std::string::npos) {
            std::string rest = cmd.substr(i + 7);
            size_t j = rest.find_first_of(" ;\"'");
            if (j != std::string::npos) rest = rest.substr(0, j);
            rest = trim(rest);
            if (!rest.empty()) {
                r.kind = Route::SetMacroSet;
                r.text = rest;
                return r;
            }
        }
        if (!body.empty()) {
            r.kind = Route::Winset;
            r.text = body;
            return r;
        }
    }
    r.kind = Route::Unsupported;
    r.text = cmd;
    return r;
}

std::vector<std::string> macroCommandLines(const std::string& command) {
    std::vector<std::string> out;
    for (size_t a = 0; a <= command.size();) {
        size_t b = command.find('\n', a);
        if (b == std::string::npos) b = command.size();
        std::string l = command.substr(a, b - a);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && l[0] != ' ' && l[0] != '\t') out.push_back(l);
        a = b + 1;
    }
    return out;
}

Route routeWorldCommand(const std::string& command, const SkinLookup& lookup) {
    Route r = routeCommand(command, "", false, lookup);
    if (r.kind != Route::Unsupported) return r;
    // A malformed `.output` is still the client's own command, never a verb.
    const std::string line = trim(command);
    if (lower(line.substr(0, line.find_first_of(" 	"))) != ".output") r.kind = Route::Command;
    return r;
}

}  // namespace fugue::shell

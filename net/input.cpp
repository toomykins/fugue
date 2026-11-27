#include "net/input.h"
#include "net/textmacro.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <set>

namespace fugue::net {
using proto::enc::VerbArg;

namespace {
constexpr uint32_t kArgNum = 8, kArgText = 0x04, kArgNull = 0x80, kArgMessage = 0x800, kArgAnything = 0x1000;
constexpr uint32_t kArgAtoms = 0x01 | 0x02 | 0x20;  // mob | obj | turf: a typed name, never a string
constexpr uint32_t kArgCommandText = 0x10000;
constexpr uint8_t kFilterContents = 0x08, kFilterWorld = 0x10;  // `in usr`, `in world` (FilterKind on the Verb)
constexpr uint8_t kFilterList = 0x40;                           // VerbArgSpec.filterKind for an `in <list>` clause
// Verb flag 0x02 is `set src = ...` (not `in`); a turf / area src kind.
constexpr uint8_t kVerbFlagSrcEquals = 0x02, kVerbSrcTurf = 0x03, kVerbSrcArea = 0x04;

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
bool ieq(const std::string& a, const std::string& b) { return a.size() == b.size() && lower(a) == lower(b); }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string ltrim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a);
}

// Typed-line quoting, as stock DreamSeeker and DreamDaemon parse it:
//   * A typed argument's backslashes go out raw, and DreamDaemon then parses DM escapes in it.
//   * Only `"` quotes, and a backslash escapes the byte after it while scanning.
//   * A `"` token closes at its first unescaped `"` only when whitespace or the end of the line follows it. Otherwise
//     the token is the rest of the line minus its opening quote.
//   * A closed `"` token is one token even as the last argument.
//   * A `'` is not a quote, but a `'` token spans to its closing `'` the same way. Only the last text argument may be
//     one, and it takes the rest of the line verbatim.
//   * Any other token runs to whitespace, with a backslash escaping the byte after it (`Pick Big\ Box` is one token).
size_t unescapedQuote(const std::string& s, size_t from, char q = '"') {  // first unescaped q at or after `from`
    for (size_t i = from; i < s.size(); i++) {
        if (s[i] == '\\') {
            i++;
            continue;
        }
        if (s[i] == q) return i;
    }
    return std::string::npos;
}
bool isSpace(char c) { return c == ' ' || c == '\t'; }
struct Token {
    enum Kind { Bare, Quoted, QuotedRest, Single } kind = Bare;
    std::string raw;    // as typed: what a refusal prints, and what a list argument's ListResolve carries
    std::string value;  // Quoted / QuotedRest: the text between the quotes (backslashes still raw)
};
// One argument token off the front of `s` (no leading whitespace); `rest` gets what follows it, left-trimmed.
Token nextToken(const std::string& s, std::string& rest) {
    Token t;
    rest.clear();
    if (s.empty()) return t;
    if (s[0] == '"' || s[0] == '\'') {
        size_t q = unescapedQuote(s, 1, s[0]);
        bool closed = q != std::string::npos && (q + 1 == s.size() || isSpace(s[q + 1]));
        if (s[0] == '"') {
            if (closed) {
                t.kind = Token::Quoted;
                t.raw = s.substr(0, q + 1);
                t.value = s.substr(1, q - 1);
                rest = ltrim(s.substr(q + 1));
            } else {
                t.kind = Token::QuotedRest;
                t.raw = s;
                t.value = s.substr(1);
            }
            return t;
        }
        t.kind = Token::Single;
        if (closed) {
            t.raw = s.substr(0, q + 1);
            rest = ltrim(s.substr(q + 1));
        } else
            t.raw = s;  // unclosed or run-on: the rest of the line, as with "..."
        return t;
    }
    size_t i = 0;
    for (; i < s.size() && !isSpace(s[i]); i++)
        if (s[i] == '\\') i++;
    if (i > s.size()) i = s.size();
    t.raw = s.substr(0, i);
    rest = ltrim(s.substr(i));
    return t;
}
bool parseRefArg(const std::string& s, uint32_t& out) {
    std::string raw;
    if (s.rfind("ref:", 0) == 0)
        raw = s.substr(4);
    else if (!s.empty() && s[0] == '#')
        raw = s.substr(1);
    else
        return false;
    if (raw.empty()) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(raw.c_str(), &end, 0);
    if (*end || v > 0xffffffffull) return false;
    out = uint32_t(v);
    return true;
}
bool parseNumArg(const std::string& s, float& out) {
    if (s.rfind("num:", 0) != 0) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str() + 4, &end);
    return s.size() > 4 && !*end;
}
std::string argTypeName(uint32_t m) {
    if (m == 0) return "arg";
    if ((m & kArgNum) && !(m & kArgText)) return "num";
    if (m & kArgText) return "\"text\"";
    if (m & 0x01) return "mob";
    if (m & 0x02) return "obj";
    if (m & 0x20) return "turf";
    if (m & 0x100) return "area";
    return "arg";
}
}  // namespace

int parseDir(const std::string& s) {
    std::string l = lower(s);
    if (l == "north" || l == "up" || l == "n") return 1;
    if (l == "south" || l == "down" || l == "s") return 2;
    if (l == "east" || l == "right" || l == "e") return 4;
    if (l == "west" || l == "left" || l == "w") return 8;
    if (l == "northeast" || l == "ne") return 5;
    if (l == "southeast" || l == "se") return 6;
    if (l == "northwest" || l == "nw") return 9;
    if (l == "southwest" || l == "sw") return 10;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (!s.empty() && !*end && (v == 1 || v == 2 || v == 4 || v == 8 || v == 5 || v == 6 || v == 9 || v == 10))
        return int(v);
    return 0;
}

void InputModel::onFrame(uint16_t type, ByteSpan b) {
    if (type == S2C::ResolveReply) {
        uint8_t k;
        uint32_t v;
        if (parseListResolveReply(b, k, v)) listReplies_.push_back({k, v});
        return;
    }
    if (type == S2C::MouseSubs) {
        if (b.size() < 16) return;
        flags_ = le32(&b[12]);
        extra_ = 0;
        if (flags_ & kMouseFlagExtra) {
            if (b.size() < 20) return;
            extra_ = le32(&b[16]);
        }
        haveSubs_ = true;
        return;
    }
    if (type != S2C::Verb) return;
    // [id u16/u32, negotiated by handshake][name\0][desc\0][cat\0][head u8][src u8][flags u8]([flagsExt u32][extByte u8] iff
    // flags&0x80)
    // [argc u8] argc x [typemask u32][filter u8][filterKind u8]
    size_t p = 0;
    auto cstr = [&](std::string& o) {
        size_t q = p;
        while (q < b.size() && b[q]) q++;
        if (q >= b.size()) return false;
        o.assign((const char*)&b[p], q - p);
        p = q + 1;
        return true;
    };
    const size_t idBytes = wideVerbIds_ ? 4 : 2;
    if (b.size() < idBytes) return;
    VerbEntry e;
    e.id = wideVerbIds_ ? le32(&b[0]) : le16(&b[0]);
    p = idBytes;
    e.wideId = wideVerbIds_;
    if (!cstr(e.name) || !cstr(e.desc) || !cstr(e.category)) return;
    // Without text-macro markers, as stock shows and matches a typed name; the verb itself is sent by id.
    e.name = plainText(e.name);
    e.desc = plainText(e.desc);
    e.category = plainText(e.category);
    if (p + 3 > b.size()) return;
    e.head = b[p++];
    e.src = b[p++];
    e.flags = b[p++];
    if (e.flags & 0x80) {
        if (p + 5 > b.size()) return;
        p += 5;
    }
    if (p >= b.size()) return;
    unsigned argc = b[p++];
    for (unsigned i = 0; i < argc; i++) {
        if (p + 6 > b.size()) return;
        VerbArgSpec a;
        a.typeMask = le32(&b[p]);
        a.filter = b[p + 4];
        a.filterKind = b[p + 5];
        p += 6;
        e.args.push_back(a);
    }
    if (p != b.size()) return;  // malformed/wrong-width advertisements must not install partial names
    verbs_[e.id] = std::move(e);
}

proto::enc::MouseHooks InputModel::hooks() const {
    proto::enc::MouseHooks h;
    h.click = flags_ & kMouseFlagClick;
    h.dblClick = flags_ & kMouseFlagDblClick;
    h.up = flags_ & kMouseFlagUp;
    h.down = flags_ & kMouseFlagDown;
    h.drop = flags_ & kMouseFlagDrop;
    h.drag = flags_ & kMouseFlagDrag;
    return h;
}

const VerbEntry* InputModel::verbByName(const std::string& name) const {
    for (auto& [id, e] : verbs_)
        if (ieq(e.name, name)) return &e;
    return nullptr;
}

std::string verbCommandName(const std::string& name) {
    std::string o = name;
    std::replace(o.begin(), o.end(), ' ', '-');
    return o;
}

// Stock DreamSeeker's verb lookup: the first word names the verb when it is the name with each space written `-`
// (case-insensitive, no abbreviations). Failing that, a name typed with spaces still names the verb, but its arguments
// begin after the first word (`Set Default Language` is that verb with the argument `Default`).
static bool nameTyped(const std::string& name, const std::string& line) {  // `name` at the start of `line`
    if (name.empty() || line.size() < name.size()) return false;
    for (size_t i = 0; i < name.size(); i++) {
        char n = name[i], c = line[i];
        if (n == ' ' ? (c != ' ' && c != '-') : std::tolower((unsigned char)n) != std::tolower((unsigned char)c))
            return false;
    }
    return line.size() == name.size() || line[name.size()] == ' ' || line[name.size()] == '\t';
}

const VerbEntry* InputModel::matchVerb(const std::string& line0, std::string& rest, bool* firstWord) const {
    std::string line = trim(line0);
    size_t w = line.find_first_of(" \t");
    std::string word = line.substr(0, w);
    const VerbEntry* best = nullptr;
    bool first = false;
    for (auto& [id, e] : verbs_) {
        bool f = e.name.size() == word.size() && nameTyped(e.name, word);
        if (!f && !nameTyped(e.name, line)) continue;
        if (!best || (f && !first) || (f == first && !f && e.name.size() > best->name.size())) {
            best = &e;
            first = f;
        }
    }
    if (best) rest = w == std::string::npos ? "" : trim(line.substr(w));
    if (firstWord) *firstWord = first;
    return best;
}

// Space completion, as in the stock command bar: a space typed at the end of the bar becomes ` "` when the verb's next
// unfilled argument is text or message (`say` -> `say "`, `numtext 5` -> `numtext 5 "`), and a plain space for num,
// anything, or no arguments. A unique prefix completes to the verb's command name (`Set` -> `Set-Default-Language `).
// Inside an open quote, after whitespace, or for an ambiguous prefix, a plain space is typed. An escaped `\"` neither
// opens nor closes a quote.
bool InputModel::spaceCompletion(const std::string& line, std::string& out) const {
    size_t quotes = 0;
    for (size_t q = unescapedQuote(line, 0); q != std::string::npos; q = unescapedQuote(line, q + 1)) quotes++;
    if (quotes % 2 == 1 || trim(line) != line) return false;
    std::string rest;
    bool firstWord = false;
    const VerbEntry* e = matchVerb(line, rest, &firstWord);
    std::string head;
    if (e) {
        if (!firstWord) return false;
        head = verbCommandName(e->name) + line.substr(e->name.size());
    } else {
        if (line.find_first_of(" \t") != std::string::npos) return false;
        std::set<std::string> names;  // an override shares its verb's name: one completion, not an ambiguous prefix
        for (auto& [id, v] : verbs_) {
            if (v.name.empty() || v.name.size() <= line.size()) continue;
            if (ieq(verbCommandName(v.name).substr(0, line.size()), line) && names.insert(v.name).second) e = &v;
        }
        if (names.size() != 1) return false;
        head = verbCommandName(e->name);
    }
    size_t filled = 0;
    for (std::string r = rest, nr; !r.empty(); filled++) {
        nextToken(r, nr);
        r = nr;
    }
    // Stock ORs the next argument's mask over the verbs of the name (a `set src in ...` one's next word is its src, so it
    // adds nothing) and types ` "` only for exactly text, message or another text kind: not for null|text or text|num.
    uint32_t mask = 0;
    for (auto& [id, v] : verbs_) {
        if (v.name != e->name || (v.src != 0 && !(v.flags & 0x80) && !(v.flags & kVerbFlagSrcEquals))) continue;
        if (filled < v.args.size()) mask |= v.args[filled].typeMask;
    }
    bool quote = mask == kArgText || mask == kArgMessage || mask == 0x8000 || mask == 0x20000;
    out = head + (quote ? " \"" : " ");
    return true;
}

// An `in <list>` argument whose value the server resolves (ListResolve, input.h).
static bool resolvesThroughServer(const VerbArgSpec& a) {
    return a.filterKind == kFilterList && !(a.typeMask & (kArgText | kArgMessage | kArgNum));
}
// A nullable argument the line ran out before goes out as null (tag 0, kind 0, ref 0xffff), unless the verb was started
// from the stat panel (promptNull), which opens a dialog with a None button. A required argument after it still prompts.
static VerbArg nullArg() {
    VerbArg a;
    a.tag = 0;
    a.kind = 0;
    a.ref = 0xffff;
    return a;
}

static VerbArg atomArg(const SrcCandidate& c) {
    VerbArg a;
    a.tag = 0;
    a.kind = c.src.kind;
    a.ref = c.src.ref;
    return a;
}

// Atom-argument matching, by stock DreamSeeker's own rules. A token names a candidate as follows (case-insensitive
// unless backslashed):
//   * the WHOLE name, or else the LAST WORDS of a longer name (a word that starts after a space). No prefixes or substrings.
//   * a typed `-` matches a `-` or a space in the name.
//   * a backslashed character matches only itself, case-sensitively: `\ ` matches a space, `\b` a lowercase b only.
//     `\n`, `\t` and `\r` are newline, tab and CR. A lone trailing `\` is ignored.
//   * if nothing matches, the token is cut at its last backslash and tried again.
// A whole-name match beats a last-words match, then the nearer (SrcCandidate::dist), and ties go to the first candidate
// (verbArgCandidates' order, lowest ref). The candidates are model::verbArgCandidates, never a guess at what is carried.
// A refusal names the token (refusedAtom).
namespace {
bool ownMob(const SrcCandidate& c) { return c.src.kind == proto::enc::kVerbSrcMob && !c.named; }
bool isClient(const SrcCandidate& c) { return c.src.kind == proto::enc::kVerbSrcClient; }
int distOf(const SrcCandidate& c) { return isClient(c) || ownMob(c) ? -1 : c.dist; }
struct NameChar {
    char c;
    bool exact;
};
std::vector<NameChar> namePattern(const std::string& raw) {
    std::vector<NameChar> p;
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] != '\\') {
            p.push_back({raw[i], false});
            continue;
        }
        if (i + 1 == raw.size()) break;  // a lone trailing `\`
        char c = raw[++i];
        p.push_back({c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c, true});
    }
    return p;
}
size_t lastEscape(const std::string& raw) {  // where the last `\c` (or a lone trailing `\`) starts
    size_t at = std::string::npos;
    for (size_t i = 0; i < raw.size(); i++)
        if (raw[i] == '\\') at = i++;
    return at;
}
char lowerc(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
bool patternIs(const std::vector<NameChar>& p, const std::string& name, size_t at) {
    if (name.size() - at != p.size()) return false;
    for (size_t i = 0; i < p.size(); i++) {
        char n = name[at + i];
        bool ok = p[i].exact ? n == p[i].c : lowerc(n) == lowerc(p[i].c) || (p[i].c == '-' && n == ' ');
        if (!ok) return false;
    }
    return true;
}
const SrcCandidate* atomForPattern(const std::vector<SrcCandidate>& cands, const std::vector<NameChar>& p) {
    if (p.empty()) return nullptr;
    const SrcCandidate* best = nullptr;
    auto take = [&](const SrcCandidate& c) {
        if (!best || distOf(c) < distOf(*best)) best = &c;
    };
    for (const SrcCandidate& c : cands)
        if (patternIs(p, c.name, 0)) take(c);
    if (best) return best;
    for (const SrcCandidate& c : cands)
        for (size_t k = 1; k < c.name.size(); k++)
            if (c.name[k - 1] == ' ' && patternIs(p, c.name, k)) {
                take(c);
                break;
            }
    return best;
}
}  // namespace

const SrcCandidate* atomTyped(const std::vector<SrcCandidate>& cands, const std::string& raw) {
    for (std::string r = raw;;) {
        if (const SrcCandidate* c = atomForPattern(cands, namePattern(r))) return c;
        size_t cut = lastEscape(r);
        if (cut == std::string::npos) return nullptr;
        r.resize(cut);
    }
}

// A dialog value naming one of an atom argument's candidates: the name as listed, else case-insensitively or with its
// spaces written `-`.
static const SrcCandidate* atomNamed(const std::vector<SrcCandidate>& cands, const std::string& tok) {
    for (const SrcCandidate& c : cands)
        if (c.name == tok) return &c;
    for (const SrcCandidate& c : cands) {
        std::string dashed = c.name;
        std::replace(dashed.begin(), dashed.end(), ' ', '-');
        if (ieq(dashed, tok) || ieq(c.name, tok)) return &c;
    }
    return nullptr;
}

// DreamSeeker's usage line: the type bits in ascending order joined by `|`, square brackets for null, and `in inventory`
// or `in world` for the filter.
static std::string usageToken(const VerbArgSpec& a) {
    uint32_t m = a.typeMask & ~kArgNull;
    if (a.filterKind == kFilterList) m &= ~kArgAnything;
    if ((m & kArgMessage) && (m & kArgText)) m &= ~kArgMessage;
    static const std::pair<uint32_t, const char*> names[] = {{0x01, "mob"},
                                                             {0x02, "obj"},
                                                             {kArgText, "\"text\""},
                                                             {kArgNum, "number"},
                                                             {0x20, "turf"},
                                                             {0x100, "area"},
                                                             {kArgMessage, "\"text\""},
                                                             {kArgAnything, "anything"},
                                                             {kArgCommandText, "command"}};
    std::string t;
    for (const auto& [bit, name] : names)
        if (m & bit) t += (t.empty() ? "" : "|") + std::string(name);
    if (t.empty()) t = "parameter";
    if (a.typeMask & kArgNull) t = "[" + t + "]";
    if (a.filterKind == kFilterContents)
        t += " in inventory";
    else if (a.filterKind == kFilterWorld)
        t += " in world";
    return t;
}
static std::string usageLine(const VerbEntry& e) {
    std::string u = "usage: " + verbCommandName(e.name);
    for (const VerbArgSpec& a : e.args) u += " " + usageToken(a);
    return u;
}
// DreamSeeker prints two lines and sends nothing for a refusal: `Sorry, the following is not valid: X`, then `usage: ...`.
// They are joined by '\n' here; the caller prints each.
static std::string notValid(const VerbEntry& e, const std::string& what) {
    return "Sorry, the following is not valid: " + what + "\n" + usageLine(e);
}
// The token an atom refusal names: the token itself, unless it and the tokens after it, joined by spaces, would have
// named a candidate. Then it is the second token: `Pick Big Box` names Box, `Pick red box zzz` names box. The join's
// space is a real space.
static std::string refusedAtom(const std::vector<SrcCandidate>& cands, const Token& tok, const std::string& after) {
    std::string joined = tok.raw, second;
    for (std::string r = after; !r.empty();) {
        std::string nr;
        Token t = nextToken(r, nr);
        if (t.kind != Token::Bare) break;
        if (second.empty()) second = t.raw;
        joined += "\\ " + t.raw;
        if (atomTyped(cands, joined)) return second;
        r = nr;
    }
    return tok.raw;
}

struct ArgParse {
    std::vector<VerbArg> args;
    std::string line;           // the display line (MissingArg::line), with the atoms DreamSeeker fills in itself
    bool promptNull = false;    // MissingArg::promptNull
    size_t missing = SIZE_MAX;  // the first argument the line ran out before that needs a dialog
    std::optional<std::vector<SrcCandidate>> listed;  // its listbox, when DreamSeeker lists the atoms itself
    size_t listAt = SIZE_MAX;  // the first argument that needs a server resolve, its token and the rest of the line
    std::string listText, listRest;
    bool quiet = false;  // the refusal prints nothing
};

// An atom argument's listbox when DreamSeeker lists the atoms itself (mob / obj / turf / `anything`, no text or num bit):
// one candidate per name, the first kept.
static std::optional<std::vector<SrcCandidate>> listedAtoms(const VerbArgSpec& spec,
                                                            const InputModel::ArgResolver& atoms) {
    if (!atoms || (spec.typeMask & (kArgText | kArgMessage | kArgNum))) return std::nullopt;
    std::optional<std::vector<SrcCandidate>> cands = atoms(spec);
    if (!cands) return cands;
    std::vector<SrcCandidate> out;
    for (const SrcCandidate& c : *cands)
        if (std::none_of(out.begin(), out.end(), [&](const SrcCandidate& o) { return o.name == c.name; }))
            out.push_back(c);
    return out;
}

// Parses arguments [start..] of e from `rest` onto p.args. Returns the local error ("" = complete). Parsing stops at
// p.missing or p.listAt.
static std::string verbArgsFor(const VerbEntry& e, size_t start, std::string rest, ArgParse& p,
                               const InputModel::ArgResolver& atoms) {
    auto& args = p.args;
    for (size_t i = start; i < e.args.size(); i++) {
        const auto& spec = e.args[i];
        bool command = spec.typeMask & kArgCommandText;
        if (!command || ltrim(rest).empty()) rest = ltrim(rest);
        if (rest.empty()) {
            bool nullable = spec.typeMask & kArgNull;
            if (nullable && !p.promptNull) {
                args.push_back(nullArg());
                continue;
            }
            if (command) return usageLine(e);  // no dialog
            // A listbox of atoms: a lone choice is taken without a dialog unless None is offered; none sends nothing.
            if (std::optional<std::vector<SrcCandidate>> cands = listedAtoms(spec, atoms)) {
                if (cands->empty()) {
                    p.quiet = true;
                    return e.name + ": argument " + std::to_string(i + 1) +
                           " has nothing to choose from \xe2\x80\x94 nothing sent";
                }
                if (cands->size() == 1 && !nullable) {
                    args.push_back(atomArg(cands->front()));
                    p.line += " " + verbCommandName(cands->front().name);
                    p.promptNull = true;
                    continue;
                }
                p.listed = std::move(cands);
            }
            p.missing = i;
            return e.name + ": missing argument " + std::to_string(i + 1) + " \xe2\x80\x94 " + usageLine(e);
        }
        bool last = i + 1 == e.args.size();
        if (command) {
            // The rest of the line verbatim, quotes and backslashes included; only a "..." followed by more is refused.
            std::string after, r2;
            if (nextToken(ltrim(rest), after).kind == Token::Quoted && !after.empty())
                return notValid(e, nextToken(after, r2).raw);
            VerbArg a;
            a.tag = 2;
            a.str = escapeVerbText(rest);
            args.push_back(a);  // DreamDaemon parses escapes back out
            rest.clear();
            continue;
        }
        bool textLike = (spec.typeMask == 0 && spec.filterKind != kFilterList) || (spec.typeMask & kArgText);
        std::string after;
        Token tok = nextToken(rest, after);
        // `as text|anything`: a "..." token is the text, a bare one an atom name. A single-quoted or backslashed token is refused.
        bool atomText =
            textLike && (spec.typeMask & kArgAnything) && tok.kind != Token::Quoted && tok.kind != Token::QuotedRest;
        if (textLike && !atomText) {
            // A text argument is a "..." token. A bare or '...' one is refused unless it is the last argument, which then
            // takes the rest of the line verbatim.
            VerbArg a;
            a.tag = 2;
            if (tok.kind == Token::Quoted || tok.kind == Token::QuotedRest)
                a.str = tok.value;
            else if (last) {
                a.str = trim(rest);
                after.clear();
            } else
                return notValid(e, tok.raw);
            args.push_back(a);
            rest = after;
            continue;
        }
        rest = after;
        // A number, an atom, `anything` and a list choice are a bare token. Quoted or single-quoted forms are refused,
        // and list choices send no ListResolve for them.
        if (tok.kind != Token::Bare) {
            if ((spec.typeMask & (kArgNum | kArgAtoms | kArgAnything)) || resolvesThroughServer(spec))
                return notValid(e, tok.raw);
            VerbArg a;
            a.tag = 2;
            a.str = tok.kind == Token::Single ? tok.raw : tok.value;
            args.push_back(a);
            continue;
        }
        uint32_t ref;
        float n;
        if (parseRefArg(tok.raw, ref)) {
            VerbArg a;
            a.tag = 0;
            a.kind = proto::enc::RefObj;
            a.ref = ref;
            args.push_back(a);
            continue;
        }
        if (parseNumArg(tok.raw, n)) {
            VerbArg a;
            a.tag = 1;
            a.num = n;
            args.push_back(a);
            continue;
        }
        if (atoms) {
            if (std::optional<std::vector<SrcCandidate>> cands = atoms(spec)) {
                // An atom the client names itself: [tag 0][kind][ref] at once, or a local refusal with no frame.
                const SrcCandidate* c = atomTyped(*cands, tok.raw);
                if (!c) return notValid(e, refusedAtom(*cands, tok, rest));
                args.push_back(atomArg(*c));
                continue;
            }
        }
        if (resolvesThroughServer(spec)) {
            // A typed list argument goes out as typed: the server matches it, escapes and all, and the answer arrives
            // as S2C::ResolveReply.
            p.listAt = i;
            p.listText = tok.raw;
            p.listRest = rest;
            return "";
        }
        if (spec.typeMask & kArgNum) {
            char* end = nullptr;
            float f = std::strtof(tok.raw.c_str(), &end);
            if (tok.raw.empty() || *end) return notValid(e, tok.raw);
            VerbArg a;
            a.tag = 1;
            a.num = f;
            args.push_back(a);
            continue;
        }
        VerbArg a;
        a.tag = 2;
        a.str = tok.raw;
        args.push_back(a);
    }
    // Text left over is refused at its first token: `SetNum 5 6 7` refuses `6`, `Pick thing extra` refuses `extra`.
    rest = ltrim(rest);
    if (!rest.empty()) {
        std::string r2;
        return notValid(e, nextToken(rest, r2).raw);
    }
    return "";
}

// The rest of a verb from argument `start` on: frames, a missing argument (titled from `line`), or a list resolve.
static CommandResult continueVerb(const VerbEntry& e, const VerbSrc& vs, const std::string& line,
                                  std::vector<VerbArg> args, size_t start, const std::string& rest,
                                  const InputModel::ArgResolver& atoms, bool promptNull = false) {
    CommandResult r;
    // Stock's >65535 command IDs truncated in the targeted fixture and did not
    // execute. Retain full dictionary/source identity, but do not reproduce that
    // alias or claim an unverified high-ID dispatch extension.
    if (e.id > UINT16_MAX) {
        r.error = "Unsupported verb ID above 65535: " + e.name;
        return r;
    }
    ArgParse p;
    p.args = std::move(args);
    p.line = line;
    p.promptNull = promptNull;
    r.error = verbArgsFor(e, start, rest, p, atoms);
    r.quiet = p.quiet;
    if (p.listAt != SIZE_MAX) {
        r.resolve = ListResolve{MissingArg{e, p.listAt, p.line, p.args, vs, p.promptNull, std::nullopt}, p.listText,
                                p.listRest};
        r.error = "verb " + e.name + ": argument " + std::to_string(p.listAt + 1) +
                  " resolves through the server (ListResolve)";
        return r;
    }
    if (p.missing != SIZE_MAX)
        r.missing = MissingArg{e, p.missing, p.line, p.args, vs, p.promptNull, std::move(p.listed)};
    if (!r.error.empty()) return r;
    r.frames.push_back({C2S::Command, proto::enc::encodeVerb(uint16_t(e.id), vs.kind, vs.ref, p.args, e.wideId)});
    return r;
}

// A typed line, as stock DreamSeeker resolves it. Every verb id of the typed name is one match, in id order (the client's
// verb table, not the order the 0x0011s arrived). A match whose src the line need not name has it at once (implicitSrc);
// for the others the first word after the name must name it. Each further word gets ONE reading for all the matches:
// the readings every open match offers for its next slot are gathered in match order, and the first literal (number,
// text, command text) is taken, else the best-named atom. A match that cannot take that reading drops out. Of the
// complete matches srcBetter picks one, the earlier keeping a tie.
namespace {
constexpr uint32_t kArgTextLike = 0x28804;  // the masks a "..." word, or the last argument's rest of line, fills
constexpr uint32_t kArgAtomish = 0x1123;    // mob | obj | turf | area | anything: a bare word may name an atom
struct Reading {
    enum Kind { Num, Text, Command, Atom, Server, Raw, Fail } kind = Fail;  // Raw: a `ref:` / `num:` word
    bool rest = false;                                                      // takes the rest of the line
    SrcCandidate atom;
    bool whole = true;  // Atom: named by its whole name, not its last words
};
struct Match {
    const VerbEntry* v = nullptr;
    std::vector<SrcCandidate> named;  // the srcs the line would have to name
    std::optional<SrcCandidate> src;
    bool typed = false;      // the src is the first word's
    bool resolving = false;  // a word went to the server: the rest of the line is parsed once it answers
    size_t argc = 0;         // arguments the words filled
    bool failed = false;
    std::string failTok, after;  // after: the line after a typed src
};
bool sameAtom(const SrcCandidate& a, const SrcCandidate& b) {
    return a.src.kind == b.src.kind && a.src.ref == b.src.ref;
}
// The src a verb has without a word, in stock's order: the client, usr (a default mob verb), usr's turf or area (a
// default turf / area verb), else the first atom a `set src = <list>` verb admits (mobs, then objs, by ref).
std::optional<SrcCandidate> implicitSrc(const VerbEntry& v, const std::vector<SrcCandidate>& cands) {
    for (const SrcCandidate& c : cands)
        if (isClient(c)) return c;
    if (v.src == 0) {
        for (const SrcCandidate& c : cands)
            if (ownMob(c)) return c;
        for (uint8_t k : {kVerbSrcTurf, kVerbSrcArea})
            for (const SrcCandidate& c : cands)
                if (c.src.kind == k) return c;
    }
    if (!(v.flags & 0x80) && (v.flags & kVerbFlagSrcEquals) && !cands.empty()) return cands.front();
    return std::nullopt;
}
// Stock's compare of two complete matches (a the later): a typed src beats one the line did not name, then the nearer
// src (SrcCandidate::dist). At one distance off the map, anything beats the client, then anything beats usr, then the
// earlier in usr.contents wins; in view, the one earlier in its cell's list. The earlier match keeps any other tie.
bool srcBetter(const Match& a, const Match& b) {
    if (a.typed != b.typed) return a.typed;
    const SrcCandidate &sa = *a.src, &sb = *b.src;
    int da = distOf(sa), db = distOf(sb);
    if (da != db) return da < db;
    if (da < 0) {
        if (isClient(sa) != isClient(sb)) return isClient(sb);
        if (ownMob(sa) != ownMob(sb)) return ownMob(sb);
        return sa.carried >= 0 && sb.carried >= 0 && sa.carried < sb.carried;
    }
    return sa.cell >= 0 && sb.cell >= 0 && sa.cell < sb.cell;
}
bool isNumber(const std::string& s) {
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return !s.empty() && end != s.c_str() && !*end;
}
void atomReadings(const std::vector<SrcCandidate>& cands, const std::string& raw, std::vector<Reading>& out) {
    for (const SrcCandidate& c : cands)
        if (atomTyped({c}, raw)) out.push_back({Reading::Atom, false, c, patternIs(namePattern(raw), c.name, 0)});
}
// What word `word` (0 = the first after the name) can be to match m: its src, else its next argument. `out` is shared by
// every match, so a bare word is the last argument's text only when no match read it otherwise.
void readingsFor(const Match& m, size_t word, const Token& tok, const InputModel::ArgResolver& atoms,
                 std::vector<Reading>& out) {
    bool bare = tok.kind == Token::Bare, quoted = tok.kind == Token::Quoted || tok.kind == Token::QuotedRest;
    if (word == 0 && !m.src) {
        if (bare) atomReadings(m.named, tok.raw, out);
        return;
    }
    const auto& args = m.v->args;
    if (m.argc >= args.size()) return;
    const VerbArgSpec& a = args[m.argc];
    uint32_t base = a.typeMask & ~kArgNull;
    uint32_t ref;
    float n;
    if (bare && (parseRefArg(tok.raw, ref) || parseNumArg(tok.raw, n))) out.push_back({Reading::Raw});
    if ((a.typeMask & kArgNum) && bare && isNumber(tok.raw)) out.push_back({Reading::Num});
    if ((a.typeMask & kArgTextLike) && quoted) out.push_back({Reading::Text, tok.kind == Token::QuotedRest});
    if (base == kArgCommandText) out.push_back({Reading::Command, true});
    if (base == 0 && a.filterKind == 0 && bare) out.push_back({Reading::Text});
    if (bare) {
        if (resolvesThroughServer(a) || a.filterKind == kFilterWorld)
            out.push_back({Reading::Server});
        else if ((a.typeMask & kArgAtomish) && atoms)
            if (std::optional<std::vector<SrcCandidate>> c = atoms(a)) atomReadings(*c, tok.raw, out);
    }
    if (out.empty() && !quoted && m.argc + 1 == args.size() &&
        (base == kArgText || base == kArgMessage || base == 0x8000 || base == 0x20000))
        out.push_back({Reading::Text, true});
}
// The one reading a word gets: the first literal, else the best-named atom (whole names first, then the nearer, the
// earlier keeping a tie), else a server-resolved argument's word as typed.
Reading chosenReading(const std::vector<Reading>& rs) {
    std::vector<Reading> named;
    bool server = false;
    for (const Reading& x : rs) {
        if (x.kind == Reading::Num || x.kind == Reading::Text || x.kind == Reading::Raw) return x;
        if (x.kind == Reading::Command) return named.empty() ? x : named.front();
        if (x.kind == Reading::Atom) named.push_back(x);
        if (x.kind == Reading::Server) server = true;
    }
    if (!named.empty()) {
        std::stable_sort(named.begin(), named.end(), [](const Reading& a, const Reading& b) {
            if (a.whole != b.whole) return a.whole;
            return distOf(a.atom) < distOf(b.atom);
        });
        return named.front();
    }
    return {server ? Reading::Server : Reading::Fail};
}
bool takes(const VerbArgSpec& a, const Reading& x, const InputModel::ArgResolver& atoms) {
    switch (x.kind) {
        case Reading::Num: return a.typeMask & kArgNum;
        case Reading::Text: return (a.typeMask & kArgTextLike) || (a.typeMask & ~kArgNull) == 0;
        case Reading::Command: return a.typeMask & kArgCommandText;
        case Reading::Server:
        case Reading::Raw: return true;
        case Reading::Atom: {
            if (!(a.typeMask & kArgAtomish) || !atoms) return false;
            std::optional<std::vector<SrcCandidate>> c = atoms(a);
            return c && std::any_of(c->begin(), c->end(), [&](const SrcCandidate& o) { return sameAtom(o, x.atom); });
        }
        default: return false;
    }
}
bool textNext(const Match& m) {
    return m.src && m.argc < m.v->args.size() && (m.v->args[m.argc].typeMask & kArgTextLike);
}
}  // namespace

CommandResult InputModel::verbCommand(const std::string& line, bool haveSelf, const SrcResolver& src,
                                      const ArgResolver& atoms, bool promptNull) const {
    CommandResult r;
    std::string rest;
    const VerbEntry* e = matchVerb(line, rest);
    if (!e) {
        r.error = "Unrecognized or inaccessible verb: " + trim(line).substr(0, trim(line).find_first_of(" \t"));
        return r;
    }
    if (!haveSelf) {
        r.error = "verb " + e->name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    // Exactly one frame, with the verb's real src: a /client verb goes out as [08][0], an own /mob verb as [02][mob ref],
    // an /obj verb as [01][obj ref], a /turf verb as [03][(x-1) | (y-1) << 16]. The srcs come from model::verbSrcCandidates,
    // asked for every id of the name: an override is its own id and an atom carries only its own type's, so each
    // candidate goes out under the id it carries (a re-run's picked candidate keeps its id).
    std::vector<Match> ms;
    bool carried = false;
    for (const auto& [id, v] : verbs_) {
        if (v.name != e->name) continue;
        Match m;
        m.v = &v;
        std::vector<SrcCandidate> cands;
        if (src)
            for (SrcCandidate c : src(id)) {
                if (c.verb && *c.verb != id) continue;
                c.verb = id;
                cands.push_back(std::move(c));
            }
        carried |= !cands.empty();
        m.src = implicitSrc(v, cands);
        if (!m.src) m.named = std::move(cands);
        ms.push_back(std::move(m));
    }
    // A known verb that nothing carries: DreamSeeker sends nothing and prints nothing.
    if (!carried) {
        r.error = "verb " + e->name + ": no atom the client knows carries it \xe2\x80\x94 not sent";
        r.quiet = true;
        return r;
    }
    std::string text = rest;
    for (size_t word = 0; !text.empty(); word++) {
        std::string after;
        Token tok = nextToken(text, after);
        std::vector<Reading> rs;
        for (const Match& m : ms)
            if (!m.failed) readingsFor(m, word, tok, atoms, rs);
        Reading x = chosenReading(rs);
        for (Match& m : ms) {
            if (m.failed) continue;
            if (word == 0 && !m.src) {
                auto c = std::find_if(m.named.begin(), m.named.end(), [&](const SrcCandidate& o) {
                    return x.kind == Reading::Atom && sameAtom(o, x.atom);
                });
                if (c != m.named.end()) {
                    m.src = *c;
                    m.typed = true;
                    m.after = after;
                    continue;
                }
            } else if (m.argc < m.v->args.size() && takes(m.v->args[m.argc], x, atoms)) {
                m.argc++;
                m.resolving = x.kind == Reading::Server;
                continue;
            }
            m.failed = true;
            m.failTok = tok.raw;
        }
        if (x.rest || x.kind == Reading::Server) break;
        text = after;
    }
    auto runOn = [&](const Match& m, const SrcCandidate& c, bool picked) {
        const VerbEntry& v = *m.v;
        std::string shown = verbCommandName(v.name), args = m.typed ? m.after : rest;
        bool pn = promptNull;
        if (picked) {
            shown += " " + verbCommandName(c.name);
            pn = true;
        }
        if (!rest.empty()) shown += " " + rest;
        // A leading command_text argument starts one character after the first word and keeps the spacing after that.
        if (!m.typed && !v.args.empty() && (v.args[0].typeMask & kArgCommandText)) {
            args = line.substr(std::min(line.size(), line.find_first_not_of(" \t")));
            size_t w = args.find_first_of(" \t");
            args = w == std::string::npos ? "" : args.substr(w + 1);
        }
        return continueVerb(v, c.src, shown, {}, 0, args, atoms, pn);
    };
    auto complete = [&](const Match& m) {
        if (m.failed || !m.src) return false;
        if (m.resolving) return true;
        for (size_t i = m.argc; i < m.v->args.size(); i++)
            if (!(m.v->args[i].typeMask & kArgNull) || promptNull) return false;
        return true;
    };
    const Match* win = nullptr;
    for (const Match& m : ms)
        if (complete(m) && (!win || srcBetter(m, *win))) win = &m;
    if (win) return runOn(*win, *win->src, false);
    // None complete. One that only lacks its src lists the atoms that carry it as `<Verb> word`, one row per name, on the
    // nearest atom of that name (a row runs as the line with its name typed); a single row is taken without asking. One
    // that lacks an argument asks for it, a text one first, on the match that would win once it is filled.
    std::vector<const Match*> open;
    for (const Match& m : ms)
        if (!m.failed) open.push_back(&m);
    const Match* ask = nullptr;
    for (const Match* m : open) {
        if (!m->src || m->argc >= m->v->args.size()) continue;
        if (!ask || (textNext(*m) != textNext(*ask) ? textNext(*m) : srcBetter(*m, *ask))) ask = m;
    }
    if (ask) return runOn(*ask, *ask->src, false);
    std::vector<std::pair<const Match*, SrcCandidate>> listed;
    for (const Match* m : open) {
        if (m->src) continue;
        for (const SrcCandidate& c : m->named) {
            auto row = std::find_if(listed.begin(), listed.end(),
                                    [&](const auto& o) { return sameAtom(o.second, c) || o.second.name == c.name; });
            if (row == listed.end())
                listed.push_back({m, c});
            else if (!sameAtom(row->second, c) && distOf(c) < distOf(row->second))
                *row = {m, c};
        }
    }
    if (listed.size() == 1) return runOn(*listed[0].first, listed[0].second, true);
    if (listed.size() > 1) {
        std::vector<SrcCandidate> cands;
        for (const auto& [m, c] : listed) cands.push_back(c);
        r.srcChoice = SrcChoice{verbCommandName(e->name) + " word", line, std::move(cands), promptNull};
        r.error = "verb " + e->name + ": " + std::to_string(r.srcChoice->candidates.size()) +
                  " atoms carry it \xe2\x80\x94 asking which";
        return r;
    }
    if (!open.empty()) {
        r.error = "verb " + e->name + ": nothing the line could name carries it \xe2\x80\x94 not sent";
        r.quiet = true;
        return r;
    }
    // Every match refused a word: per match, `Sorry, the following is not valid: <word>` (unless it repeats the line
    // before) and its usage. A lone match keeps its own parse's wording.
    if (ms.size() == 1 && ms[0].src) {
        CommandResult one = runOn(ms[0], *ms[0].src, false);
        if (one.frames.empty() && !one.resolve) return one;
    }
    std::string last;
    for (const Match& m : ms) {
        if (!r.error.empty()) r.error += "\n";
        if (m.failTok != last) r.error += "Sorry, the following is not valid: " + m.failTok + "\n";
        r.error += usageLine(*m.v);
        last = m.failTok;
    }
    return r;
}

// ================================================================================================================
// THE MISSING-ARGUMENT PROMPT
// ================================================================================================================
// When a typed line runs out before a required argument, DreamSeeker opens a dialog for it. The control comes from the
// argument's type mask, not its `in` clause: a text or message mask gets a text box (multi-line for message), a num
// mask a number box, and a list filter (FilterKind 0x40) a listbox of the list's choices, except that `as text in list`
// stays a text box. An atom argument DreamSeeker resolves itself gets a listbox built on the client with no
// VerbExpansion. The title is the verb's command name, the line so far and the argument's token. The text above the
// field is the verb's `set desc`. OK sends the value verbatim as that argument; an empty box sends "", not null. Cancel
// or Escape sends nothing. A non-number in a number box is refused in place.
//
// The value is never appended to the line and re-parsed. The typed-line rules (an unclosed leading quote is dropped,
// a "..." token closes only before whitespace) fit typed text, not dialog text. completeArg() builds the argument
// directly and encodes the verb with the arguments already parsed.
//
// A nullable argument (0x80) the line ran out before goes out as null, or gets a dialog (verbCommand says when;
// MissingArg::promptNull): its token in square brackets, with a None button that sends null.
// A `mob in world` argument with a single candidate is auto-filled by DreamSeeker with no dialog; that scope is
// server-side and is not modelled here.
// A List prompt's choice resolves through the server (ListResolve, input.h).
namespace {
std::string argTokenBare(uint32_t m) {
    if (m & (kArgText | kArgMessage)) return "\"text\"";
    if (m & kArgNum) return "number";
    if (m & kArgAnything) return "anything";
    if (m == 0) return "word";
    return argTypeName(m);  // mob / obj / turf / area: the usage-line tokens
}
std::string argToken(const VerbArgSpec& a) {
    std::string t = argTokenBare(a.typeMask & ~kArgNull);
    return a.typeMask & kArgNull ? "[" + t + "]" : t;
}
// A prompted value as the next dialog's title shows it (display only, never parsed). Both quote characters are
// backslash-escaped, and the value is double-quoted; text always is, other values only when they need it.
std::string quoteArg(const std::string& v, bool text) {
    if (v.empty()) return "\"\"";
    if (!text && v.find_first_of(" \t\"'") == std::string::npos && v[0] != '#' && v.rfind("num:", 0) != 0 &&
        v.rfind("ref:", 0) != 0)
        return v;
    std::string q = "\"";
    for (char c : v) {
        if (c == '"' || c == '\'') q += '\\';
        q += c;
    }
    return q + "\"";
}
}  // namespace

ArgPrompt argPrompt(const MissingArg& m) {
    ArgPrompt p;
    p.message = m.verb.desc;
    std::string rest;
    {  // the line so far, under the verb's own spelling
        std::string line = trim(m.line);
        rest = line.size() > m.verb.name.size() ? trim(line.substr(m.verb.name.size())) : "";
    }
    p.title = verbCommandName(m.verb.name) + (rest.empty() ? "" : " " + rest);
    if (m.index >= m.verb.args.size()) return p;
    const VerbArgSpec& a = m.verb.args[m.index];
    p.title += " " + argToken(a);
    p.allowNull = a.typeMask & kArgNull;
    if (m.atoms) {
        p.kind = ArgPrompt::List;
        for (const SrcCandidate& c : *m.atoms) p.choices.push_back(c.name);
    } else if (a.typeMask & kArgMessage)
        p.kind = ArgPrompt::Message;
    else if (a.typeMask & kArgText)
        p.kind = ArgPrompt::Text;  // `as text in list` is a text box, not a list
    else if (a.typeMask & kArgNum)
        p.kind = ArgPrompt::Num;
    else if (a.filterKind == kFilterList) {
        p.kind = ArgPrompt::List;
        p.ordinal = a.filter;
    }
    return p;
}

// A dialog value travels DM-escaped. DreamDaemon parses escapes in a verb's string argument, and stock DreamSeeker
// escapes exactly four characters: backslash, double quote, single quote, and a newline (as `\n`). Other characters go
// out bare. ExpansionReply choices are not in that form (unescapeVerbText); the picked value is escaped like any other.
// The same writer serves input() (model/prompt.cpp).
std::string escapeVerbText(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '\n') {
            o += "\\n";
            continue;
        }
        if (c == '\\' || c == '"' || c == '\'') o += '\\';
        o += c;
    }
    return o;
}

static std::string argDisplayLine(const MissingArg& m, const std::string& value, bool text) {
    size_t end = m.line.find_last_not_of(" \t");
    return (end == std::string::npos ? "" : m.line.substr(0, end + 1)) + " " + quoteArg(value, text);
}

std::string escapeChoiceText(const std::string& choice) {
    std::string o;
    for (char c : choice) {
        if (c == '\r') continue;
        o += c == '\t' ? std::string("\\t") : escapeVerbText(std::string(1, c));
    }
    return o;
}

std::string listPickText(const std::string& choice) { return verbCommandName(escapeChoiceText(choice)); }

CommandResult InputModel::completeArg(const MissingArg& m, const std::string& value, bool haveSelf,
                                      const ArgResolver& atoms) {
    CommandResult r;
    if (m.index >= m.verb.args.size() || m.args.size() != m.index) {
        r.error = m.verb.name + ": stale argument dialog";
        return r;
    }
    if (!haveSelf) {
        r.error =
            "verb " + m.verb.name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    const VerbArgSpec& spec = m.verb.args[m.index];
    std::vector<VerbArg> args = m.args;
    VerbArg a;
    if (resolvesThroughServer(spec)) {
        // A listbox choice is sent back verbatim in a ListResolve, and the Command carries the server's answer. `#1`
        // and `num:5` are the choice's own text here, never the typed path's ref or number.
        r.resolve = ListResolve{m, value, ""};
        r.resolve->at.line = argDisplayLine(m, value, false);
        r.resolve->at.promptNull = true;
        r.error =
            m.verb.name + ": argument " + std::to_string(m.index + 1) + " resolves through the server (ListResolve)";
        return r;
    }
    std::string shown = value;
    bool text = false;
    if (m.atoms) {
        const SrcCandidate* c = atomNamed(*m.atoms, value);
        if (!c) {
            r.error = "Sorry, the following is not valid: " + value;
            return r;
        }
        a = atomArg(*c);
        shown = verbCommandName(c->name);
    } else if (spec.filterKind == kFilterList || (spec.typeMask & (kArgText | kArgMessage))) {
        // A text or message box, or `as text in list`: the string as given, escaped (escapeVerbText).
        a.tag = 2;
        a.str = escapeVerbText(value);
        text = true;
    } else if (spec.typeMask & kArgNum) {
        std::string t = trim(value);
        char* end = nullptr;
        float f = std::strtof(t.c_str(), &end);
        if (t.empty() || *end) {
            r.error = "Sorry, the following is not valid: " + value;
            return r;
        }  // the dialog refuses it first
        a.tag = 1;
        a.num = f;
    } else {
        // mob / obj / anything with no list: the typed path's ref:/num: forms, an atom the client names itself
        // (ArgResolver), else a string. The dialog is a text box here, so its text is matched as a typed token would be.
        std::string t = trim(value);
        uint32_t ref;
        float n;
        std::optional<std::vector<SrcCandidate>> cands;
        if (atoms) cands = atoms(spec);
        if (cands) {
            const SrcCandidate* c = atomNamed(*cands, t);
            if (!c) {
                r.error = "Sorry, the following is not valid: " + t;
                return r;
            }
            a = atomArg(*c);
            shown = verbCommandName(c->name);
        } else if (parseRefArg(t, ref)) {
            a.tag = 0;
            a.kind = proto::enc::RefObj;
            a.ref = ref;
        } else if (parseNumArg(t, n)) {
            a.tag = 1;
            a.num = n;
        } else {
            a.tag = 2;
            a.str = escapeVerbText(value);
        }
    }
    args.push_back(a);
    // The next argument may still be missing: a second dialog, titled with the value so far. Once a dialog has been
    // answered, a nullable argument gets one too.
    return continueVerb(m.verb, m.src, argDisplayLine(m, shown, text), std::move(args), m.index + 1, "", atoms, true);
}

CommandResult InputModel::completeNull(const MissingArg& m, bool haveSelf, const ArgResolver& atoms) {
    CommandResult r;
    if (m.index >= m.verb.args.size() || m.args.size() != m.index || !(m.verb.args[m.index].typeMask & kArgNull)) {
        r.error = m.verb.name + ": stale argument dialog";
        return r;
    }
    if (!haveSelf) {
        r.error =
            "verb " + m.verb.name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    std::vector<VerbArg> args = m.args;
    args.push_back(nullArg());
    return continueVerb(m.verb, m.src, m.line, std::move(args), m.index + 1, "", atoms, true);
}

CommandResult InputModel::completeResolve(const ListResolve& rs, uint8_t kind, uint32_t value, bool haveSelf,
                                          const ArgResolver& atoms) {
    CommandResult r;
    const MissingArg& m = rs.at;
    if (m.index >= m.verb.args.size() || m.args.size() != m.index) {
        r.error = m.verb.name + ": stale list resolve";
        return r;
    }
    if (!haveSelf) {
        r.error =
            "verb " + m.verb.name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    // Text that names no list entry draws null (00 ffff0000): stock DreamSeeker sends no Command and prints the usage
    // line.
    if (kind == 0 && value == 0xffff) {
        r.error = notValid(m.verb, rs.text);
        return r;
    }
    std::vector<VerbArg> args = m.args;
    VerbArg a;
    a.tag = 0;
    a.kind = kind;
    a.ref = value;
    args.push_back(a);
    // Stock DreamSeeker re-sends the ListResolve for a typed list argument when a dialog follows; this keeps the first
    // answer.
    return continueVerb(m.verb, m.src, m.line, std::move(args), m.index + 1, rs.rest, atoms, m.promptNull);
}

CommandResult InputModel::atomArgument(const VerbEntry& e, const VerbSrc& src, uint8_t kind, uint32_t ref,
                                       const std::string& text, bool haveSelf, const std::string& atomName,
                                       const ArgResolver& atoms) {
    CommandResult r;
    if (e.args.empty()) {
        r.error = "verb " + e.name + ": takes no argument";
        return r;
    }
    if (!haveSelf) {
        r.error = "verb " + e.name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    std::string line = verbCommandName(e.name) + " " + verbCommandName(atomName.empty() ? text : atomName);
    if (resolvesThroughServer(e.args[0])) {
        r.resolve = ListResolve{MissingArg{e, 0, line, {}, src, true, std::nullopt}, text, ""};
        r.error = "verb " + e.name + ": argument 1 resolves through the server (ListResolve)";
        return r;
    }
    VerbArg a;
    a.tag = 0;
    a.kind = kind;
    a.ref = ref;
    return continueVerb(e, src, line, {a}, 1, "", atoms, true);
}

CommandResult InputModel::atomVerb(const VerbEntry& e, const VerbSrc& src, const std::string& atomName, bool haveSelf,
                                   const ArgResolver& atoms) {
    CommandResult r;
    if (!haveSelf) {
        r.error = "verb " + e.name + ": no own-mob ref yet (no SelfRef seen) \xe2\x80\x94 the server would drop this";
        return r;
    }
    return continueVerb(e, src, verbCommandName(e.name) + " " + verbCommandName(atomName), {}, 0, "", atoms, true);
}

std::string atomArgumentText(const std::string& name, uint32_t dmRef) {
    std::string o = name;
    std::replace(o.begin(), o.end(), ' ', '-');
    char hex[16];
    std::snprintf(hex, sizeof hex, ".0x%x", dmRef);
    return o + hex;
}

bool parseListResolveReply(ByteSpan b, uint8_t& kind, uint32_t& value) {
    if (b.size() < 5) return false;
    kind = b[0];
    value = le32(&b[1]);
    return true;
}

bool InputModel::takeListReply(uint8_t& kind, uint32_t& value) {
    if (listReplies_.empty()) return false;
    kind = listReplies_.front().first;
    value = listReplies_.front().second;
    listReplies_.pop_front();
    return true;
}

std::string unescapeVerbText(const std::string& v) { return v.size() > 1 && v[0] == '\\' ? v.substr(1) : v; }

// VerbExpansion layout: [0:2] Filter (u16; byte 1 is always 0), [2:6] TypeMask with the null and anything bits cleared,
// [6] src kind, [7:11] src ref (a turf: x-1 u16, y-1 u16), [11] prior-argument count, the prior arguments, then the
// argument's text so far as a C string. The src is the verb's real src; the own mob is not assumed to be mob 0.
Bytes encodeVerbExpansionRequest(const MissingArg& m, const std::string& text) {
    const VerbArgSpec a = m.index < m.verb.args.size() ? m.verb.args[m.index] : VerbArgSpec{};
    Bytes b;
    putLe16(b, a.filter);
    // The null and anything bits are cleared for the request, as for the Command argument.
    putLe32(b, a.typeMask & ~(kArgNull | kArgAnything));
    b.push_back(m.src.kind);
    putLe32(b, m.src.ref);
    Bytes v = proto::enc::encodeVerb(
        0, 0, 0, m.args);  // [02 00][id][kind][ref] = 9 bytes, then [count][args]: the same value encoding
    b.insert(b.end(), v.begin() + 9, v.end());
    b.insert(b.end(), text.begin(),
             text.end());  // the text so far: "" for a dialog's VerbExpansion, the choice for a ListResolve
    b.push_back(0x00);
    return b;
}

FrameOut stockMoveFrame(int dir) { return {C2S::Command, proto::enc::encodeMove(uint8_t(dir))}; }

}  // namespace fugue::net

#include "model/prompt.h"
#include "net/input.h"
#include "net/textmacro.h"
#include "proto/messages.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fugue::model {
namespace {
void putCStr(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
}
void put32(Bytes& b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> (8 * i)));
}
}  // namespace

std::string Prompt::typeName() const {
    switch (typeMask & ~kInputNull) {
        case kInputText: return "text";
        case kInputNum: return "num";
        case kInputMessage: return "message";
        case kInputAnything: return "anything";
        case kInputColor: return "color";
        case 0: return "anything";
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "mask 0x%04x", typeMask);
    return buf;
}

bool parseAlertRequest(ByteSpan b, Prompt& out) {
    proto::Alert m;
    try {
        m = proto::decodeAlert(b);
    } catch (const std::exception&) { return false; }
    Prompt p;
    p.kind = Prompt::Alert;
    p.id = m.id;
    p.message = net::plainText(m.message);
    p.title = net::plainText(m.title);
    for (const std::string& s : m.buttons)
        if (!s.empty()) p.buttons.push_back(net::plainText(s));
    if (p.buttons.empty()) p.buttons.push_back("OK");  // a one-argument alert() shows and returns "OK"
    out = std::move(p);
    return true;
}

bool parseInputRequest(ByteSpan b, Prompt& out) {
    proto::InputPrompt m;
    try {
        m = proto::decodeInputPrompt(b);
    } catch (const std::exception&) { return false; }
    Prompt p;
    p.kind = Prompt::Input;
    p.title = net::plainText(m.title);
    p.message = net::plainText(m.message);
    p.def = inputDefaultText(m.defaultValue);
    p.typeMask = m.typeMask;
    p.filter = m.filter;
    p.filterKind = m.filterKind;
    p.id = m.id;
    p.inList = p.filterKind == kInputFilterList && !(p.typeMask & kInputText);  // `as text in L` is a plain text field
    if ((p.typeMask & ~kInputNull) == kInputNum && !numericDefaultShown(p.def)) p.def.clear();
    out = std::move(p);
    return true;
}

std::string inputDefaultText(const std::string& sent) {
    std::string o;
    for (size_t i = 0; i < sent.size(); i++) {
        if (sent[i] != '\\') {
            o += sent[i];
            continue;
        }
        if (++i == sent.size()) break;
        char c = sent[i];
        if (c == 'n')
            o += '\n';
        else if (c == 't')
            o += '\t';
        else if (c == '\\' || c == '"' || c == '[')
            o += c;
    }
    return o;
}

bool numericDefaultShown(const std::string& def) {
    size_t i = !def.empty() && def[0] == '-';
    bool dot = false;
    for (; i < def.size(); i++) {
        if (def[i] == '.' && !dot)
            dot = true;
        else if (def[i] < '0' || def[i] > '9')
            return false;
    }
    return true;
}

bool inputChoiceMatches(const std::string& choice, const std::string& def) {
    if (def.empty()) return false;
    std::string c;
    for (size_t i = 0; i < choice.size(); i++) {
        if (choice[i] == '\n') return false;
        if (choice[i] == '\\' && ++i == choice.size()) break;
        c += choice[i];
    }
    if (c.size() != def.size()) return false;
    for (size_t i = 0; i < c.size(); i++)
        if (std::tolower((unsigned char)c[i]) != std::tolower((unsigned char)def[i])) return false;
    return true;
}

bool parseExpansionReply(ByteSpan b, std::vector<std::string>& out) {
    proto::ExpansionReply m;
    try {
        m = proto::decodeExpansionReply(b);
    } catch (const std::exception&) { return false; }
    out.clear();
    for (const std::string& s : m.choices) out.push_back(net::plainText(net::unescapeVerbText(s)));
    return true;
}

Bytes encodeAlertReply(uint32_t id, const std::string& answer) {
    Bytes b;
    put32(b, id);
    putCStr(b, answer);
    return b;
}
// A text answer is DMValue kind 2 with the text DM-escaped, as stock DreamSeeker 516 writes it: input() `a\b` is sent
// as `02 "a\\b"`, a tab as `\t` and a CR LF line break as `\n`, and DM receives the text exactly as typed.
Bytes encodeInputText(uint32_t id, const std::string& text) {
    std::string t;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') continue;
        if (text[i] == '\t')
            t += "\\t";
        else
            t += net::escapeVerbText(text.substr(i, 1));
    }
    Bytes b{2};
    putCStr(b, t);
    put32(b, id);
    return b;
}
Bytes encodeInputNum(uint32_t id, float n) {
    Bytes b{1};
    uint32_t bits;
    std::memcpy(&bits, &n, 4);
    put32(b, bits);
    put32(b, id);
    return b;
}
// A cancelled `as null|...` input(): DMValue tag 0 (an atom reference) to the NULL reference [00][ffff0000], then the id.
// DM receives null. The null reference is 0xffff, as in every other c2s atom slot.
Bytes encodeInputNull(uint32_t id) {
    Bytes b{0, 0};
    put32(b, 0x0000ffff);
    put32(b, id);
    return b;
}
Bytes encodePendingInputExpansionRequest(uint32_t id) {
    Bytes b{0};
    put32(b, id);
    put32(b, 0);
    return b;
}

std::string inputChoiceText(const std::string& choice) {
    std::string t = net::escapeChoiceText(choice);
    for (char& c : t)
        if (c == ' ') c = '-';
    return t + ' ';
}
Bytes encodeInputResolveRequest(uint32_t id, const std::string& choice) {
    Bytes b;
    putCStr(b, inputChoiceText(choice));
    put32(b, id);
    put32(b, 0);
    return b;
}
Bytes encodeInputValue(uint32_t id, uint8_t kind, uint32_t value) {
    Bytes b{0, kind};
    put32(b, value);
    put32(b, id);
    return b;
}

Bytes encodeInputAnswer(const Prompt& p, const std::string& text) {
    if (p.typeMask & kInputNum) {
        // Parses the trimmed text as a 32-bit float. A non-numeric answer stays TEXT (0 is a value the
        // player did not type); an out-of-float32-range one is a range error, so it is text too.
        size_t a = text.find_first_not_of(" \t\r\n\v\f"), z = text.find_last_not_of(" \t\r\n\v\f");
        if (a != std::string::npos) {
            std::string t = text.substr(a, z - a + 1);
            errno = 0;
            char* end = nullptr;
            float f = std::strtof(t.c_str(), &end);
            if (end && *end == 0 && end != t.c_str() && errno != ERANGE) return encodeInputNum(p.id, f);
        }
    }
    return encodeInputText(p.id, text);
}

const Prompt* PromptBook::find(uint32_t id) const {
    for (const Prompt& p : pending_)
        if (p.id == id) return &p;
    return nullptr;
}
const Prompt* PromptBook::find(uint32_t id, bool alert) const {
    for (const Prompt& p : pending_)
        if (p.id == id && (p.kind == Prompt::Alert) == alert) return &p;
    return nullptr;
}

void PromptBook::retire(uint32_t id, bool alert) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->id == id && (it->kind == Prompt::Alert) == alert) {
            pending_.erase(it);
            return;
        }
}

bool parsePromptWithdrawal(ByteSpan b, PromptWithdrawal& out) {
    proto::PromptWithdraw m;
    try {
        m = proto::decodePromptWithdraw(b);
    } catch (const std::exception&) { return false; }
    if (m.kind > 1) return false;
    out.id = m.id;
    out.alert = m.kind == 1;
    return true;
}

bool PromptBook::withdraw(const PromptWithdrawal& w) {
    bool was = false;
    for (auto it = pending_.begin(); it != pending_.end();)
        if (it->id == w.id && (it->kind == Prompt::Alert) == w.alert) {
            it = pending_.erase(it);
            was = true;
        } else
            ++it;
    if (!withdrawn(w.id, w.alert)) withdrawn_.push_back(w);
    if (withdrawn_.size() > kCap) withdrawn_.pop_front();
    return was;
}

bool PromptBook::withdrawn(uint32_t id, bool alert) const {
    for (const PromptWithdrawal& w : withdrawn_)
        if (w.id == id && w.alert == alert) return true;
    return false;
}

const Prompt* PromptBook::onFrame(uint16_t type, ByteSpan body, bool* replaced) {
    if (replaced) *replaced = false;
    Prompt p;
    if (type == S2C::Alert) {
        if (!parseAlertRequest(body, p)) return nullptr;
    } else if (type == S2C::Input) {
        if (!parseInputRequest(body, p)) return nullptr;
    } else if (type == S2C::ExpansionReply) {
        const Prompt* q = needsChoices();
        return q ? attachChoices(q->id, body) : nullptr;
    } else if (type == S2C::PromptWithdraw) {
        PromptWithdrawal w;
        if (parsePromptWithdrawal(body, w)) withdraw(w);
        return nullptr;
    } else {
        return nullptr;
    }
    const bool alert = p.kind == Prompt::Alert;
    for (auto it = withdrawn_.begin(); it != withdrawn_.end(); ++it)
        if (it->id == p.id && it->alert == alert) {
            withdrawn_.erase(it);
            break;
        }
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->id == p.id && (it->kind == Prompt::Alert) == alert) {
            pending_.erase(it);
            if (replaced) *replaced = true;
            break;
        }
    p.seq = total_++;
    pending_.push_back(std::move(p));
    if (pending_.size() > kCap) pending_.pop_front();
    return &pending_.back();
}

const Prompt* PromptBook::attachChoices(uint32_t id, ByteSpan body) {
    for (Prompt& q : pending_)
        if (q.id == id && q.kind == Prompt::Input && q.inList && !q.choicesKnown) {
            std::vector<std::string> ch;
            if (parseExpansionReply(body, ch)) q.choices = std::move(ch);
            q.choicesKnown = true;
            return &q;
        }
    return nullptr;
}

const Prompt* PromptBook::needsChoices() const {
    for (const Prompt& p : pending_)
        if (p.kind == Prompt::Input && p.inList && !p.choicesKnown) return &p;
    return nullptr;
}

PromptReply PromptBook::answerAlert(uint32_t id, const std::string& button) {
    if (withdrawn(id, true)) return {};
    retire(id, true);
    return {C2S::Alert, encodeAlertReply(id, button)};
}
PromptReply PromptBook::answerInput(uint32_t id, const std::string& text) {
    if (withdrawn(id, false)) return {};
    Prompt p;
    if (const Prompt* q = find(id, false)) p = *q;  // not held: answer as plain text rather than dropping it
    p.id = id;
    retire(id, false);
    if (p.resolvesThroughServer()) return {C2S::InputResolve, encodeInputResolveRequest(id, text)};
    return {C2S::Input, encodeInputAnswer(p, text)};
}
PromptReply PromptBook::resolvedInput(uint32_t id, uint8_t kind, uint32_t value) {
    return {C2S::Input, encodeInputValue(id, kind, value)};
}
PromptReply PromptBook::cancel(uint32_t id) {
    // Stock DreamSeeker cannot dismiss an alert() or a non-null input() (Prompt::cancellable), so nothing is sent for one.
    if (withdrawn(id, false)) return {};
    if (const Prompt* q = find(id); q && !q->cancellable()) return {};
    retire(id, false);
    return {C2S::Input, encodeInputNull(id)};
}

}  // namespace fugue::model

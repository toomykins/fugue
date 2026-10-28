#include "skin/skinfeed.h"
#include "skin/winset.h"

#include <vector>

namespace fugue::skin {

namespace {
bool cstrAt(ByteSpan b, size_t& p, std::string& out) {
    size_t q = p;
    while (q < b.size() && b[q]) q++;
    if (q >= b.size()) return false;
    out.assign(reinterpret_cast<const char*>(&b[p]), q - p);
    p = q + 1;
    return true;
}
bool hasWildcard(const std::string& s) { return s.find_first_of("*?") != std::string::npos; }
// Which reply arm a winget takes, as stock DreamSeeker 516's client handler chooses it. The flag byte is read only when
// bytes remain after the reqid; a flag of 0 selects the named-property arm, so an absent flag and a flag of 0 are the
// same frame. Any non-zero flag selects the type arm (winexists), where the prop is never read.
bool typeArm(const WingetQuery& q) { return q.hasFlag && q.flag != 0; }
// The disclosure query (skinfeed.h): the empty control (a null and an empty control both arrive as an empty cstring),
// the named arm, and every name in the list is kFugueVersionParam. It needs no skin, so it is never held.
bool isVersionOnly(const WingetQuery& q) {
    if (!q.control.empty() || typeArm(q)) return false;
    std::vector<std::string> names = wingetParamNames(q.prop);
    for (const std::string& n : names) if (n != kFugueVersionParam) return false;
    return !names.empty();
}
bool endsWith(const std::string& s, const char* suf) {
    std::string t(suf);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
}  // namespace

bool parseWingetBody(ByteSpan body, WingetQuery& q) {
    size_t p = 0;
    if (!cstrAt(body, p, q.control) || !cstrAt(body, p, q.prop)) return false;
    if (body.size() < p + 4) return false;
    q.reqId = le32(&body[p]);
    p += 4;
    q.hasFlag = body.size() > p;
    q.flag = q.hasFlag ? body[p] : 0;
    return true;
}

void SkinFeed::loadText(const std::string& text) {
    if (live_->loadedFrom(text)) { loadErr_.clear(); c_.skinLoads++; return; }
    std::string e = live_->load(text);
    loadErr_ = e;
    if (e.empty()) c_.skinLoads++;
}

void SkinFeed::onFrame(uint16_t type, ByteSpan b) {
    if (type == 0x00af) {
        // [dmf\0][hub\0]?[flags u16]?  An empty body / empty dmf is "no skin": the default skin, unless this session's
        // 0x00ef named a skin resource.
        size_t p = 0;
        std::string dmf;
        if (b.empty() || !cstrAt(b, p, dmf) || dmf.empty()) {
            if (!textLoaded_ && !resLoaded_ && skinResId_ == 0xffffffffu && !defaultSkin_.empty() && !live_->loadedFrom(defaultSkin_))
                loadText(defaultSkin_);
            return;
        }
        textLoaded_ = true;
        loadText(dmf);
    } else if (type == 0x00ef) {
        if (b.size() < 4) return;
        skinResId_ = le32(&b[0]);
        if (!textLoaded_ && !resLoaded_) {
            auto it = dmfRes_.find(skinResId_);
            if (it != dmfRes_.end()) { resLoaded_ = true; loadText(it->second); }
        }
    } else if (type == 0x00a8) {
        std::string control, props;
        size_t p = 0;
        if (!cstrAt(b, p, control) || !cstrAt(b, p, props)) return;
        c_.winsets++;
        live_->winset(control, props, /*urlEncoded=*/true);   // DreamSeeker url-decodes a world winset's values
    } else if (type == 0x00a7) {
        // output(html, "<clone>.<control>"): the page of a winclone()d window. The text arrives verbatim (a '+' or '%' in
        // inline script is the page's own; only the argument list of a page call is url-encoded), plus the "\n" BYOND appends.
        std::string text, control;
        size_t p = 0;
        if (!cstrAt(b, p, text) || !cstrAt(b, p, control) || control.find(':') != std::string::npos) return;
        if (!text.empty() && text.back() == '\n') text.pop_back();
        live_->setClonePage(control, text);
    } else if (type == 0x00b1) {
        // winclone: [template\0][clone\0]
        std::string tmpl, clone;
        size_t p = 0;
        if (!cstrAt(b, p, tmpl) || !cstrAt(b, p, clone)) return;
        if (lowerAscii(tmpl) == "window") live_->cloneWindow(clone);
    } else if (type == 0x00b2) {
        // winshow: [window\0][flag u8], the same as a winset of is-visible
        std::string win;
        size_t p = 0;
        if (!cstrAt(b, p, win) || p >= b.size()) return;
        c_.winsets++;
        live_->winset(win, b[p] ? "is-visible=true" : "is-visible=false");
    }
}

void SkinFeed::onResource(uint32_t id, const std::string& name, ByteSpan data) {
    if (!endsWith(name, ".dmf") || data.size() > (1u << 20)) return;
    dmfRes_[id].assign(reinterpret_cast<const char*>(data.data()), data.size());
    if (!textLoaded_ && !resLoaded_ && id == skinResId_) { resLoaded_ = true; loadText(dmfRes_[id]); }
}

// Answers a winget the way stock DreamSeeker does. A null or "" control is the client-as-a-whole winget
// (LiveSkin::wingetGlobal), and a wildcard control is answered by LiveSkin::wingetWildcard. Stock sends a null
// control as an empty control string, with no flag. The arm is chosen as DreamSeeker chooses it (typeArm).
std::string SkinFeed::answer(const WingetQuery& q) const {
    if (typeArm(q)) {
        if (q.control.empty() || hasWildcard(q.control)) return "";
        return live_->wingetType(q.control);   // the skin's control first, then a browse() popup (MAIN)
    }
    if (q.prop.empty()) return "";
    if (q.control.empty())
        // kFugueVersionParam is answered by NAME only (skinfeed.h): wingetGlobal consults this hook for a name its own
        // table lacks, and a wildcard / `*` never reaches it.
        return live_->wingetGlobal(q.prop, [&](const std::string& k, std::string& v) {
            if (k != kFugueVersionParam) return false;
            v = version_;
            return true;
        });
    if (hasWildcard(q.control)) return live_->wingetWildcard(q.control, q.prop);
    return live_->wingetParams(q.control, q.prop);
}

void SkinFeed::submitWinget(const WingetQuery& q, int64_t nowMs, const Reply& reply) {
    c_.wingets++;
    if (isVersionOnly(q)) { reply(q.reqId, answer(q)); return; }   // needs no skin: never held
    bool answerable = typeArm(q) ? (!q.control.empty() && !hasWildcard(q.control)) : !q.prop.empty();
    if (!answerable) { reply(q.reqId, ""); return; }
    // A browse() popup answers MAIN once a skin is loaded, so the name can be checked against the skin's own controls (a
    // browse() into vg's `browseroutput` is that BROWSER control, not a popup).
    if (typeArm(q) && live_->loaded() && live_->isPopup(q.control)) { reply(q.reqId, answer(q)); return; }
    if (!live_->ready()) {
        // No skin (or no client size) yet: hold the query. The deadline turns it into the empty refusal.
        held_.push_back({q, nowMs + kWingetDeferBudgetMs});
        c_.heldTotal++;
        return;
    }
    reply(q.reqId, answer(q));
}

void SkinFeed::pump(int64_t nowMs, const Reply& reply) {
    if (held_.empty()) return;
    bool ready = live_->ready();
    std::deque<Held> keep;
    for (Held& h : held_) {
        if (ready) reply(h.q.reqId, answer(h.q));
        else if (nowMs >= h.deadline) {
            // A popup is MAIN even skinless; the client-as-a-whole values (dpi, hwmode, url, ...) need no skin either.
            c_.timedOut++;
            bool skinless = (typeArm(h.q) && live_->isPopup(h.q.control)) || (!typeArm(h.q) && h.q.control.empty());
            reply(h.q.reqId, skinless ? answer(h.q) : "");
        }
        else keep.push_back(std::move(h));
    }
    held_ = std::move(keep);
}

}  // namespace fugue::skin

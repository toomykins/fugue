// ChoiceFetches: the one choices request in flight. An input() in list (C2S::InputExpansion, which names the prompt's
// id) and an `in list` verb argument (C2S::VerbExpansion) are both answered on S2C::ExpansionReply, whose body ([count
// u16][cstring]*count) names neither the request nor the prompt. As stock DreamSeeker does, at most one is outstanding.
//
// DreamDaemon reads an InputExpansion when its DM thread is next free (seconds under load) and answers with the list of
// whichever prompt holds that id at that moment, any player's, or not at all when nothing holds it. Its s2c frames keep
// that order, so when the ExpansionReply arrives the PromptBook shows our prompts as the server saw them, and the reply
// belongs to our in-list prompt holding the id then, or to nobody. Whether a reply is coming at all is settled by a
// C2S::Probe sent right behind the request: DreamDaemon echoes it as S2C::ProbeEcho after the request's reply, if any.
// A server that has never echoed one gets the slot back after kNoEchoKeep.
//
// PromptFlow is the session's path for the prompt frames (Input, Alert, ExpansionReply, PromptWithdraw, ProbeEcho)
// through a PromptBook and the slot. onFrame never sends; pump(), run once the frames already read are handled, hands
// out the dialogs that are due in arrival order and the next request. An in-list prompt asks for its choices only when
// every earlier prompt is answered, as stock asks when it shows the dialog, and the prompts behind it wait for it.
#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <vector>
#include "model/prompt.h"
#include "proto/messages.h"

namespace fugue {

struct ChoiceFetches {
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kWait{3000}, kNoEchoKeep{30000};
    static constexpr uint16_t kProbeTag = 0x0064;
    struct Fetch {
        bool verbArg = false;
        uint32_t id = 0;     // verb argument: the dialog's local id; input(): the prompt id the InputExpansion names
        uint64_t seq = 0;    // input(): the prompt asked for (model::Prompt::seq)
        uint32_t token = 0;  // the Probe sent behind it
        Clock::time_point sent;
        bool expired = false;  // verb argument: its dialog opened with typed text; the reply is swallowed
    };
    std::optional<Fetch> slot;
    bool echoSeen = false;  // this server answers a Probe with a ProbeEcho
    uint32_t nextToken = 1;

    // Occupies the slot; returns the Probe body to send right after the request: [tag u16][token u32].
    Bytes start(bool verbArg, uint32_t id, uint64_t seq, Clock::time_point now) {
        slot = Fetch{verbArg, id, seq, nextToken++, now};
        Bytes b;
        putLe16(b, kProbeTag);
        putLe32(b, slot->token);
        return b;
    }
    // An S2C::ExpansionReply: the fetch it answers, which frees the slot.
    std::optional<Fetch> replied() {
        std::optional<Fetch> f = slot;
        slot.reset();
        return f;
    }
    // An S2C::ProbeEcho of one of our probes: the fetch it closes, when that fetch had no reply.
    std::optional<Fetch> echoed(ByteSpan body) {
        proto::ProbeEcho m;
        try {
            m = proto::decodeProbeEcho(body);
        } catch (const std::exception&) { return std::nullopt; }
        if (!m.hasValue || m.tag != kProbeTag) return std::nullopt;
        echoSeen = true;
        if (!slot || slot->token != m.value) return std::nullopt;  // answered already
        return replied();
    }
    // The slot's fetch, released because this server has never echoed a probe and kNoEchoKeep has passed.
    std::optional<Fetch> lapsed(Clock::time_point now) {
        if (!slot || echoSeen || now <= slot->sent + kNoEchoKeep) return std::nullopt;
        return replied();
    }
};

struct PromptFlow {
    using Clock = ChoiceFetches::Clock;
    model::PromptBook book;
    ChoiceFetches fetches;
    struct VerbArgAsk {
        uint32_t id;
        uint16_t type;
        Bytes body;
        Clock::time_point queued;
    };
    std::deque<VerbArgAsk> verbArgs;  // verb-argument requests waiting for the slot
    std::set<uint64_t> shown;         // prompts handed to the dialog host, by seq

    struct Step {
        enum Kind {
            None,            // not a prompt frame, or nothing to do
            Show,            // open `prompt`'s dialog
            Send,            // send `type` + `body` (a choices request or its probe)
            VerbArgChoices,  // `id`'s verb-argument dialog: its choices are the ExpansionReply body
            VerbArgGaveUp,   // `id`'s verb-argument dialog: no choices in time, open it with typed text
            Withdraw,        // close the dialog for (`id`, `alert`), shown or queued
            Dropped,         // an ExpansionReply no prompt of ours can take
        } kind = None;
        const model::Prompt* prompt = nullptr;
        uint32_t id = 0;
        bool alert = false;
        bool replaced = false;   // an Input/Alert replaced a pending prompt of the same kind and id: close its dialog
        bool abandoned = false;  // Dropped: the reply of a verb-argument fetch whose dialog already opened
        bool gaveUp = false;     // Show: an in-list prompt whose choices never came, shown with typed text
        uint16_t type = 0;
        Bytes body;
    };

    // Queues verb-argument dialog `id`'s choices request (c2s `type` + `request`); pump() sends it when the slot is free.
    void askVerbArg(uint32_t id, uint16_t type, Bytes request, Clock::time_point now) {
        verbArgs.push_back({id, type, std::move(request), now});
    }

    Step onFrame(uint16_t type, ByteSpan body) {
        Step st;
        if (type == S2C::Alert || type == S2C::Input) {
            const model::Prompt* p = book.onFrame(type, body, &st.replaced);
            if (p) {
                st.id = p->id;
                st.alert = p->kind == model::Prompt::Alert;
            }
        } else if (type == S2C::PromptWithdraw) {
            model::PromptWithdrawal w;
            if (!model::parsePromptWithdrawal(body, w)) return st;
            book.withdraw(w);
            st.kind = Step::Withdraw;
            st.id = w.id;
            st.alert = w.alert;
        } else if (type == S2C::ExpansionReply) {
            std::optional<ChoiceFetches::Fetch> f = fetches.replied();
            if (f && f->verbArg && !f->expired) {
                st.kind = Step::VerbArgChoices;
                st.id = f->id;
            } else if (!f || f->verbArg || !book.attachChoices(f->id, body)) {
                st.kind = Step::Dropped;
                st.abandoned = f && f->verbArg;
                if (f) st.id = f->id;
            }
        } else if (type == S2C::ProbeEcho) {
            if (std::optional<ChoiceFetches::Fetch> f = fetches.echoed(body)) unanswered(*f);
        }
        return st;
    }

    // After the frames read so far are handled, and after the player answers: the steps now due, in order.
    std::vector<Step> pump(Clock::time_point now) {
        std::vector<Step> out;
        if (std::optional<ChoiceFetches::Fetch> f = fetches.lapsed(now)) unanswered(*f);
        for (uint32_t id : unansweredArgs) out.push_back(verbArgGaveUp(id));
        unansweredArgs.clear();
        for (auto it = verbArgs.begin(); it != verbArgs.end();)
            if (now > it->queued + ChoiceFetches::kWait) {
                out.push_back(verbArgGaveUp(it->id));
                it = verbArgs.erase(it);
            } else
                ++it;
        if (fetches.slot && fetches.slot->verbArg && !fetches.slot->expired &&
            now > fetches.slot->sent + ChoiceFetches::kWait) {
            fetches.slot->expired = true;
            out.push_back(verbArgGaveUp(fetches.slot->id));
        }
        for (std::set<uint64_t>* seqs : {&shown, &gaveUp})
            for (auto it = seqs->begin(); it != seqs->end();) it = seqPending(*it) ? std::next(it) : seqs->erase(it);
        const model::Prompt* ask = nullptr;  // the in-list prompt now due, still without its choices
        bool first = true;
        for (const model::Prompt& p : book.pending()) {
            bool wasFirst = first;
            first = false;
            if (shown.count(p.seq)) continue;
            if (p.kind == model::Prompt::Input && p.inList && !p.choicesKnown) {
                if (wasFirst) ask = &p;
                break;
            }
            Step st;
            st.kind = Step::Show;
            st.prompt = &p;
            st.id = p.id;
            st.alert = p.kind == model::Prompt::Alert;
            st.gaveUp = p.inList && p.choices.empty() && gaveUp.count(p.seq);
            shown.insert(p.seq);
            out.push_back(std::move(st));
        }
        if (fetches.slot) return out;
        if (!verbArgs.empty()) {
            VerbArgAsk a = std::move(verbArgs.front());
            verbArgs.pop_front();
            Bytes probe = fetches.start(true, a.id, 0, now);
            fetches.slot->sent = a.queued;  // the dialog's wait counts from the player's command
            out.push_back(send(a.type, std::move(a.body), a.id));
            out.push_back(send(C2S::Probe, std::move(probe), a.id));
        } else if (ask) {
            Bytes probe = fetches.start(false, ask->id, ask->seq, now);
            out.push_back(send(C2S::InputExpansion, model::encodePendingInputExpansionRequest(ask->id), ask->id));
            out.push_back(send(C2S::Probe, std::move(probe), ask->id));
        }
        return out;
    }

private:
    std::set<uint64_t> gaveUp;             // in-list prompts shown without choices
    std::vector<uint32_t> unansweredArgs;  // verb-argument fetches that ended with no reply, for the next pump

    bool seqPending(uint64_t seq) const {
        for (const model::Prompt& p : book.pending())
            if (p.seq == seq) return true;
        return false;
    }
    // A fetch that ended with no reply. A verb argument's dialog opens with typed text. An input() still pending as the
    // prompt asked for got no answer for a live id, which DreamDaemon does not do, so it is shown with typed text rather
    // than asked again; a prompt that took its id since is asked for in its turn.
    void unanswered(const ChoiceFetches::Fetch& f) {
        if (f.verbArg) {
            if (!f.expired) unansweredArgs.push_back(f.id);
            return;
        }
        for (const model::Prompt& p : book.pending())
            if (p.seq == f.seq && p.inList && !p.choicesKnown) {
                book.attachChoices(p.id, ByteSpan{});  // malformed body: known, and empty
                gaveUp.insert(p.seq);
            }
    }
    static Step send(uint16_t type, Bytes body, uint32_t id) {
        Step st;
        st.kind = Step::Send;
        st.type = type;
        st.body = std::move(body);
        st.id = id;
        return st;
    }
    static Step verbArgGaveUp(uint32_t id) {
        Step st;
        st.kind = Step::VerbArgGaveUp;
        st.id = id;
        return st;
    }
};

}  // namespace fugue

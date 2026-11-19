// model/prompt.h: DM's alert() and input() dialogs, and the exact bytes that answer them.
// Qt-free and socket-free: parse a request frame, hold it pending, encode the one reply. The GUI and a headless
// client are both front ends over this, so the reply bytes exist in exactly one place.
//
// Request and reply layouts follow the stock client, and the InputType masks are the stock
// ones. Kind 2 is escaped text (kind 4 = verbatim), kind 1 a number, kind 0 a null reply.
//
//   S2C::Alert           [id u32][message\0][title\0][count u16][button\0]*count  (always 3 buttons, padded with "")
//   C2S::Alert           [id u32][answer\0]             refusal = "" (alert has no null)
//   S2C::Input           [title\0][default\0][message\0][mask u32][filter u8][filterKind u8][id u32]
//   C2S::Input           [kind u8][value][id u32]        kind 2 [cstring] DM-escaped text (prompt.cpp), kind 1 [f32]
//                                                        number, kind 0 [00][ref u32] null, kind 0 [value kind u8]
//                                                        [value u32] the server's own value (in list)
//   S2C::PromptWithdraw  [id u32][kind u8]               kind 0 an input(), 1 an alert(): its proc is gone; the dialog
//                                                        closes
//   in-list pick: C2S::InputResolve [choice\0][id u32][u32 0] -> S2C::ResolveReply [kind u8][value u32][u32 0]
//                 -> C2S::Input kind 0 (Prompt::resolvesThroughServer)
//
// THE ANSWER IS THE PLAYER'S. `as num` answers with a NUMBER when the text parses (DreamDaemon does no coercion; a
// text "42" reaches DM as a string), otherwise falls back to TEXT, never to 0.
#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>
#include "net/bytes.h"
#include "proto/msg.h"

namespace fugue::model {

constexpr uint32_t kInputText = 0x0004, kInputNum = 0x0008, kInputNull = 0x0080, kInputMessage = 0x0800,
                   kInputAnything = 0x1000, kInputColor = 0x020000;
constexpr uint8_t kInputFilterList = 0x40;

struct Prompt {
    enum Kind { Alert, Input } kind = Alert;
    uint32_t id = 0;
    std::string message, title;
    std::vector<std::string> buttons;  // alert only; empty slots removed, never empty ("OK" when nothing was labelled)
    std::string def;                   // input: seeds the editor, as stock shows it (inputDefaultText)
    uint32_t typeMask = 0;
    uint8_t filter = 0, filterKind = 0;
    bool inList = false;               // `in <list>`: the choices are not on this frame; see PromptBook::needsChoices
    std::vector<std::string> choices;  // filled from the ExpansionReply to our InputExpansion
    bool choicesKnown = false;
    uint64_t seq = 0;

    bool multiline() const { return (typeMask & kInputMessage) != 0; }
    bool numeric() const { return (typeMask & kInputNum) != 0; }
    bool isColor() const { return (typeMask & kInputColor) != 0; }
    bool nullable() const { return (typeMask & kInputNull) != 0; }
    // Whether stock DreamSeeker lets the player dismiss the prompt without an answer. Only an `as null|...` input() has a
    // Cancel button, and only there do Escape and the close box do anything (all three send the same null). An alert()
    // and every non-null input() have no Cancel: Escape is ignored, the close box is greyed out, and nothing is sent
    // until an answer.
    bool cancellable() const { return kind == Input && nullable(); }
    // Whether the answer is the SERVER's value for the chosen entry rather than its text. For `as null|anything in L`
    // (mask 0x1080) and a bare `in L` (mask 0x0), stock DreamSeeker sends C2S::InputResolve
    // (encodeInputResolveRequest); the server answers S2C::ResolveReply with the list member ([kind][value]), and the
    // input() is answered with that value (encodeInputValue), so DM receives the object itself. `as text in L` (mask
    // 0x4) is an ordinary text field. A plain text answer to an object list is accepted by DreamDaemon as that text,
    // not the object, so the server value path is required for `for(var/turf/T in A)`-style code.
    bool resolvesThroughServer() const {
        return kind == Input && inList && !(typeMask & (kInputText | kInputMessage | kInputNum));
    }
    std::string typeName() const;
};

// Request decoders (false = malformed; the caller then does NOT answer, as the decoder does on error). The title,
// message, buttons and choices come back without text-macro markers (net::plainText), as stock shows and answers them.
bool parseAlertRequest(ByteSpan body, Prompt& out);
bool parseInputRequest(ByteSpan body, Prompt& out);
// The default as stock DreamSeeker shows and returns it. DreamDaemon sends it DM-escaped: `\n`, `\t`, `\\`, `\"` and
// `\[` are unescaped, and any other backslash pair is dropped, so `\improper X` shows as `mproper X`. An `as num`
// prompt shows its default only when it is an optional '-', digits and at most one '.' (numericDefaultShown).
std::string inputDefaultText(const std::string& sent);
bool numericDefaultShown(const std::string& def);
// Whether an in-list input() selects this shown choice for its default: equal ignoring ASCII case once each `\x` pair in
// the choice is read as x. An empty default or a choice holding a newline never matches; with no match the first choice
// is selected.
bool inputChoiceMatches(const std::string& choice, const std::string& def);
// ExpansionReply -> choices as stock shows them: net::unescapeVerbText, then marker-free.
bool parseExpansionReply(ByteSpan body, std::vector<std::string>& out);

// S2C::PromptWithdraw: the server dropped a pending prompt (its proc ended, e.g. its src was deleted) and nothing will
// read an answer. Kinds other than 0 and 1 withdraw something other than a prompt and return false.
struct PromptWithdrawal {
    uint32_t id = 0;
    bool alert = false;
};
bool parsePromptWithdrawal(ByteSpan body, PromptWithdrawal& out);

// Reply encoders: the BODY of the c2s frame (type C2S::Alert / C2S::Input).
Bytes encodeAlertReply(uint32_t id, const std::string& answer);
Bytes encodeInputText(uint32_t id, const std::string& text);
Bytes encodeInputNum(uint32_t id, float n);
Bytes encodeInputNull(uint32_t id);  // [00][null ref: 00 ffff0000][id], the stock cancel
// The C2S::InputExpansion asking for an in-list input()'s choices: [00][id u32][u32 0], naming the prompt. Prompt ids
// are server-wide, and DreamDaemon answers with whichever prompt holds that id, any player's: an empty list for a
// non-list prompt, and no reply at all for an id nothing holds.
Bytes encodePendingInputExpansionRequest(uint32_t id);
// The C2S::InputResolve that asks the server for the list member a choice names (Prompt::resolvesThroughServer):
// [inputChoiceText(choice)\0][id u32][u32 0].
Bytes encodeInputResolveRequest(uint32_t id, const std::string& choice);
// A choice as stock DreamSeeker names it in that InputResolve: net::escapeChoiceText, each space written as `-`,
// and a trailing space, from the choice as parseExpansionReply shows it.
std::string inputChoiceText(const std::string& choice);
// The input() answer carrying the server's value from that S2C::ResolveReply: [00][kind u8][value u32][id u32].
Bytes encodeInputValue(uint32_t id, uint8_t kind, uint32_t value);
// The reply for a typed answer: a number for `as num` when it parses, else text.
Bytes encodeInputAnswer(const Prompt& p, const std::string& text);

struct PromptReply {
    uint16_t type = 0;
    Bytes body;
};

// The pending prompts of one session. Single-thread (the session thread); no locking.
class PromptBook {
public:
    // Feeds an s2c frame. Returns the newly pended prompt (Alert/Input), or nullptr. An ExpansionReply with a prompt
    // waiting for its list attaches the choices to the oldest such prompt and returns it; a PromptWithdraw withdraws. A
    // new prompt replaces a pending one of the same kind and id, since the server holds an id for one prompt at a time;
    // `replaced` then reports it.
    const Prompt* onFrame(uint16_t type, ByteSpan body, bool* replaced = nullptr);
    // Retires the prompt a PromptWithdraw names; true when one was pending. Its id stays refused (answer* and cancel
    // return an empty reply) until the server asks with it again.
    bool withdraw(const PromptWithdrawal& w);
    bool withdrawn(uint32_t id, bool alert) const;
    // An ExpansionReply answering the InputExpansion for prompt `id`: attaches its choices (none when malformed) and
    // returns the prompt, or nullptr when `id` is not waiting for choices.
    const Prompt* attachChoices(uint32_t id, ByteSpan body);
    const std::deque<Prompt>& pending() const { return pending_; }
    const Prompt* find(uint32_t id) const;
    const Prompt* find(uint32_t id,
                       bool alert) const;  // alert() and input() ids are matched apart, as PromptWithdraw names them
    // The in-list prompt (if any) that has asked for choices and not yet had them: the session sends InputExpansion for
    // it.
    const Prompt* needsChoices() const;

    // Each retires the prompt and returns the frame to send. Unknown ids still answer: a double click is harmless to
    // the server, and an out-of-order reply must still reach a parked proc. A withdrawn id gets an empty reply (type 0).
    PromptReply answerAlert(uint32_t id, const std::string& button);
    // For a prompt that resolvesThroughServer() the reply is an InputResolve, and the input() is not answered yet: the
    // caller feeds the ResolveReply to resolvedInput() and sends that. A ResolveReply names no request, so while
    // verb-argument resolves (net::ListResolve, also answered by ResolveReply) are in flight, the OLDEST owns it.
    PromptReply answerInput(uint32_t id, const std::string& text);
    // The input() answer for an InputResolve the server answered with (kind, value). A not-in-list text comes back null
    // (`00 ffff0000`), sent as the null answer.
    static PromptReply resolvedInput(uint32_t id, uint8_t kind, uint32_t value);
    // Null for a cancellable input (or an unknown id). For a prompt stock DreamSeeker cannot dismiss (Prompt::cancellable)
    // it returns an EMPTY reply (type 0, nothing to send) and the prompt stays pending, since the world's proc waits.
    // A withdrawn id also gets an empty reply.
    PromptReply cancel(uint32_t id);

private:
    void retire(uint32_t id, bool alert);
    std::deque<Prompt> pending_;
    std::deque<PromptWithdrawal> withdrawn_;  // newest last, kCap kept
    uint64_t total_ = 0;
    static constexpr size_t kCap = 32;
};

}  // namespace fugue::model

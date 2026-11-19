// S2C::ExpansionReply: verb-expansion reply (the current choices list).
#include "proto/registry.h"

namespace fugue::proto {

// Layout [count u16][cstring] x count. Nothing may trail the last string.
ExpansionReply decodeExpansionReply(ByteSpan body) {
    Reader r(body);
    ExpansionReply m;
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.choices.push_back(r.cstr());
    r.expectEnd();
    return m;
}

canon::Json toJson(const ExpansionReply& m) {
    std::vector<canon::Json> choices;
    for (const std::string& c : m.choices) choices.push_back(canon::str(c));
    return canon::Object().set("choices", canon::array(choices)).build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::ExpansionReply, "cpp:stock-dd-wire", jsonOf<decodeExpansionReply>);

}  // namespace fugue::proto

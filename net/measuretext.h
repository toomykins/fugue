// MeasureText request parsing and reply encoding; text layout is delegated
// to the caller.
#pragma once
#include "net/bytes.h"
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace fugue::net {
using TextMeasure = std::function<std::pair<uint16_t, uint16_t>(const std::string&, const std::string&, uint16_t)>;
inline std::optional<Bytes> measureTextReply(ByteSpan body, const TextMeasure& measure) {
    size_t at = 0;
    auto string = [&](std::string& out) {
        const size_t start = at;
        while (at < body.size() && body[at]) ++at;
        if (at == body.size()) return false;
        out.assign(reinterpret_cast<const char*>(body.data() + start), at - start); ++at;
        return true;
    };
    // A malformed body receives no reply. Valid unsupported/over-budget layout
    // receives the historical 0xffff pair, so a server proc does not park.
    std::string text, style;
    if (body.size() > 131080 || !string(text) || !string(style) || body.size() - at != 6) return {};
    const uint16_t width = uint16_t(body[at] | (body[at+1] << 8));
    auto size = measure ? measure(text, style, width) : std::pair<uint16_t,uint16_t>{uint16_t(0xffff),uint16_t(0xffff)};
    Bytes reply{uint8_t(size.first), uint8_t(size.first >> 8), uint8_t(size.second), uint8_t(size.second >> 8)};
    reply.insert(reply.end(), body.begin() + at + 2, body.end());
    return reply;
}
}

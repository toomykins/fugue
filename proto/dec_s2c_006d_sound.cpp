// S2C::Sound: sound play. Wide ids (u32 ResID) as on 516.
#include "proto/registry.h"

namespace fugue::proto {

// Head is [ResID u32][Repeat][Wait][Channel u8][Volume u16], then an optional length-guarded Marker byte:
//   marker 0x01 -> Pan u16, Frequency f32, Priority u8, Status u8, Environment i8; 0x02 -> X,Y,Z,Falloff f32;
//   0x10 -> Channel u16 override.
//   A body whose marker bits are not 01/02/10, or whose block is short or has trailing slack, keeps
//   hasMarker/marker/head plus `unresolved` = body[9:] verbatim, with every other tail field zero. The framing is known;
//   the remainder is carried verbatim rather than guessed.
Sound decodeSound(ByteSpan body) {
    if (body.size() < 9) throw DecodeError("short sound");
    Reader r(body);
    Sound m;
    m.resID = r.u32();
    m.repeat = r.u8();
    m.wait = r.u8();
    m.channel = r.u8();
    m.volume = r.u16();
    if (!r.more()) return m;
    m.hasMarker = true;
    m.marker = r.u8();
    bool ok = (m.marker & ~0x13) == 0;
    if (ok && (m.marker & 0x01)) {
        if (r.remaining() < 9)
            ok = false;
        else {
            m.pan = r.u16();
            m.frequencyBits = r.u32();
            m.priority = r.u8();
            m.status = r.u8();
            m.environment = int8_t(r.u8());
        }
    }
    if (ok && (m.marker & 0x02)) {
        if (r.remaining() < 16)
            ok = false;
        else {
            m.xBits = r.u32();
            m.yBits = r.u32();
            m.zBits = r.u32();
            m.falloffBits = r.u32();
        }
    }
    if (ok && (m.marker & 0x10)) {
        if (r.remaining() < 2)
            ok = false;
        else
            m.channelWide = r.u16();
    }
    if (ok && r.more()) ok = false;
    if (!ok) {
        Sound head = m;
        m = Sound{};
        m.resID = head.resID;
        m.repeat = head.repeat;
        m.wait = head.wait;
        m.channel = head.channel;
        m.volume = head.volume;
        m.hasMarker = true;
        m.marker = head.marker;
        auto tail = body.subspan(9);
        m.unresolved.assign(tail.begin(), tail.end());
    }
    return m;
}

canon::Json toJson(const Sound& m) {
    return canon::Object()
        .u("resID", m.resID)
        .u("repeat", m.repeat)
        .u("wait", m.wait)
        .u("channel", m.channel)
        .u("volume", m.volume)
        .boolean("hasMarker", m.hasMarker)
        .u("marker", m.marker)
        .u("pan", m.pan)
        .set("frequency", canon::f32(m.frequencyBits))
        .u("priority", m.priority)
        .u("status", m.status)
        .i("environment", m.environment)
        .set("x", canon::f32(m.xBits))
        .set("y", canon::f32(m.yBits))
        .set("z", canon::f32(m.zBits))
        .set("falloff", canon::f32(m.falloffBits))
        .u("channelWide", m.channelWide)
        .bytes("unresolved", m.unresolved)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Sound, "cpp:stock-dd-wire", jsonOf<decodeSound>);

}  // namespace fugue::proto

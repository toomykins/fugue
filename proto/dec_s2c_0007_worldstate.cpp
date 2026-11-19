// S2C::WorldState: world state = viewport/eye prologue + the same ten object arrays as InView.
//   [flags u8]
//   flags & 0x02: [mapX u16][eyeX u16]        flags & 0x04: [mapY u16][eyeY u16]
//   flags & 0x06: eye  [kind u8] (kind 0: [eyeType u8][eyeValue u32]) [viewMode u8][flags32 u32][flags32b u32]
//                      [glide_size f32][step_size f32][viewX u16][viewY u16]
//   flags & 0x08: 2 bytes, skipped                 flags & 0x10: 2 bytes, skipped
//   flags & 0x20: [sight u8]                       flags & 0x40: a DM value (proto/g3_inview.cpp readDmValue)
//   flags & 0x80: vision  [sub u8] then, per sub bit: 0x01 [see_in_dark u8], 0x02 [see_invisible u8],
//                         0x04 [sight u32], 0x08 4 x u16, 0x10 [pixel_x u16][pixel_y u16], 0x20 4 x u16 (client
//                         pixel_x/y/w/z), 0x40 4 x u16
//   then the object arrays (proto/g3_inview.cpp).
#include "proto/g3_inview.h"

namespace fugue::proto {

namespace {

template <size_t N>
void readU16s(Reader& r, std::array<uint16_t, N>& out) {
    for (uint16_t& v : out) v = r.u16();
}

template <size_t N>
canon::Json u16Array(const std::array<uint16_t, N>& v) {
    std::vector<canon::Json> out;
    for (uint16_t x : v) out.push_back(canon::u(x));
    return canon::array(out);
}

}  // namespace

WorldState decodeWorldState(ByteSpan body) {
    Reader r(body);
    WorldState m;
    m.flags = r.u8();
    m.hasX = (m.flags & 2) != 0;
    m.hasY = (m.flags & 4) != 0;
    if (m.hasX) {
        m.mapX = r.u16();
        m.eyeX = r.u16();
    }
    if (m.hasY) {
        m.mapY = r.u16();
        m.eyeY = r.u16();
    }
    if (m.hasX || m.hasY) {
        WorldState::Eye& e = m.eye.emplace();
        e.kind = r.u8();
        if (e.kind == 0) {
            e.eyeType = r.u8();
            e.eyeValue = r.u32();
        }
        e.viewMode = r.u8();
        e.flags32 = r.u32();
        e.flags32b = r.u32();
        e.glideSizeBits = r.u32();
        e.stepSizeBits = r.u32();
        e.viewX = r.u16();
        e.viewY = r.u16();
    }
    if (m.flags & 0x08) r.take(2);
    if (m.flags & 0x10) r.take(2);
    if (m.flags & 0x20) {
        m.hasSight = true;
        m.sight = r.u8();
    }
    if (m.flags & 0x40) {
        auto dm = g3::readDmValue(r);
        m.dmValue.assign(dm.begin(), dm.end());
    }
    if (m.flags & 0x80) {
        WorldState::Vision& v = m.vision.emplace();
        v.sub = r.u8();
        if (v.sub & 0x01) v.seeInDark = r.u8();
        if (v.sub & 0x02) v.seeInvisible = r.u8();
        if (v.sub & 0x04) v.sightWide = r.u32();
        if (v.sub & 0x08) readU16s(r, v.rect);
        if (v.sub & 0x10) readU16s(r, v.viewOrigin);
        if (v.sub & 0x20) readU16s(r, v.screenOffset);
        if (v.sub & 0x40) readU16s(r, v.mapBounds);
    }
    m.objects = g3::readObjectArrays(r);
    r.expectEnd();
    return m;
}

// Eye floats are emitted as raw IEEE bits. Absent fixed-size u16 arrays are all zeros, never empty.
canon::Json toJson(const WorldState& m) {
    canon::Object o;
    o.u("flags", m.flags);
    o.boolean("hasX", m.hasX).u("mapX", m.mapX).u("eyeX", m.eyeX);
    o.boolean("hasY", m.hasY).u("mapY", m.mapY).u("eyeY", m.eyeY);
    if (m.eye) {
        const WorldState::Eye& e = *m.eye;
        o.set("eye", canon::Object()
                         .u("kind", e.kind)
                         .u("eyeType", e.eyeType)
                         .u("eyeValue", e.eyeValue)
                         .u("viewMode", e.viewMode)
                         .u("flags32", e.flags32)
                         .u("flags32b", e.flags32b)
                         .set("glideSize", canon::f32(e.glideSizeBits))
                         .set("stepSize", canon::f32(e.stepSizeBits))
                         .u("viewX", e.viewX)
                         .u("viewY", e.viewY)
                         .build());
    } else {
        o.set("eye", canon::null());
    }
    o.u("sight", m.sight).boolean("hasSight", m.hasSight);
    o.bytes("dmValue", m.dmValue);
    if (m.vision) {
        const WorldState::Vision& v = *m.vision;
        o.set("vision", canon::Object()
                            .u("sub", v.sub)
                            .u("seeInDark", v.seeInDark)
                            .u("seeInvisible", v.seeInvisible)
                            .u("sightWide", v.sightWide)
                            .set("rect0x08", u16Array(v.rect))
                            .set("viewOrigin", u16Array(v.viewOrigin))
                            .set("screenOffset", u16Array(v.screenOffset))
                            .set("mapBounds", u16Array(v.mapBounds))
                            .build());
    } else {
        o.set("vision", canon::null());
    }
    canon::Object objs;
    objs.u("flags", 0).u("extra1", 0).u("extra2", 0).boolean("hasExtra1", false).boolean("hasExtra2", false);
    addJson(objs, m.objects);
    o.set("objects", objs.build());
    return o.build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::WorldState, "cpp:stock-dd-wire", jsonOf<decodeWorldState>);

}  // namespace fugue::proto

// S2C::Animate: animate() chain. 14-byte header + `count` mask-driven steps.
#include "proto/registry.h"

namespace fugue::proto {

// Header [kind u8][ref u32][ver u16][delay f32][chain u8][count u16]. Each step: [from u32][to u32][time f32][loop i32]
//   [easing u8][flags u8, plus a u16 as bits 16-31 when bit 0x80 is set], then StepDelay f32 (when flags has 0x40000000
//   or 0x4, or the previous step had ext), [tag u16], TimeBase f32 (same flags, or the first step of a chain), a NUL-
//   terminated string (0x10000000) and an ext f32 (0x8).
Animate decodeAnimate(ByteSpan body) {
    Reader r(body);
    Animate m;
    m.kind = r.u8();
    m.ref = r.u32();
    m.ver = r.u16();
    m.delayBits = r.u32();
    m.chain = r.u8();
    unsigned n = r.u16();
    bool prevExt = false;
    bool wantBase = m.chain != 0;
    for (unsigned i = 0; i < n; i++) {
        Animate::Step s;
        s.from = r.u32();
        s.to = r.u32();
        s.timeBits = r.u32();
        s.loop = int32_t(r.u32());
        s.easing = r.u8();
        s.flags = r.u8();
        if (s.flags & 0x80) s.flags |= uint32_t(r.u16()) << 16;
        bool head = (s.flags & (0x40000000u | 0x4u)) != 0;
        if (head || prevExt) {
            s.hasDelay = true;
            s.delayBits = r.u32();
        }
        s.tag = r.u16();
        if (head) wantBase = true;
        if (wantBase) {
            s.hasBase = true;
            s.timeBaseBits = r.u32();
            wantBase = false;
        }
        if (s.flags & 0x10000000u) s.str = r.cstr();
        if (s.flags & 0x8u) {
            s.hasExt = true;
            s.extBits = r.u32();
        }
        prevExt = s.hasExt;
        m.steps.push_back(std::move(s));
    }
    r.expectEnd();
    return m;
}

canon::Json toJson(const Animate& m) {
    std::vector<canon::Json> steps;
    for (const Animate::Step& s : m.steps)
        steps.push_back(canon::Object()
                            .u("from", s.from)
                            .u("to", s.to)
                            .set("time", canon::f32(s.timeBits))
                            .i("loop", s.loop)
                            .u("easing", s.easing)
                            .u("flags", s.flags)
                            .u("tag", s.tag)
                            .boolean("hasDelay", s.hasDelay)
                            .set("delay", canon::f32(s.delayBits))
                            .boolean("hasBase", s.hasBase)
                            .set("timeBase", canon::f32(s.timeBaseBits))
                            .str("str", s.str)
                            .boolean("hasExt", s.hasExt)
                            .set("ext", canon::f32(s.extBits))
                            .build());
    return canon::Object()
        .u("kind", m.kind)
        .u("ref", m.ref)
        .u("ver", m.ver)
        .set("delay", canon::f32(m.delayBits))
        .u("chain", m.chain)
        .set("steps", canon::array(steps))
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Animate, "cpp:stock-dd-wire", jsonOf<decodeAnimate>);

}  // namespace fugue::proto

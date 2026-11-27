// S2C::Appearance: atom appearance, interned by appid.
// Wire layout (516, wide appearance ids u32, negotiated verb ids u16/u32):
//   [appid u32][name cstr][suffix cstr][icon_res u32][icon_state cstr][text cstr][maptext cstr]
//   [maptext_w u16][maptext_h u16][maptext_x s16][maptext_y s16][dir u8][appearance_flags u32]
//   3 x [tag u8 (+ id u32 iff tag == 2)]  [icon_ref u32]
//   [nverbs u16][verb u16/u32 x n] [noverlays u16][u32 x n] [nunderlays u16][u32 x n]
//   [layer f32][screen_loc cstr] [vars_mask: u8, or u32 re-read from the same offset iff first byte & 0x80]
//   payloads for each set mask bit in ascending bit order (see varWidth below).
//
// Layout follows the client's appearance handler. A filter record with type 0 ends the chain early; the record is
// dropped. Anything not fully accounted for (short body, unterminated cstr, mask bits above 0xffff, bytes left over)
// throws rather than guessing a framing.
#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Filter param width per bit; -1 = cstr. 0 = not a param bit.
int filterParamWidth(uint32_t bit) {
    switch (bit) {
        case 0x1:
        case 0x2:
        case 0x4:
        case 0x8:
        case 0x10: return 4;
        case 0x20: return 2;
        case 0x40:
        case 0x100: return 4;
        case 0x200: return -1;
        case 0x400: return 80;
        case 0x800:
        case 0x1000: return 4;
        case 0x2000:
        case 0x4000: return 1;
    }
    return 0;
}

// Var payload width per mask bit; 0 = no payload (0x80), -1 = variable (0x800, 0x1000).
int varWidth(uint32_t bit) {
    switch (bit) {
        case 0x1:
        case 0x2:
        case 0x4:
        case 0x100: return 1;
        case 0x8: return 8;
        case 0x10:
        case 0x40:
        case 0x4000: return 4;
        case 0x20: return 24;
        case 0x80: return 0;
        case 0x200:
        case 0x2000: return 2;
        case 0x400: return 80;
        case 0x8000: return 4;
        case 0x800:
        case 0x1000: return -1;
    }
    throw DecodeError("0x26 unknown vars_mask bit");
}

// [count u8] then per filter [type u8][flags u8][id u32][param mask varint] and the set params in ascending bit order.
std::vector<Appearance::Filter> readFilters(Reader& r) {
    std::vector<Appearance::Filter> out;
    for (unsigned n = r.u8(), i = 0; i < n; i++) {
        Appearance::Filter f;
        f.type = r.u8();
        if (f.type == 0) break;
        f.flags = r.u8();
        f.id = r.u32();
        f.paramMask = r.varint() & ~0x80u;
        if (f.paramMask & ~0x7fffu) throw DecodeError("0x26 filter param mask has unknown high bits");
        size_t start = r.pos();
        for (uint32_t bit = 1; bit <= 0x4000; bit <<= 1) {
            if (!(f.paramMask & bit)) continue;
            int w = filterParamWidth(bit);
            if (w == 0) throw DecodeError("0x26 filter param bit unknown");
            if (w < 0)
                f.str = r.cstr();
            else
                r.take(size_t(w));
        }
        auto params = r.since(start);
        f.params.assign(params.begin(), params.end());
        out.push_back(std::move(f));
    }
    return out;
}

}  // namespace

Appearance decodeAppearance(State& st, ByteSpan body) {
    Reader r(body);
    Appearance m;
    m.appid = r.u32();
    m.name = r.cstr();
    m.suffix = r.cstr();
    m.iconResID = r.u32();
    m.iconState = r.cstr();
    m.text = r.cstr();
    m.maptext = r.cstr();
    m.maptextW = r.u16();
    m.maptextH = r.u16();
    m.maptextX = r.s16();
    m.maptextY = r.s16();
    m.dir = r.u8();
    m.appearanceFlags = r.u32();
    for (Appearance::Slot& s : m.idSlots) {
        s.tag = r.u8();
        s.has = s.tag == 2;
        if (s.has) s.id = r.u32();
    }
    m.iconRef = r.u32();
    m.wideVerbIds = st.wideVerbIds;
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.verbs.push_back(m.wideVerbIds ? r.u32() : r.u16());
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.overlays.push_back(r.u32());
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.underlays.push_back(r.u32());
    m.layerBits = r.u32();
    m.screenLoc = r.cstr();
    m.varsMask = r.varint();
    if (m.varsMask & ~0xffffu) throw DecodeError("0x26 vars_mask has unmodelled high bits");

    for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1) {
        if (!(m.varsMask & bit)) continue;
        int w = varWidth(bit);
        if (w == 0) continue;  // bit 0x80 is a flag only: no payload, no vars entry
        size_t start = r.pos();
        if (bit == 0x800) {
            m.filters = readFilters(r);
        } else if (bit == 0x1000) {
            m.renderSource = r.cstr();
            m.renderTarget = r.cstr();
        } else {
            Reader d(r.take(size_t(w)));
            if (bit == 0x8) {
                m.hasPixelOffsets = true;
                m.pixelX = d.s16();
                m.pixelY = d.s16();
                m.pixelW = d.s16();
                m.pixelZ = d.s16();
            } else if (bit == 0x10) {
                m.hasGlideSize = true;
                m.glideSizeBits = d.u32();
            } else if (bit == 0x20) {
                m.hasTransform = true;
                for (uint32_t& t : m.transformBits) t = d.u32();
            } else if (bit == 0x40) {
                m.hasColor = true;
                for (uint8_t& c : m.color) c = d.u8();
            } else if (bit == 0x200) {
                m.hasPlane = true;
                m.plane = d.s16();
            } else if (bit == 0x4) {
                m.hasInvisibility = true;
                m.invisibility = d.u8();
            } else if (bit == 0x100) {
                m.hasBlend = true;
                m.blend = d.u8();
            } else if (bit == 0x400) {
                m.hasColorMatrix = true;
                for (uint32_t& c : m.colorMatrixBits) c = d.u32();
            }
        }
        auto data = r.since(start);
        m.vars.push_back({bit, Bytes(data.begin(), data.end())});
    }
    if (r.more()) throw DecodeError("0x26 consumed " + std::to_string(r.pos()) + " of " + std::to_string(r.size()));
    return m;
}

canon::Json toJson(const Appearance& m) {
    auto F = [](uint32_t bits) { return canon::f32(bits); };
    std::vector<canon::Json> slots, verbs, overlays, underlays, vars, filters, xf;
    for (const Appearance::Slot& s : m.idSlots)
        slots.push_back(canon::Object().boolean("has", s.has).u("id", s.id).u("tag", s.tag).build());
    for (uint32_t v : m.verbs) verbs.push_back(canon::u(v));
    for (uint32_t v : m.overlays) overlays.push_back(canon::u(v));
    for (uint32_t v : m.underlays) underlays.push_back(canon::u(v));
    for (const Appearance::Var& v : m.vars)
        vars.push_back(canon::Object().u("bit", v.bit).bytes("data", v.data).build());
    for (const Appearance::Filter& f : m.filters)
        filters.push_back(canon::Object()
                              .u("flags", f.flags)
                              .u("id", f.id)
                              .u("paramMask", f.paramMask)
                              .bytes("params", f.params)
                              .str("str", f.str)
                              .u("type", f.type)
                              .build());
    for (uint32_t t : m.transformBits) xf.push_back(F(t));
    return canon::Object()
        .u("appid", m.appid)
        .str("name", m.name)
        .str("suffix", m.suffix)
        .u("iconResID", m.iconResID)
        .str("iconState", m.iconState)
        .str("text", m.text)
        .str("maptext", m.maptext)
        .u("maptextW", m.maptextW)
        .u("maptextH", m.maptextH)
        .i("maptextX", m.maptextX)
        .i("maptextY", m.maptextY)
        .u("dir", m.dir)
        .u("appearanceFlags", m.appearanceFlags)
        .u("flags", uint8_t(m.appearanceFlags))
        .set("slots", canon::array(slots))
        .u("iconRef32", m.iconRef)
        .u("iconRef", uint16_t(m.iconRef))
        .set("verbs", canon::array(verbs))
        .set("overlays", canon::array(overlays))
        .set("underlays", canon::array(underlays))
        .u("overlayCount", m.overlays.size())
        .u("underlayCount", m.underlays.size())
        .set("layer", F(m.layerBits))
        .str("screenLoc", m.screenLoc)
        .u("varsMask", m.varsMask)
        .set("vars", canon::array(vars))
        .set("filters", canon::array(filters))
        .str("renderSource", m.renderSource)
        .str("renderTarget", m.renderTarget)
        .set("widths", canon::Object().u("id", 4).u("verb", m.wideVerbIds ? 4 : 2).build())
        .boolean("hasColor", m.hasColor)
        .u("colorR", m.color[0])
        .u("colorG", m.color[1])
        .u("colorB", m.color[2])
        .u("colorA", m.color[3])
        .i("plane", m.plane)
        .boolean("hasPlane", m.hasPlane)
        .i("pixelX", m.pixelX)
        .i("pixelY", m.pixelY)
        .i("pixelW", m.pixelW)
        .i("pixelZ", m.pixelZ)
        .boolean("hasPixelOffsets", m.hasPixelOffsets)
        .set("glideSize", F(m.glideSizeBits))
        .boolean("hasGlideSize", m.hasGlideSize)
        .set("transform", canon::array(xf))
        .boolean("hasTransform", m.hasTransform)
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::Appearance, "cpp:stock-dd-wire", jsonOf<decodeAppearance>);

}  // namespace fugue::proto

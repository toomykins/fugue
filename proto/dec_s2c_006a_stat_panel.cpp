// S2C::StatPanel: statpanel (DreamSeeker's info control). flags is a BITMASK of independent sections.
#include <cstring>

#include "proto/registry.h"

namespace fugue::proto {
namespace {

// Frame layout: [flags u8][token u8] {&1: DMValue} {&2: u16 n, n cstrings} {&4: u16} {&8: DMValue} [rows u16] x
//   {cstring, DMValue, u16 ordinal} [clears u16] x u16. DMValue kinds 1/2/3/4/6/12/15/0x2a are sized as below; any other
//   kind is NULL with zero payload bytes. Refs are u32.
//   Payload shapes: 0x01/0x04 [u32 head][u32 cid]; 0x02/0x03 [u32 head][u16][u32 cid]; 0x06 cstring; 0x0c u32;
//   0x0f u16 count + recursive values, depth cap 8; 0x2a float32 bits. A row surfaces head, cid, the 0x06 text and the
//   0x2a number. Retain the 0x02/0x03 u16 as opaque row data for conservative lifetime invalidation;
//   its semantics are not assumed. The 0x0c word and 0x0f's values are skipped. The flags&1 / flags&8 DMValues are
//   read and discarded.
StatValue value(Reader& r, int depth) {
    StatValue v;
    v.kind = r.u8();
    switch (v.kind) {
        case 0x01:
        case 0x04:
            v.head = r.u32();
            v.appid = r.u32();
            v.hasAppid = true;
            break;
        case 0x02:
        case 0x03:
            v.head = r.u32();
            v.rowWord = r.u16();
            v.appid = r.u32();
            v.hasAppid = true;
            break;
        case 0x06: v.text = r.cstr(); break;
        case 0x0c: r.u32(); break;
        case 0x0f: {
            if (depth > 8) throw DecodeError("statpanel DMValue list nested too deep");
            for (unsigned n = r.u16(), i = 0; i < n; i++) value(r, depth + 1);
            break;
        }
        case 0x2a: {
            uint32_t bits = r.u32();
            float f;
            std::memcpy(&f, &bits, 4);
            v.num = double(f);
            break;
        }
        default: break;  // NULL arm: zero payload bytes
    }
    return v;
}

}  // namespace

StatPanel decodeStatPanel(ByteSpan body) {
    Reader r(body);
    StatPanel m;
    m.flags = r.u8();
    m.token = r.u8();
    if (m.flags & 0x01) value(r, 0);
    if (m.flags & 0x02) {
        m.hasTabs = true;
        for (unsigned n = r.u16(), i = 0; i < n; i++) m.tabs.push_back(r.cstr());
    }
    if (m.flags & 0x04) {
        m.hasSelected = true;
        m.selected = r.u16();
    }
    if (m.flags & 0x08) value(r, 0);
    for (unsigned n = r.u16(), i = 0; i < n; i++) {
        StatPanel::Row row;
        row.name = r.cstr();
        row.value = value(r, 0);
        row.ordinal = r.u16();
        m.rows.push_back(std::move(row));
    }
    for (unsigned n = r.u16(), i = 0; i < n; i++) m.clears.push_back(r.u16());
    r.expectEnd();
    return m;
}

canon::Json toJson(const StatValue& v) {
    return canon::Object()
        .u("kind", v.kind)
        .str("text", v.text)
        .set("num", canon::f64FromDouble(v.num))
        .u("appid", v.appid)
        .boolean("hasAppid", v.hasAppid)
        .u("head", v.head)
        .u("rowWord", v.rowWord)
        .build();
}

canon::Json toJson(const StatPanel& m) {
    std::vector<canon::Json> tabs, rows, clears;
    for (const std::string& t : m.tabs) tabs.push_back(canon::str(t));
    for (const StatPanel::Row& row : m.rows)
        rows.push_back(
            canon::Object().str("name", row.name).set("value", toJson(row.value)).u("ordinal", row.ordinal).build());
    for (uint16_t c : m.clears) clears.push_back(canon::u(c));
    return canon::Object()
        .u("flags", m.flags)
        .u("token", m.token)
        .boolean("hasTabs", m.hasTabs)
        .set("tabs", canon::array(tabs))
        .boolean("hasSelected", m.hasSelected)
        .u("selected", m.selected)
        .set("rows", canon::array(rows))
        .set("clears", canon::array(clears))
        .build();
}

FUGUE_REGISTER_DECODER(Dir::S2C, S2C::StatPanel, "cpp:stock-dd-wire", jsonOf<decodeStatPanel>);

}  // namespace fugue::proto

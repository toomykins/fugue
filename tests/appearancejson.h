// Test support: an appearance written as the canonical JSON of proto::Appearance (proto/messages.h toJson), so a test
// can state only the fields it is about. The client itself never parses JSON.
#pragma once
#include <bit>
#include <limits>
#include <string>
#include <string_view>

#include "model/json.h"
#include "model/world.h"

namespace fugue::model {

// Replaces every field but the icon, icon_state and layer, and the opacity and luminosity (setOpaque, setLuminosity),
// which tests set on their own.
inline void setFields(Appearance& ap, std::string_view json) {
    const JVal v = parseJson(json);
    proto::Appearance a;
    a.iconResID = ap.fields.iconResID;
    a.iconState = ap.fields.iconState;
    a.layerBits = ap.fields.layerBits;
    a.appid = v["appid"].u32();
    a.name = v["name"].bytesOrStr();
    a.suffix = v["suffix"].bytesOrStr();
    a.text = v["text"].bytesOrStr();
    a.maptext = v["maptext"].bytesOrStr();
    a.maptextW = v["maptextW"].u16();
    a.maptextH = v["maptextH"].u16();
    a.maptextX = int16_t(v["maptextX"].i());
    a.maptextY = int16_t(v["maptextY"].i());
    a.dir = uint8_t(v["dir"].i());
    a.appearanceFlags = v["appearanceFlags"].u32();
    for (size_t i = 0; i < v["slots"].a.size() && i < a.idSlots.size(); i++)
        a.idSlots[i] = {uint8_t(v["slots"].a[i]["tag"].i()), v["slots"].a[i]["has"].b, v["slots"].a[i]["id"].u32()};
    a.iconRef = v["iconRef32"].u32();
    a.wideVerbIds = v["widths"]["verb"].u32() == 4;
    for (const JVal& x : v["verbs"].a) a.verbs.push_back(x.u32());
    for (const JVal& x : v["overlays"].a) a.overlays.push_back(x.u32());
    for (const JVal& x : v["underlays"].a) a.underlays.push_back(x.u32());
    a.screenLoc = v["screenLoc"].bytesOrStr();
    a.varsMask = v["varsMask"].u32();
    for (const JVal& x : v["vars"].a) {
        const auto data = hexDecode(x["data"].s);
        if (!data) continue;
        a.vars.push_back({x["bit"].u32(), *data});
        if (x["bit"].u32() == 0x4 && data->size() == 1) {
            a.hasInvisibility = true;
            a.invisibility = (*data)[0];
        } else if (x["bit"].u32() == 0x100 && data->size() == 1) {
            a.hasBlend = true;
            a.blend = (*data)[0];
        } else if (x["bit"].u32() == 0x400 && data->size() == 80) {
            a.hasColorMatrix = true;
            for (size_t i = 0; i < 20; i++) a.colorMatrixBits[i] = le32(&(*data)[i * 4]);
        }
    }
    for (const JVal& x : v["filters"].a) {
        proto::Appearance::Filter f;
        f.type = uint8_t(x["type"].i());
        f.flags = uint8_t(x["flags"].i());
        f.id = x["id"].u32();
        f.paramMask = x["paramMask"].u32();
        f.str = x["str"].bytesOrStr();
        if (x["params"].kind == JVal::Str) {
            if (const auto data = hexDecode(x["params"].s)) f.params = *data;
        } else {
            const std::string raw = x["params"].bytesOrStr();
            f.params.assign(raw.begin(), raw.end());
        }
        a.filters.push_back(std::move(f));
    }
    a.renderSource = v["renderSource"].bytesOrStr();
    a.renderTarget = v["renderTarget"].bytesOrStr();
    a.hasColor = v["hasColor"].b;
    const char* color[] = {"colorR", "colorG", "colorB", "colorA"};
    for (size_t i = 0; i < 4; i++) a.color[i] = uint8_t(v[color[i]].i());
    a.hasPlane = v["hasPlane"].b;
    a.plane = int16_t(v["plane"].i());
    a.pixelX = int16_t(v["pixelX"].i());
    a.pixelY = int16_t(v["pixelY"].i());
    a.pixelW = int16_t(v["pixelW"].i());
    a.pixelZ = int16_t(v["pixelZ"].i());
    a.hasPixelOffsets = v["hasPixelOffsets"].b;
    a.hasGlideSize = v["hasGlideSize"].b;
    a.glideSizeBits = v["glideSize"].f32bits();
    a.hasTransform = v["hasTransform"].b;
    // A matrix that is not six cells cannot be sent; it stands for one the renderer must refuse.
    const bool six = v["transform"].a.size() == 6;
    for (size_t i = 0; i < 6; i++)
        a.transformBits[i] = six ? v["transform"].a[i].f32bits()
                                 : std::bit_cast<uint32_t>(std::numeric_limits<float>::quiet_NaN());
    a.appearanceFlags = (a.appearanceFlags & ~1u) | (ap.fields.appearanceFlags & 1u);
    std::erase_if(a.vars, [](const proto::Appearance::Var& x) { return x.bit == 2; });
    for (const proto::Appearance::Var& x : ap.fields.vars)
        if (x.bit == 2) a.vars.push_back(x);
    ap.fields = std::move(a);
}

inline proto::Appearance appearanceOf(std::string_view json) {
    Appearance ap;
    setFields(ap, json);
    return ap.fields;
}

inline void setOpaque(Appearance& ap, bool opaque) {
    ap.fields.appearanceFlags = (ap.fields.appearanceFlags & ~1u) | (opaque ? 1u : 0u);
}

// -1 leaves a payload the model does not read as a luminosity.
inline void setLuminosity(Appearance& ap, int luminosity) {
    std::erase_if(ap.fields.vars, [](const proto::Appearance::Var& x) { return x.bit == 2; });
    if (luminosity < 0)
        ap.fields.vars.push_back({2, Bytes{0, 0}});
    else if (luminosity > 0)
        ap.fields.vars.push_back({2, Bytes{uint8_t(luminosity)}});
}

inline proto::StatValue statValue(std::string_view json) {
    const JVal v = parseJson(json);
    proto::StatValue s;
    s.kind = uint8_t(v["kind"].i());
    s.text = v["text"].bytesOrStr();
    s.hasAppid = v["hasAppid"].b;
    s.appid = v["appid"].u32();
    s.head = v["head"].u32();
    s.rowWord = v["rowWord"].u16();
    return s;
}

inline std::string fieldsJson(const Appearance& ap) { return proto::toJson(ap.fields); }

}  // namespace fugue::model

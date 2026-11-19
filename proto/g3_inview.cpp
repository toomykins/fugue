// The object arrays, in wire order. Each array is [count u8] then count records, and every record starts with the
// record head [id u32][key u16]:
//   1 new          head
//   2 pos          head [x u16][y u16][z u16]
//   3 bind         head [other u32]
//   4 dir          head [dir u8]
//   5 appearance   head [appid u32]
//   6 pixel        head [x s16][y s16][w s16][z s16]
//   7 bounds       head [x s16][y s16][w s16][h s16]
//   8 step         head [x s16][y s16][step_size f32]
//   9 visContents  head [n u16] n x [type u8][value u32]
//  10 particles    head [particleID u32]; unless the id is 0xffff, [len u16] a fixed block of len bytes, then
//                  generators [key u16][type u8] (type 0xff = no payload, else [len u16][len bytes]) up to key 0 or the
//                  end of the body
// then an optional [tail u8]. Arrays 6-10 and the tail are each read only while bytes remain.
#include "proto/g3_inview.h"

#include <cstring>

namespace fugue::proto {

namespace g3 {

namespace {

RecordRef head(Reader& r) {
    RecordRef h;
    h.id = r.u32();
    h.key = r.u16();
    return h;
}

// [count u8] then count records, each read by f after its head.
template <class T, class F>
void counted(Reader& r, std::vector<T>& out, F f) {
    for (unsigned n = r.u8(); n > 0; n--) {
        T rec;
        rec.rec = head(r);
        f(rec);
        out.push_back(std::move(rec));
    }
}

float f32(Reader& r) {
    uint32_t u = r.u32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

ParticleGenerator readGenerator(Reader& r, uint8_t type);

// [type u8], then unless 0xff [len u16] and a payload of len bytes.
ParticleGenerator readNestedGenerator(Reader& r) {
    uint8_t type = r.u8();
    if (type == 0xff) return {};
    Reader payload(r.take(r.u16()));
    return readGenerator(payload, type);
}

// One generator payload (the bytes after [type u8][len u16]); bytes after the body are skipped.
ParticleGenerator readGenerator(Reader& r, uint8_t type) {
    ParticleGenerator g;
    g.type = type;
    g.rand = r.u8();
    const bool constant = type & 0x80;
    auto floats = [&](std::vector<float>& v, int n) {
        for (int i = 0; i < n; i++) v.push_back(f32(r));
    };
    switch (type & 0x7f) {
        case 0:
            floats(g.a, 1);
            if (!constant) floats(g.b, 1);
            break;
        case 2:
            g.colors.push_back(r.u32());
            if (!constant) g.colors.push_back(r.u32());
            break;
        case 0x21:
            g.sub = r.u16();
            floats(g.a, 1);
            if (!constant) floats(g.b, 1);
            break;
        case 1:
        case 0x20:
            floats(g.a, 3);
            if (!constant) floats(g.b, 3);
            break;
        case 3:
            floats(g.a, 20);
            if (!constant) floats(g.b, 20);
            break;
        case 4:
            g.sub = r.u16();
            g.kids.push_back(readNestedGenerator(r));
            g.kids.push_back(readNestedGenerator(r));
            break;
        case 5:
            for (unsigned n = r.u16(); n > 0; n--) {
                uint32_t id = r.u32();
                g.ids.push_back({id, f32(r)});
            }
            break;
        case 6:
            for (unsigned n = r.u16(); n > 0; n--) {
                std::string s = r.cstr();
                g.strs.push_back({std::move(s), f32(r)});
            }
            break;
        default: return {};  // an unknown kind: stock skips its payload and keeps no generator
    }
    return g;
}

// The fixed block and generators of one particle record; nullopt when either does not parse.
std::optional<ParticleDef> particleDef(ByteSpan fixed, const std::vector<ObjectArrays::ParticleGen>& gens) {
    ParticleDef d;
    try {
        Reader r(fixed);
        d.width = r.u16();
        d.height = r.u16();
        d.count = r.u16();
        d.spawning = f32(r);
        for (float& v : d.bound1) v = f32(r);
        for (float& v : d.bound2) v = f32(r);
        for (float& v : d.gravity) v = f32(r);
        for (unsigned n = r.u8(); n > 0; n--) d.transform.push_back(f32(r));
        if (unsigned stops = r.u16()) {
            for (; stops > 0; stops--) {
                float pos = f32(r);
                d.gradient.push_back({pos, r.u32()});
            }
            d.gradientLoop = r.u8() != 0;
            d.gradientSpace = r.u8();
        }
        for (const ObjectArrays::ParticleGen& g : gens) {
            Reader payload(g.payload);
            d.gens.push_back({g.key, g.type == 0xff ? ParticleGenerator{} : readGenerator(payload, g.type)});
        }
    } catch (const DecodeError&) { return std::nullopt; }
    return d;
}

void particles(Reader& r, std::vector<ObjectArrays::Particles>& out) {
    counted(r, out, [&](ObjectArrays::Particles& p) {
        p.particleID = r.u32();
        if (p.particleID == 0xffff) return;
        auto fixed = r.take(r.u16());
        p.fixed.assign(fixed.begin(), fixed.end());
        // The generator stream ends at key 0 OR at the end of the body; a body that simply stops is accepted.
        while (r.more()) {
            ObjectArrays::ParticleGen g;
            g.key = r.u16();
            if (g.key == 0) break;
            g.type = r.u8();
            if (g.type != 0xff) {
                auto payload = r.take(r.u16());
                g.payload.assign(payload.begin(), payload.end());
            }
            p.gens.push_back(std::move(g));
        }
        p.def = particleDef(p.fixed, p.gens);
    });
}

}  // namespace

// DMValue is a shared self-delimiting reader: kinds 1/2/3/4/6/12/15/0x2a are sized as below, anything
// else is NULL with zero payload bytes; refs are u32.
// Widths: 1: u32+ref, 2/3: ref+u16+ref, 4: ref+ref, 6: cstring, 0xc/0x2a: u32, 0xf: u16 count + nested.
ByteSpan readDmValue(Reader& r) {
    size_t start = r.pos();
    switch (r.u8()) {
        case 1:
        case 4: r.take(8); break;
        case 2:
        case 3: r.take(10); break;
        case 6: r.cstr(); break;
        case 0xc:
        case 0x2a: r.u32(); break;
        case 0xf:
            for (unsigned n = r.u16(); n > 0; n--) readDmValue(r);
            break;
        default: break;
    }
    return r.since(start);
}

ObjectArrays readObjectArrays(Reader& r) {
    ObjectArrays a;
    for (unsigned n = r.u8(); n > 0; n--) a.created.push_back(head(r));
    counted(r, a.pos, [&](ObjectArrays::Pos& p) {
        p.x = r.u16();
        p.y = r.u16();
        p.z = r.u16();
    });
    counted(r, a.bind, [&](ObjectArrays::Bind& b) { b.other = r.u32(); });
    counted(r, a.dir, [&](ObjectArrays::Dir& d) { d.dir = r.u8(); });
    counted(r, a.appearance, [&](ObjectArrays::AppearanceBind& b) { b.appearance = r.u32(); });
    if (r.more())
        counted(r, a.pixel, [&](ObjectArrays::Pixel& p) {
            p.x = r.s16();
            p.y = r.s16();
            p.w = r.s16();
            p.z = r.s16();
        });
    if (r.more())
        counted(r, a.bounds, [&](ObjectArrays::Bounds& b) {
            b.x = r.s16();
            b.y = r.s16();
            b.w = r.s16();
            b.h = r.s16();
        });
    if (r.more())
        counted(r, a.step, [&](ObjectArrays::Step& s) {
            s.x = r.s16();
            s.y = r.s16();
            s.sizeBits = r.u32();
        });
    if (r.more())
        counted(r, a.visContents, [&](ObjectArrays::VisContents& v) {
            for (unsigned k = r.u16(); k > 0; k--) {
                VisItem it;
                it.type = r.u8();
                it.value = r.u32();
                v.items.push_back(it);
            }
        });
    if (r.more()) particles(r, a.particles);
    if (r.more()) {
        a.hasTail = true;
        a.tail = r.u8();
    }
    return a;
}

}  // namespace g3

namespace {

canon::Json recJson(const RecordRef& h) { return canon::Object().u("id", h.id).u("key", h.key).build(); }

// Every record but array 1's carries its head nested under "inViewRec".
template <class T, class F>
canon::Json records(const std::vector<T>& v, F fields) {
    std::vector<canon::Json> out;
    out.reserve(v.size());
    for (const T& rec : v) {
        canon::Object o;
        o.set("inViewRec", recJson(rec.rec));
        fields(o, rec);
        out.push_back(o.build());
    }
    return canon::array(out);
}

}  // namespace

void addJson(canon::Object& o, const ObjectArrays& a) {
    using A = ObjectArrays;
    std::vector<canon::Json> created;
    for (const RecordRef& h : a.created) created.push_back(recJson(h));
    o.set("new", canon::array(created));
    o.set("pos", records(a.pos, [](canon::Object& r, const A::Pos& p) { r.u("x", p.x).u("y", p.y).u("z", p.z); }));
    o.set("bind", records(a.bind, [](canon::Object& r, const A::Bind& b) { r.u("other", b.other); }));
    o.set("dir", records(a.dir, [](canon::Object& r, const A::Dir& d) { r.u("dir", d.dir); }));
    o.set("appearance",
          records(a.appearance, [](canon::Object& r, const A::AppearanceBind& b) { r.u("appearance", b.appearance); }));
    o.set("pixel", records(a.pixel, [](canon::Object& r, const A::Pixel& p) {
              r.i("pixelX", p.x).i("pixelY", p.y).i("pixelW", p.w).i("pixelZ", p.z);
          }));
    o.set("bounds", records(a.bounds, [](canon::Object& r, const A::Bounds& b) {
              r.i("boundX", b.x).i("boundY", b.y).i("boundW", b.w).i("boundH", b.h);
          }));
    o.set("step", records(a.step, [](canon::Object& r, const A::Step& s) {
              r.i("stepX", s.x).i("stepY", s.y).u("extra", s.sizeBits);
          }));
    o.set("visContents", records(a.visContents, [](canon::Object& r, const A::VisContents& v) {
              std::vector<canon::Json> items;
              for (const VisItem& it : v.items)
                  items.push_back(canon::Object().u("type", it.type).u("value", it.value).build());
              r.set("items", canon::array(items));
          }));
    o.set("particles", records(a.particles, [](canon::Object& r, const A::Particles& p) {
              std::vector<canon::Json> gens;
              for (const A::ParticleGen& g : p.gens)
                  gens.push_back(canon::Object().u("key", g.key).u("type", g.type).bytes("payload", g.payload).build());
              r.u("particleID", p.particleID).bytes("fixed", p.fixed).set("gens", canon::array(gens));
          }));
    o.u("tail", a.tail).boolean("hasTail", a.hasTail);
}

}  // namespace fugue::proto

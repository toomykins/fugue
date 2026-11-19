#include "model/particles.h"

#include <algorithm>
#include <cmath>

namespace fugue::model {

namespace {

float randFloat(std::mt19937& rng) { return float(rng() >> 8) * (1.0f / 16777216.0f); }

// The three non-uniform distributions, each mapping a uniform u in [0,1) to
// the fraction of the way from lo to hi.
float normalRand(float u) {
    if (u <= 0) return 0;
    if (u >= 1) return 1;
    float sign = u >= 0.5f ? 1.0f : -1.0f;
    // Winitzki's inverse erf (a = 0.147), times sqrt(2): the normal quantile, squashed back into (0,1) by the logistic.
    float l = float(std::log(double((1 - u) * 4 * u)));
    float f = l * 0.5f + 4.3307467f;
    float s = float(std::sqrt(double(f * f - l * 6.802721f)));
    float z = float(std::sqrt(double((s - f) + (s - f)))) * sign;
    float e = float(std::exp(double(z)));
    return e / (e + 1);
}
float linearRand(float u, float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    float span = hi - lo;
    if (span < 0.0001f) return u;
    if (lo < 0) {
        if (hi > 0) {
            float w = lo * lo / (hi * hi + lo * lo);
            if (w <= u) {
                u = (u - w) / (1 - w);
                lo = 0;
                span = hi;
                if (hi < 0.0001f) return u;
                return (float(std::sqrt(double(hi * hi * u))) - lo) / span;
            }
            return 1 - linearRand(1 - u / w, 0, -lo);
        }
        return 1 - linearRand(1 - u, -hi, -lo);
    }
    return (float(std::sqrt(double((hi * hi - lo * lo) * u + lo * lo))) - lo) / span;
}
float squareRand(float u, float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    if (hi - lo < 0.0001f) return u;
    float lo3 = 0, hi3 = hi * hi * hi;
    if (lo >= 0) {
        lo3 = lo * lo * lo;
    } else {
        if (hi <= 0) return 1 - squareRand(1 - u, -hi, -lo);
        float neg = lo * lo * lo;
        float w = neg / (hi3 + neg);
        if (u < w) return 1 - squareRand(1 - u / w, 0, -lo);
        lo = 0;
        u = (u - w) / (1 - w);
        if (hi < 0.0001f) return u;
    }
    return (float(std::cbrt(double((hi3 - lo3) * u + lo3))) - lo) / (hi - lo);
}
float distribute(uint8_t rand, float u, float lo, float hi) {
    switch (rand) {
        case 1: return normalRand(u);
        case 2: return linearRand(u, lo, hi);
        case 3: return squareRand(u, lo, hi);
        default: return u;
    }
}

float maxAbs3(const std::vector<float>& v) {
    float m = 0;
    for (size_t i = 0; i < 3 && i < v.size(); i++) m = std::max(m, std::fabs(v[i]));
    return m;
}
float at(const std::vector<float>& v, size_t i) { return i < v.size() ? v[i] : 0.0f; }

uint32_t packRgba(float r, float g, float b, float a) {
    auto c = [](float v) {
        int x = int(v * 255.0f + 0.5f);
        return uint32_t(std::clamp(x, 0, 255));
    };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
uint32_t lerpColor(uint32_t x, uint32_t y, float t) {
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float a = float(x >> sh & 0xff), b = float(y >> sh & 0xff);
        int v = int(a + (b - a) * t + 0.5f);
        out |= uint32_t(std::clamp(v, 0, 255)) << sh;
    }
    return out;
}
// "#rgb", "#rrggbb" or "#rrggbbaa"; 0 when it is none of those.
uint32_t parseColor(const std::string& s) {
    if (s.empty() || s[0] != '#') return 0;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = char(c | 32);
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::vector<int> d;
    for (size_t i = 1; i < s.size(); i++) {
        int n = nib(s[i]);
        if (n < 0) return 0;
        d.push_back(n);
    }
    uint32_t r, g, b, a = 255;
    if (d.size() == 3) {
        r = uint32_t(d[0] * 17);
        g = uint32_t(d[1] * 17);
        b = uint32_t(d[2] * 17);
    } else if (d.size() == 6 || d.size() == 8) {
        r = uint32_t(d[0] << 4 | d[1]);
        g = uint32_t(d[2] << 4 | d[3]);
        b = uint32_t(d[4] << 4 | d[5]);
        if (d.size() == 8) a = uint32_t(d[6] << 4 | d[7]);
    } else
        return 0;
    return r | g << 8 | b << 16 | a << 24;
}

template <class T>
const T* weighted(const std::vector<std::pair<T, float>>& v, std::mt19937& rng) {
    float sum = 0;
    for (const auto& e : v) sum += e.second;
    float r = randFloat(rng) * sum;
    for (const auto& e : v) {
        r -= e.second;
        if (r < 0) return &e.first;
    }
    return nullptr;
}

// An operation generator (type 4) is not modelled beyond its first operand.
const ParticleGen* opOperand(const ParticleGen& g) { return g.kids.empty() ? nullptr : &g.kids[0]; }

}  // namespace

int particleSlotForKey(uint16_t key) {
    switch (key) {
        case 0x145: return PLifespan;
        case 0x146: return PFade;
        case 0x15a: return PFadein;
        case 0x147: return PPosition;
        case 0x148: return PVelocity;
        case 0xf2: return PColor;
        case 0x14a: return PColorChange;
        case 0x131: return PUnknown131;
        case 0x14b: return PFriction;
        case 0x14c: return PDrift;
        case 0x14d: return PScale;
        case 0x159: return PGrow;
        case 0x14e: return PRotation;
        case 0x14f: return PSpin;
        case 0x2b: return PIcon;
        case 0x2c: return PIconState;
        default: return -1;
    }
}

void applyParticleBlock(ParticleSet& s, const proto::ParticleDef& d) {
    s.width = d.width;
    s.height = d.height;
    s.count = d.count;
    s.spawning = d.spawning;
    s.bound1 = d.bound1;
    s.bound2 = d.bound2;
    s.gravity = d.gravity;
    const size_t n = d.transform.size();
    s.hasTransform = n > 0;
    s.transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (n > 0) {
        size_t perRow = n < 7 ? 2 : (n > 12 ? 4 : 3), k = 0;
        for (size_t row = 0; row < 4 && k < n; row++)
            for (size_t c = 0; c < perRow && k < n; c++) s.transform[row * 4 + c] = d.transform[k++];
    }
    s.gradient.clear();
    for (const proto::ParticleDef::Stop& st : d.gradient) s.gradient.push_back({st.pos, st.rgba});
    if (!d.gradient.empty()) {
        std::stable_sort(s.gradient.begin(), s.gradient.end(),
                         [](const ParticleSet::Stop& x, const ParticleSet::Stop& y) { return x.pos < y.pos; });
        s.gradientLoop = d.gradientLoop;
        s.gradientSpace = d.gradientSpace;
    }
    for (const auto& [key, gen] : d.gens) {
        int slot = particleSlotForKey(key);
        if (slot < 0) continue;
        ParticleGen g = gen;
        // icon must be an icon list and icon_state a string list; anything else leaves the slot empty
        if (slot == PIcon && (g.type & 0x7f) != 5) g = ParticleGen{};
        if (slot == PIconState && (g.type & 0x7f) != 6) g = ParticleGen{};
        s.gen[size_t(slot)] = std::move(g);
    }
}

// ---- sampling (number, vector, colour) -------------------------------------------------------------------------------

float particleGenNum(const ParticleGen& g, std::mt19937& rng) {
    float lo = 0, hi = 1;
    switch (g.type) {
        case 0x80: return at(g.a, 0);
        case 0x81: return maxAbs3(g.a);
        case 0:
        case 0x21:
            lo = at(g.a, 0);
            hi = at(g.b, 0);
            break;
        case 1:
        case 0x20:
            lo = maxAbs3(g.a);
            hi = maxAbs3(g.b);
            break;
        case 4:
        case 0x84: {
            const ParticleGen* k = opOperand(g);
            return k ? particleGenNum(*k, rng) : 0.0f;
        }
        default: break;
    }
    float u = distribute(g.rand, randFloat(rng), lo, hi);
    return (hi - lo) * u + lo;
}

std::array<float, 3> particleGenVec(const ParticleGen& g, std::mt19937& rng) {
    switch (g.type) {
        case 0: {
            float x = particleGenNum(g, rng), y = particleGenNum(g, rng), z = particleGenNum(g, rng);
            return {x, y, z};
        }
        case 0x80: {
            float v = at(g.a, 0);
            return {v, v, v};
        }
        case 0x81: return {at(g.a, 0), at(g.a, 1), at(g.a, 2)};
        case 1: {
            float u = randFloat(rng);
            u = distribute(g.rand, u, maxAbs3(g.a), maxAbs3(g.b));
            return {at(g.a, 0) + (at(g.b, 0) - at(g.a, 0)) * u, at(g.a, 1) + (at(g.b, 1) - at(g.a, 1)) * u,
                    at(g.a, 2) + (at(g.b, 2) - at(g.a, 2)) * u};
        }
        case 0x20: {
            std::array<float, 3> out{};
            for (size_t i = 0; i < 3; i++) {
                float lo = at(g.a, i), hi = at(g.b, i);
                float u = lo == hi ? 0.0f : randFloat(rng);
                if (lo != hi) u = distribute(g.rand, u, lo, hi);
                out[i] = (hi - lo) * u + lo;
            }
            return out;
        }
        case 0x21: {
            float r = at(g.a, 0);
            if (at(g.a, 0) != at(g.b, 0)) {
                float lo = at(g.a, 0), hi = at(g.b, 0);
                r = (hi - lo) * distribute(g.rand, randFloat(rng), lo, hi) + lo;
            }
            switch (g.sub) {
                case 1: {  // sphere: a point in the unit ball, pushed out to radius r
                    float x = randFloat(rng) * 2 - 1, y = randFloat(rng) * 2 - 1, z, len;
                    for (;;) {
                        z = randFloat(rng) * 2 - 1;
                        len = std::sqrt(x * x + y * y + z * z);
                        if (len >= 0.0001f && len <= 1) break;
                        x = y;
                        y = z;
                    }
                    return {x * r / len, y * r / len, z * r / len};
                }
                case 2: {  // square: a point on the perimeter of the square of half-size r
                    float s = randFloat(rng) * 8, x, y;
                    if (s < 2) {
                        x = s - 1;
                        y = -1;
                    } else if (s < 4) {
                        x = 3 - s;
                        y = 1;
                    } else if (s < 6) {
                        x = 1;
                        y = s - 5;
                    } else {
                        x = -1;
                        y = 7 - s;
                    }
                    return {x * r, y * r, 0};
                }
                case 3: {  // cube: a point on the surface of the cube of half-size r
                    float s = randFloat(rng) * 12, w = randFloat(rng) * 2 - 1, x, y, z;
                    if (s <= 8) {
                        if (s <= 4) {
                            if (s <= 2) {
                                z = 1;
                                x = s - 1;
                                y = w;
                            } else {
                                z = -1;
                                x = 3 - s;
                                y = -w;
                            }
                        } else if (s <= 6) {
                            x = s - 5;
                            z = w;
                            y = 1;
                        } else {
                            x = 7 - s;
                            z = -w;
                            y = -1;
                        }
                    } else if (s <= 10) {
                        x = 1;
                        z = s - 9;
                        y = w;
                    } else {
                        x = -1;
                        z = 11 - s;
                        y = -w;
                    }
                    return {x * r, y * r, z * r};
                }
                default: {  // circle
                    float x = randFloat(rng) * 2 - 1, y, len;
                    for (;;) {
                        y = randFloat(rng) * 2 - 1;
                        len = std::sqrt(x * x + y * y);
                        if (len >= 0.0001f && len <= 1) break;
                        x = y;
                    }
                    return {x * r / len, y * r / len, 0};
                }
            }
        }
        case 2:
        case 6:
        case 0x82: {
            uint32_t c = particleGenColor(g, rng);
            return {float(c & 0xff) / 255.0f, float(c >> 8 & 0xff) / 255.0f, float(c >> 16 & 0xff) / 255.0f};
        }
        case 4:
        case 0x84: {
            const ParticleGen* k = opOperand(g);
            return k ? particleGenVec(*k, rng) : std::array<float, 3>{};
        }
        default: return {};
    }
}

uint32_t particleGenColor(const ParticleGen& g, std::mt19937& rng) {
    uint8_t kind = g.type & 0x7f;
    if (kind == 0x20 || kind == 0x21) kind = 1;
    switch (kind) {
        case 0: {
            float v = particleGenNum(g, rng);
            return packRgba(v, v, v, 1);
        }
        case 1: {
            auto v = particleGenVec(g, rng);
            return packRgba(v[0], v[1], v[2], 1);
        }
        case 2: {
            if (g.colors.empty()) return 0;
            if (g.type & 0x80 || g.colors.size() < 2) return g.colors[0];
            float u = randFloat(rng);
            if (g.rand >= 1 && g.rand <= 3) u = distribute(g.rand, u, 0, 1);
            return lerpColor(g.colors[0], g.colors[1], u);
        }
        case 3: {  // a colour matrix: each channel is the column sum of the sampled 5x4 matrix
            std::vector<float> m = g.a;
            if (!(g.type & 0x80) && g.b.size() == m.size()) {
                float u = randFloat(rng);
                for (size_t i = 0; i < m.size(); i++) m[i] += (g.b[i] - m[i]) * u;
            }
            float ch[4] = {};
            for (size_t c = 0; c < 4; c++)
                for (size_t row = 0; row < 5; row++) ch[c] += at(m, row * 4 + c);
            return packRgba(ch[0], ch[1], ch[2], ch[3]);
        }
        case 4: {
            const ParticleGen* k = opOperand(g);
            return k ? particleGenColor(*k, rng) : 0;
        }
        case 6: {
            const std::string* s = weighted(g.strs, rng);
            return s ? parseColor(*s) : 0;
        }
        default: return 0;
    }
}

// ColorGradient::Interpolate: a looping gradient wraps x into [min(first, 0), max(last, 1)); stops are blended in RGBA.
uint32_t particleGradient(const ParticleSet& s, float x) {
    const auto& g = s.gradient;
    if (g.empty()) return 0;
    if (g.size() == 1) return g[0].rgba;
    if (s.gradientLoop) {
        float lo = std::min(g.front().pos, 0.0f), hi = std::max(g.back().pos, 1.0f);
        if (x < lo || x >= hi) x = lo + std::fmod(x - lo, hi - lo);
    }
    if (x <= g.front().pos) return g.front().rgba;
    if (x >= g.back().pos) return g.back().rgba;
    for (size_t i = 0; i + 1 < g.size(); i++) {
        if (x > g[i + 1].pos) continue;
        float span = g[i + 1].pos - g[i].pos;
        if (span <= 0) return g[i + 1].rgba;
        return lerpColor(g[i].rgba, g[i + 1].rgba, (x - g[i].pos) / span);
    }
    return g.back().rgba;
}

// ---- simulation ------------------------------------------------------------------------------------------------------

namespace {

constexpr double kMaxCatchUpTicks = 600;  // a set not drawn for longer restarts from this far back

// A new particle, born `age` ticks before now.
void spawn(ParticleSet& s, Particle& p, double now, double age) {
    auto& g = s.gen;
    auto& rng = s.rng;
    p = Particle{};
    p.alive = true;
    p.born = p.last = now - age;
    p.life = g[PLifespan].present() ? particleGenNum(g[PLifespan], rng) : float(s.count) / s.spawning;
    p.fade = g[PFade].present() ? particleGenNum(g[PFade], rng) : 0.0f;
    p.fadein = g[PFadein].present() ? particleGenNum(g[PFadein], rng) : 0.0f;
    if (g[PPosition].present()) p.pos = particleGenVec(g[PPosition], rng);
    if (g[PVelocity].present()) p.vel = particleGenVec(g[PVelocity], rng);
    if (s.gradient.empty()) {
        p.color = g[PColor].present() ? particleGenColor(g[PColor], rng) : 0xffffffffu;
    } else {
        p.colorVal = g[PColor].present() ? particleGenNum(g[PColor], rng) : 0.0f;
        p.colorChange = g[PColorChange].present() ? particleGenNum(g[PColorChange], rng) : 0.0f;
    }
    if (g[PUnknown131].present()) p.unknown131 = particleGenNum(g[PUnknown131], rng);
    if (g[PFriction].present()) p.friction = std::clamp(particleGenNum(g[PFriction], rng), 0.0f, 1.0f);
    if (g[PScale].present()) {
        auto v = particleGenVec(g[PScale], rng);
        p.scale = {v[0], v[1]};
    }
    if (g[PGrow].present()) {
        auto v = particleGenVec(g[PGrow], rng);
        p.grow = {v[0], v[1]};
    }
    if (g[PRotation].present()) p.rotation = particleGenNum(g[PRotation], rng);
    if (g[PSpin].present()) p.spin = particleGenNum(g[PSpin], rng);
    if (g[PIcon].present()) {
        const std::string* st = g[PIconState].present() ? weighted(g[PIconState].strs, rng) : nullptr;
        if (st) p.iconState = *st;
        const uint32_t* id = weighted(g[PIcon].ids, rng);
        p.icon = id ? *id : 0;
    }
}

// Ages one particle to now. false = it died (age past its life, a scale at or below 0, or outside the bounds).
bool step(ParticleSet& s, Particle& p, double now) {
    double age = now - p.born;
    if (age > double(p.life)) return false;
    float alpha = 1;
    if (p.fade > 0) alpha = std::clamp(float((double(p.life) - age) / double(p.fade)), 0.0f, 1.0f);
    if (p.fadein > 0) alpha *= std::clamp(float(age / double(p.fadein)), 0.0f, 1.0f);
    float dt = float(now - p.last);
    uint32_t c = p.color;
    if (!s.gradient.empty()) {
        p.colorVal += dt * p.colorChange;
        c = particleGradient(s, p.colorVal);
    }
    if (alpha < 0.9980392f) {
        int a8 = std::clamp(int(alpha * 255.0f + 0.5f), 0, 255);
        uint32_t ca = c >> 24;
        c = (c & 0xffffffu) | ((ca * uint32_t(a8) + 127) / 255) << 24;
    }
    p.drawColor = c;
    const auto& gr = s.gravity;
    const ParticleGen& drift = s.gen[PDrift];
    if (!drift.present() && p.friction == 0) {
        for (size_t i = 0; i < 3; i++) {
            float dv = (p.acc[i] + gr[i]) * dt;
            float v0 = p.vel[i];
            p.vel[i] = v0 + dv;
            p.pos[i] += (dv * 0.5f + v0) * dt;
        }
    } else {
        // Sub-steps of at most one tick (half the span while more than four are left).
        for (float t = dt; t > 0;) {
            float h = t > 4 ? t * 0.5f : std::min(t, 1.0f);
            if (p.friction != 0) {
                float k = float(std::pow(double(1 - p.friction), double(h)));
                for (size_t i = 0; i < 3; i++) {
                    p.vel[i] *= k;
                    p.acc[i] *= k;
                }
            }
            if (drift.present()) {
                auto d = particleGenVec(drift, s.rng);
                for (size_t i = 0; i < 3; i++) p.acc[i] += h * d[i];
            }
            for (size_t i = 0; i < 3; i++) {
                float dv = (p.acc[i] + gr[i]) * h;
                float v0 = p.vel[i];
                p.vel[i] = v0 + dv;
                p.pos[i] += (dv * 0.5f + v0) * h;
            }
            t -= h;
        }
    }
    if (p.icon != 0xffff) {
        p.rotation += dt * p.spin;
        p.scale[0] += dt * p.grow[0];
        p.scale[1] += dt * p.grow[1];
        if (p.scale[0] <= 0 || p.scale[1] <= 0) return false;
        double rad = double(p.rotation) * 0.017453292519943295;
        float sn = float(std::sin(rad)), cs = float(std::cos(rad));
        // clockwise on screen (+y down): x' = sx (u cos - v sin), y' = sy (u sin + v cos)
        p.m = {p.scale[0] * cs, -p.scale[0] * sn, p.scale[1] * sn, p.scale[1] * cs};
    }
    for (size_t i = 0; i < 3; i++)
        if (!(s.bound1[i] <= p.pos[i] && p.pos[i] <= s.bound2[i])) return false;
    p.last = now;
    return true;
}

}  // namespace

// One step of a set. The spawn count of a step is the fractional carry plus spawning x elapsed ticks; the step's spawns are
// spread evenly back over its span (the first one oldest), each aged from its own birth to now. Spawns stop at `count`
// live particles; what could not spawn then is dropped, keeping only the fraction.
void advanceParticles(ParticleSet& s, int64_t clockNs, int64_t frameNs, uint32_t seed) {
    if (frameNs <= 0) frameNs = 100'000'000;
    int64_t q = clockNs - ((clockNs % frameNs) + frameNs) % frameNs;
    const double frameTicks = double(frameNs) / 1e8;
    if (!s.started) {
        s.started = true;
        s.rng.seed(seed);
        s.epochNs = q;
        s.last = -frameTicks;  // the first step covers one frame
    }
    double now = double(q - s.epochNs) / 1e8;
    if (now < s.last) s.last = now - frameTicks;
    if (now == s.last) return;
    if (now - s.last > kMaxCatchUpTicks) s.last = now - kMaxCatchUpTicks;
    double elapsed = now - s.last;
    s.acc += elapsed * double(s.spawning);
    if (std::floor(s.acc + 0.0001) != std::floor(s.acc)) s.acc = std::floor(s.acc + 0.0001);
    double n = std::max(1.0, std::floor(s.acc));
    double per = elapsed / n;
    size_t keep = 0;
    for (size_t i = 0; i < s.live.size(); i++) {
        if (!step(s, s.live[i], now)) continue;
        if (keep != i) s.live[keep] = std::move(s.live[i]);
        keep++;
    }
    s.live.resize(keep);
    // Bounded: a set whose spawns all die at once would otherwise loop acc times.
    for (int guard = 0; s.acc >= 1 && s.live.size() < s.count && guard < 100000; guard++) {
        s.acc -= 1;
        Particle p;
        spawn(s, p, now, per * s.acc);
        if (step(s, p, now)) s.live.push_back(std::move(p));
    }
    if (s.acc >= 1) s.acc -= std::floor(s.acc);
    s.last = now;
}

}  // namespace fugue::model

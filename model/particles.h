// BYOND particle sets (an obj's or mob's `particles`): the definition as the WorldState/InView particle array sends it,
// and the client-side simulation stock DreamSeeker 516 runs on it. Times are in ticks of 0.1 s, the particle unit;
// stock advances a set on its client frame clock (1 / client.fps, else the world tick), so the simulation is stepped on
// that clock and is still between two frames.
#pragma once
#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "proto/messages.h"

namespace fugue::model {

// One generator, as decoded (proto/messages.h).
using ParticleGen = proto::ParticleGenerator;

// The generator slots, in the order of their wire keys.
enum PSlot {
    PLifespan,
    PFade,
    PFadein,
    PPosition,
    PVelocity,
    PColor,
    PColorChange,
    PUnknown131,
    PFriction,
    PDrift,
    PScale,
    PGrow,
    PRotation,
    PSpin,
    PIcon,
    PIconState,
    PSlotCount
};
// The wire key of a slot (0x145 lifespan ... 0x2b icon, 0x2c icon_state); -1 for an unknown key.
int particleSlotForKey(uint16_t key);

struct Particle {
    float life = 0, fade = 0, fadein = 0;
    std::array<float, 3> pos{}, vel{}, acc{};
    uint32_t color = 0xffffffffu;  // RGBA packed little-endian (r in the low byte)
    float colorVal = 0, colorChange = 0, unknown131 = 0, friction = 0;
    std::array<float, 2> scale{1, 1}, grow{};
    float rotation = 0, spin = 0;
    uint32_t icon = 0xffff;  // resource ordinal; 0xffff = a dot
    std::string iconState;
    double born = 0, last = 0;
    bool alive = false;
    uint32_t drawColor = 0xffffffffu;    // the colour with the fade applied, as drawn
    std::array<float, 4> m{1, 0, 0, 1};  // image-space (+y down) linear part: x' = m0 u + m1 v, y' = m2 u + m3 v
};

struct ParticleSet {
    // Definition. The fixed block is replaced by every record; a generator slot only when its key is sent.
    uint32_t pid = 0;
    uint16_t width = 0, height = 0, count = 0;
    float spawning = 0;
    std::array<float, 3> bound1{-1000, -1000, -1000}, bound2{1000, 1000, 1000}, gravity{};
    bool hasTransform = false;
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // row-vector 4x4, DM +y up
    struct Stop {
        float pos;
        uint32_t rgba;
    };
    std::vector<Stop> gradient;  // empty = none
    bool gradientLoop = false;
    uint8_t gradientSpace = 0;
    std::array<ParticleGen, PSlotCount> gen;

    // Simulation state (render clock; not in the snapshot).
    std::vector<Particle> live;
    std::mt19937 rng;
    bool started = false;
    int64_t epochNs = 0;  // the frame clock instant that is t = 0
    double last = 0;      // ticks since epochNs of the last step
    double acc = 0;       // the fractional spawn carried between steps
    int64_t drawnNs = 0;  // the clock of the last draw (MapCadence: an active set is motion)
};

// Applies one record's decoded particle block. The transform's n cells fill a row-vector 4x4 two (n <= 6), three
// (n <= 12) or four cells a row: a /matrix's six cells a,d,b,e,c,f land in the x, y and z rows, and its translation
// multiplies z, so a 2D set ignores it. The gradient is sorted by position.
void applyParticleBlock(ParticleSet& s, const proto::ParticleDef& d);

// Steps the set to clockNs, quantised down to frameNs (the client frame). seed picks the set's random stream on its first
// step. A clock earlier than the last step restarts the clock as stock does.
void advanceParticles(ParticleSet& s, int64_t clockNs, int64_t frameNs, uint32_t seed);
// True while the set would change on its next frame: it still spawns, or something is alive.
inline bool particlesActive(const ParticleSet& s) { return s.spawning > 0 || !s.live.empty(); }

// Exposed for the tests.
float particleGenNum(const ParticleGen& g, std::mt19937& rng);
std::array<float, 3> particleGenVec(const ParticleGen& g, std::mt19937& rng);
uint32_t particleGenColor(const ParticleGen& g, std::mt19937& rng);
uint32_t particleGradient(const ParticleSet& s, float x);

}  // namespace fugue::model

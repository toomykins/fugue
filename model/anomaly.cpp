#include "model/anomaly.h"

#include <cstdio>
#include <cstdlib>

namespace fugue::model::anomaly {
namespace {
FILE* g_f = nullptr;
bool g_init = false;
uint64_t g_frame = 0, g_tick = 0;
int64_t g_ns = 0;
uint16_t g_type = 0;
void init() {
    if (g_init) return;
    g_init = true;
    if (const char* p = std::getenv("FUGUE_ANOMALY_LOG"); p && *p) g_f = std::fopen(p, "w");
}
}  // namespace

bool on() {
    init();
    return g_f != nullptr;
}

void frame(uint16_t type, uint64_t resyncTick, int64_t ns) {
    g_frame++;
    g_type = type;
    g_tick = resyncTick;
    g_ns = ns;
}

void note(const char* kind, const std::string& detail) {
    if (!on()) return;
    std::fprintf(g_f, "%llu\t%llu\t%lld\t%04x\t%s\t%s\n", (unsigned long long)g_frame, (unsigned long long)g_tick,
                 (long long)(g_ns / 1000000), unsigned(g_type), kind, detail.c_str());
    std::fflush(g_f);
}
}  // namespace fugue::model::anomaly

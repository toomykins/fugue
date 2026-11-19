// S2C::ProbeEcho: the server's echo of a C2S::Probe, the probe's body verbatim.
#include "proto/registry.h"

namespace fugue::proto {

// Empty for DreamDaemon's own idle probe, else [tag u16][value u32].
ProbeEcho decodeProbeEcho(ByteSpan body) {
    Reader r(body);
    ProbeEcho m;
    if (!r.more()) return m;
    m.hasValue = true;
    m.tag = r.u16();
    m.value = r.u32();
    r.expectEnd();
    return m;
}

}  // namespace fugue::proto

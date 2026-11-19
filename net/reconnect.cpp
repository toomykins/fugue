#include "net/reconnect.h"

namespace fugue::net {

ReconnectStep reconnectStep(const SessionEnding& e, bool first, bool manual) {
    ReconnectStep r;
    if (manual || e.reboot) {
        r.delayMs = 0;
        r.sameConnection = e.reboot && !manual;
        return r;
    }
    if (e.refused) return r;
    if (e.accepted || e.afterReboot) {
        r.delayMs = 10000;
        r.message = "Connection failed after handshake. (code 0: no/unknown reason)\nReconnecting in 10 seconds...";
        return r;
    }
    if (e.afterLink && e.connected) {
        r.delayMs = 10000;
        r.message = "Connection failed during handshake. (code 0: no/unknown reason)\nReconnecting in 10 seconds...";
        return r;
    }
    if (!first)
        r.message =
            e.connected ? "Connection failed during handshake. (code 0: no/unknown reason)" : "Connection failed.";
    return r;
}

}  // namespace fugue::net

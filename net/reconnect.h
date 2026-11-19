// When a game session ends, whether and how the client connects to the world again.
#pragma once
#include <string>

namespace fugue::net {

struct SessionEnding {
    bool connected = false;    // the TCP connection was made
    bool loggedIn = false;     // the handshake and the login frame went through
    bool accepted = false;     // the server made this client a /client (SelfRef)
    bool reboot = false;       // the world rebooted (Reboot)
    bool afterReboot = false;  // this session was the login again after a reboot
    bool afterLink = false;    // this session was the join a link() started
    bool refused = false;      // the server said why it ended the session (SessionEnd)
    std::string why;
};

struct ReconnectStep {
    int delayMs = -1;             // < 0: no automatic reconnect
    bool sameConnection = false;  // log in again on the open connection (Session::relogin)
    std::string message;          // lines for the output control, '\n'-separated; "" for none
};

// As stock DreamSeeker: `manual` (.reconnect) connects at once, and a world reboot logs in again at once on the same
// connection, both silently. A session that got into the world and then lost its connection without a reason is
// retried once, 10 s later, and so is the join a link() started, when its handshake fails (vg links every client back
// to itself and then reboots). A session the server ended with a reason (a kick, a timeout), a first
// login that never got in, a join that found no server, and a failed retry are not retried.
ReconnectStep reconnectStep(const SessionEnding& e, bool first, bool manual);

}  // namespace fugue::net

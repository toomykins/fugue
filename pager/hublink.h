// HubLink: one blocking TCP connection to the BYOND hub's dtclink endpoint, with a whole-call deadline. Short frames
// only: [type u16 BE][len u16 BE][payload]. The sign-in and the login-cert token mint both dial the hub through it.
#pragma once
#include "platform/sockettypes.h"
#include <chrono>
#include <cstdint>
#include <string>
#include "net/bytes.h"

namespace fugue::pager {

constexpr const char* kHubLinkAddr = "hub.byond.com:6001";  // the hub's dtclink endpoint

class HubLink {
public:
    using Clock = std::chrono::steady_clock;
    explicit HubLink(Clock::time_point deadline) : deadline_(deadline) {}
    ~HubLink();
    HubLink(const HubLink&) = delete;
    HubLink& operator=(const HubLink&) = delete;
    bool dial(const std::string& addr, std::string* err);
    bool writeFrame(uint16_t type, const Bytes& payload, std::string* err);
    bool readFrame(uint16_t* type, Bytes* payload, std::string* err);

private:
    int remainingMs() const;
    bool readFull(uint8_t* b, size_t n, std::string* err);
    socket::Handle fd_ = -1;
    Clock::time_point deadline_;
};

// Spaces out hub dials. allow() refuses a dial that comes within the current gap of the last allowed one; the gap is
// `base` after a success and doubles after each consecutive failure, up to `cap`.
class HubThrottle {
public:
    using Clock = std::chrono::steady_clock;
    explicit HubThrottle(std::chrono::milliseconds base, std::chrono::milliseconds cap)
        : base_(base), cap_(cap), gap_(base) {}
    // True (and the dial counts as made at `now`) when enough time has passed; otherwise false and *waitMs is the time left.
    bool allow(Clock::time_point now, int* waitMs = nullptr);
    void record(bool ok);  // the outcome of the dial that allow() let through

private:
    std::chrono::milliseconds base_, cap_, gap_;
    bool any_ = false;
    Clock::time_point last_;
};

}  // namespace fugue::pager

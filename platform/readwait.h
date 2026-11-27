// Deadline-capable read wait for a single-owner, initially blocking session socket.
// WSAPoll/select round short timeouts to ~15.6 ms on Windows even with timeBeginPeriod.
// Wait on socket readiness OR a high-resolution timer instead, without polling/spinning.
#pragma once
#include "platform/socket.h"
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fugue::socket {
class ReadWait {
public:
    ReadWait() = default;
    ReadWait(const ReadWait&) = delete;
    ReadWait& operator=(const ReadWait&) = delete;
    ~ReadWait() { reset(); }
    // Must be called before the owner closes/replaces its socket.
    void reset() {
#ifdef _WIN32
        if (bound_ >= 0) {
            WSAEventSelect(SOCKET(bound_), nullptr, 0);
            nonblocking(bound_, false);
            bound_ = -1;
        }
        if (event_ != WSA_INVALID_EVENT) { WSACloseEvent(event_); event_ = WSA_INVALID_EVENT; }
        if (timer_) { CloseHandle(timer_); timer_ = nullptr; }
#endif
    }
    // >0: readable/closed, 0: timeout, <0: error. Caller must handle EAGAIN after readiness.
    int wait(Handle fd, std::chrono::microseconds timeout) {
        if (timeout.count() <= 0) return 0;
        timeout = std::min(timeout, std::chrono::microseconds(100'000));
#ifdef _WIN32
        if (!unavailable_ && bind(fd)) {
            LARGE_INTEGER due;
            due.QuadPart = -timeout.count() * 10; // relative 100 ns units
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                HANDLE handles[] = {event_, timer_};
                DWORD ready = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                CancelWaitableTimer(timer_);
                if (ready == WAIT_OBJECT_0 + 1) return 0;
                if (ready == WAIT_OBJECT_0) {
                    WSANETWORKEVENTS events{};
                    if (result(WSAEnumNetworkEvents(SOCKET(fd), event_, &events)) < 0) return -1;
                    return (events.lNetworkEvents & (FD_READ | FD_CLOSE)) ? 1 : 0;
                }
            }
            // Keep working on older/restricted Windows hosts, with the old timing precision.
            reset();
            unavailable_ = true;
        }
#endif
        PollFd p{fd, POLLIN, 0};
        return poll(&p, 1, int(std::chrono::ceil<std::chrono::milliseconds>(timeout).count()));
    }
private:
#ifdef _WIN32
    bool bind(Handle fd) {
        if (bound_ == fd) return true;
        reset();
        // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Windows 10 1803+, below our minimum).
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_MODIFY_STATE | SYNCHRONIZE);
        event_ = WSACreateEvent();
        if (!timer_ || event_ == WSA_INVALID_EVENT || WSAEventSelect(SOCKET(fd), event_, FD_READ | FD_CLOSE) != 0) {
            reset();
            unavailable_ = true;
            return false;
        }
        bound_ = fd; // WSAEventSelect makes this socket nonblocking until reset().
        return true;
    }
    Handle bound_ = -1;
    WSAEVENT event_ = WSA_INVALID_EVENT;
    HANDLE timer_ = nullptr;
    bool unavailable_ = false;
#endif
};
}

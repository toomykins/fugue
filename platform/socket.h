// Qt-free socket portability. Handles must never be narrowed to int on Windows.
#pragma once
#include <algorithm>
#include <cerrno>
#include <climits>
#include "platform/sockettypes.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include "platform/posix.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fugue::socket {
#ifdef _WIN32
using Length = int;
struct PollFd {
    Handle fd;
    short events;
    short revents;
};
inline int result(int r) {
    if (r == SOCKET_ERROR) {
        switch (WSAGetLastError()) {
            case WSAEWOULDBLOCK: errno = EAGAIN; break;
            case WSAEINPROGRESS: errno = EINPROGRESS; break;
            case WSAEINTR: errno = EINTR; break;
            case WSAECONNREFUSED: errno = ECONNREFUSED; break;
            case WSAETIMEDOUT: errno = ETIMEDOUT; break;
            case WSAECONNRESET: errno = ECONNRESET; break;
            default: errno = EIO; break;
        }
    }
    return r;
}
inline const char* addressError(int e) { return gai_strerrorA(e); }
inline bool initialize() {
    static const bool ok = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
inline Handle create(int family, int type, int protocol) {
    if (!initialize()) return -1;
    SOCKET s = WSASocketW(family, type, protocol, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    return s == INVALID_SOCKET ? (result(SOCKET_ERROR), -1) : Handle(s);
}
inline int close(Handle s) { return result(closesocket(SOCKET(s))); }
inline int nonblocking(Handle s, bool on) {
    u_long v = on;
    return result(ioctlsocket(SOCKET(s), FIONBIO, &v));
}
inline int send(Handle s, const void* b, size_t n, int = 0) {
    return result(::send(SOCKET(s), static_cast<const char*>(b), int(std::min(n, size_t(INT_MAX))), 0));
}
inline int recv(Handle s, void* b, size_t n, int = 0) {
    return result(::recv(SOCKET(s), static_cast<char*>(b), int(std::min(n, size_t(INT_MAX))), 0));
}
inline int poll(PollFd* p, unsigned long n, int ms) {
    // Call sites wait on one socket at a time.
    if (n != 1) {
        errno = EINVAL;
        return -1;
    }
    WSAPOLLFD w{SOCKET(p->fd), p->events, 0};
    int r = result(WSAPoll(&w, 1, ms));
    p->revents = w.revents;
    return r;
}
inline int connect(Handle s, const sockaddr* a, int n) {
    int r = result(::connect(SOCKET(s), a, n));
    if (r < 0 && errno == EAGAIN) errno = EINPROGRESS;
    return r;
}
inline int setsockopt(Handle s, int level, int opt, const void* v, int n) {
    return result(::setsockopt(SOCKET(s), level, opt, static_cast<const char*>(v), n));
}
inline int getsockopt(Handle s, int level, int opt, void* v, Length* n) {
    return result(::getsockopt(SOCKET(s), level, opt, static_cast<char*>(v), n));
}
#else
using Length = socklen_t;
using PollFd = pollfd;
inline const char* addressError(int e) { return gai_strerror(e); }
inline bool initialize() { return true; }
inline Handle create(int family, int type, int protocol) { return platform::socket(family, type, protocol); }
inline int close(Handle s) { return ::close(s); }
inline int nonblocking(Handle s, bool on) {
    int f = fcntl(s, F_GETFL);
    return f < 0 ? -1 : fcntl(s, F_SETFL, on ? f | O_NONBLOCK : f & ~O_NONBLOCK);
}
inline auto send(Handle s, const void* b, size_t n, int = 0) { return platform::sendNoSignal(s, b, n); }
using ::connect;
using ::getsockopt;
using ::poll;
using ::recv;
using ::setsockopt;
#endif
}  // namespace fugue::socket

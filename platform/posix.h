// Small Linux/macOS POSIX differences. Never ignore SIGPIPE process-wide: Qt and child processes share it.
#pragma once
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fugue::platform {

inline int socket(int domain, int type, int protocol, bool nonblocking = false) {
#ifdef __linux__
    return ::socket(domain, type | SOCK_CLOEXEC | (nonblocking ? SOCK_NONBLOCK : 0), protocol);
#else
    int fd = ::socket(domain, type, protocol);
    if (fd < 0) return fd;
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || (nonblocking && ::fcntl(fd, F_SETFL, O_NONBLOCK) < 0)) {
        int e = errno;
        ::close(fd);
        errno = e;
        return -1;
    }
    return fd;
#endif
}

// Also works on accepted sockets (including the loopback servers in tests).
inline ssize_t sendNoSignal(int fd, const void* data, size_t size) {
#ifdef MSG_NOSIGNAL
    return ::send(fd, data, size, MSG_NOSIGNAL);
#else
    int one = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) < 0) return -1;
    return ::send(fd, data, size, 0);
#endif
}

inline int pipeCloseOnExec(int fds[2]) {
#ifdef __linux__
    return ::pipe2(fds, O_CLOEXEC);
#else
    if (::pipe(fds) < 0) return -1;
    if (::fcntl(fds[0], F_SETFD, FD_CLOEXEC) == 0 && ::fcntl(fds[1], F_SETFD, FD_CLOEXEC) == 0) return 0;
    int e = errno;
    ::close(fds[0]);
    ::close(fds[1]);
    errno = e;
    return -1;
#endif
}

}  // namespace fugue::platform

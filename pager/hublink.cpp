#include "pager/hublink.h"
#include <algorithm>
#include "platform/socket.h"
#include <cerrno>
#include <cstring>

namespace fugue::pager {

HubLink::~HubLink() {
    if (fd_ >= 0) socket::close(fd_);
}

bool HubLink::dial(const std::string& addr, std::string* err) {
    size_t c = addr.rfind(':');
    if (c == std::string::npos) {
        *err = "bad hub address " + addr;
        return false;
    }
    std::string host = addr.substr(0, c), port = addr.substr(c + 1);
    if (host.size() > 1 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    if (!socket::initialize()) {
        *err = "Winsock initialization failed";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &res); rc != 0) {
        *err = "hub: dial " + addr + ": " + socket::addressError(rc);
        return false;
    }
    std::string last = "no address";
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        auto fd = socket::create(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            last = std::strerror(errno);
            continue;
        }
        if (socket::nonblocking(fd, true) != 0) {
            socket::close(fd);
            continue;
        }
        if (socket::connect(fd, ai->ai_addr, int(ai->ai_addrlen)) != 0 && errno != EINPROGRESS) {
            last = std::strerror(errno);
            socket::close(fd);
            continue;
        }
        socket::PollFd p{fd, POLLOUT, 0};
        int pr = socket::poll(&p, 1, remainingMs());
        int soerr = 0;
        socket::Length sl = sizeof soerr;
        if (pr <= 0) {
            last = pr == 0 ? "timed out" : std::strerror(errno);
            socket::close(fd);
            continue;
        }
        if (socket::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            last = "connection failed";
            socket::close(fd);
            continue;
        }
        fd_ = fd;
        break;
    }
    ::freeaddrinfo(res);
    if (fd_ < 0) {
        *err = "hub: dial " + addr + ": " + last;
        return false;
    }
    return true;
}
bool HubLink::writeFrame(uint16_t type, const Bytes& payload, std::string* err) {
    Bytes w{uint8_t(type >> 8), uint8_t(type), uint8_t(payload.size() >> 8), uint8_t(payload.size())};
    w.insert(w.end(), payload.begin(), payload.end());
    size_t off = 0;
    while (off < w.size()) {
        socket::PollFd p{fd_, POLLOUT, 0};
        int pr = socket::poll(&p, 1, remainingMs());
        if (pr <= 0) {
            *err = pr == 0 ? "timed out" : std::strerror(errno);
            return false;
        }
        auto n = socket::send(fd_, w.data() + off, w.size() - off);
        if (n == 0) {
            *err = "EOF";
            return false;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            *err = std::strerror(errno);
            return false;
        }
        off += size_t(n);
    }
    return true;
}
bool HubLink::readFrame(uint16_t* type, Bytes* payload, std::string* err) {
    uint8_t h[4];
    if (!readFull(h, 4, err)) return false;
    *type = uint16_t(h[0] << 8 | h[1]);
    payload->assign(size_t(h[2] << 8 | h[3]), 0);
    return payload->empty() || readFull(payload->data(), payload->size(), err);
}

int HubLink::remainingMs() const {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - Clock::now()).count();
    return ms < 0 ? 0 : int(ms);
}
bool HubLink::readFull(uint8_t* b, size_t n, std::string* err) {
    size_t off = 0;
    while (off < n) {
        socket::PollFd p{fd_, POLLIN, 0};
        int pr = socket::poll(&p, 1, remainingMs());
        if (pr == 0) {
            *err = "timed out";
            return false;
        }
        if (pr < 0) {
            if (errno == EINTR) continue;
            *err = std::strerror(errno);
            return false;
        }
        auto r = socket::recv(fd_, b + off, n - off, 0);
        if (r == 0) {
            *err = "EOF";
            return false;
        }
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            *err = std::strerror(errno);
            return false;
        }
        off += size_t(r);
    }
    return true;
}

bool HubThrottle::allow(Clock::time_point now, int* waitMs) {
    if (any_ && now - last_ < gap_) {
        if (waitMs)
            *waitMs = int(std::chrono::duration_cast<std::chrono::milliseconds>(gap_ - (now - last_)).count()) + 1;
        return false;
    }
    any_ = true;
    last_ = now;
    return true;
}

void HubThrottle::record(bool ok) { gap_ = ok ? base_ : std::min(cap_, gap_ * 2); }

}  // namespace fugue::pager

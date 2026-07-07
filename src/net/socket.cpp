#include "net/socket.h"
#include "common/log.h"

#include <unistd.h>
#include <fcntl.h>

Socket::~Socket() {
    if (fd_ >= 0) ::close(fd_);
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::reset(int fd) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

void Socket::set_nonblocking() {
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) die("fcntl(F_GETFL)");
    if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl(F_SETFL, O_NONBLOCK)");
}

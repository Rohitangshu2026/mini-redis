#include "net/socket.h"
#include "common/log.h"

#include <unistd.h>
#include <fcntl.h>

Socket::~Socket(){
    if(fd_ >= 0) ::close(fd_);
}

// Move assignment: close whatever this socket currently owns, steal the
// other's fd, and leave the other empty so its destructor won't close the
// fd we just took. The self-assignment check matters — without it, moving
// a socket onto itself would close its own fd and then "adopt" the dead one.
Socket& Socket::operator=(Socket&& other) noexcept {
    if(this != &other){
        if(fd_ >= 0) ::close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

// Replace the owned fd, closing the previous one. reset() with no argument
// just closes and empties the socket.
void Socket::reset(int fd){
    if(fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

// Switch the fd to non-blocking mode: read()/write() will return EAGAIN
// instead of sleeping. The event loop requires this on every socket it
// polls — one accidentally-blocking fd would stall every connection.
// fcntl is read-modify-write so other flags are preserved.
void Socket::set_nonblocking(){
    int flags = fcntl(fd_, F_GETFL, 0);
    if(flags < 0) die("fcntl(F_GETFL)");
    if(fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) die("fcntl(F_SETFL, O_NONBLOCK)");
}

#pragma once

// Move-only RAII wrapper around a socket file descriptor.
// Owns the fd and closes it on destruction. Copying is deleted so two
// Sockets can never own (and double-close) the same fd.
class Socket {
public:
    Socket() = default;                       // empty (fd_ == -1)
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket();                                // closes fd_ if valid

    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept;
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    int  fd() const    { return fd_; }
    bool valid() const { return fd_ >= 0; }

    int  release() { int t = fd_; fd_ = -1; return t; }  // give up ownership, no close
    void reset(int fd = -1);                              // replace fd, closing old one
    void set_nonblocking();                               // O_NONBLOCK; aborts on failure

private:
    int fd_ = -1;
};

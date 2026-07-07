#pragma once

#include <cstdint>
#include <vector>
#include <unistd.h>

// One client connection. The event loop sets want_* to express intentions;
// incoming/outgoing are byte buffers the loop fills and drains.
struct Conn {
    int  fd         = -1;
    bool want_read  = false;
    bool want_write = false;
    bool want_close = false;
    std::vector<uint8_t> incoming;   // bytes read from socket, awaiting parse
    std::vector<uint8_t> outgoing;   // bytes awaiting write to socket

    Conn() = default;
    ~Conn() { if (fd >= 0) ::close(fd); }   // owns the fd
    Conn(const Conn&) = delete;             // never copy (would double-close)
    Conn& operator=(const Conn&) = delete;
};

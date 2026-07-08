#pragma once

#include "ds/dlist.h"

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

    // Idle timer: the connection expires at last_active_ms + timeout.
    // idle_node keeps connections in last-active order in the server's list.
    uint64_t last_active_ms = 0;
    DList    idle_node;

    Conn() { dlist_init(&idle_node); }
    ~Conn() {
        dlist_detach(&idle_node);            // safe even if never listed
        if (fd >= 0) ::close(fd);            // owns the fd
    }
    Conn(const Conn&) = delete;              // never copy (would double-close)
    Conn& operator=(const Conn&) = delete;
};

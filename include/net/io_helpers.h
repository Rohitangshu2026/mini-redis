#pragma once

#include <cstddef>
#include <cstdint>

// Blocking I/O helpers that hide partial reads and writes.
//
// A single read() on a TCP socket returns whatever happens to be in the
// kernel buffer, which can be less than asked for; a single write() can
// likewise accept only part of the data when the send buffer is nearly
// full. Any code that assumes one call moves n bytes is silently broken.
// These helpers loop until the full count is transferred or the peer is
// gone. They are for BLOCKING sockets only (the client uses them); the
// server's event loop must never block, so it does its own buffering.

// Read exactly n bytes into buf.
// Returns 0 on success, -1 on error or if EOF arrives before n bytes.
int32_t read_full(int fd, char* buf, size_t n);

// Write exactly n bytes from buf.
// Returns 0 on success, -1 on error (a closed peer surfaces as an error).
int32_t write_all(int fd, const char* buf, size_t n);

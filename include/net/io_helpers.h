#pragma once

#include <cstddef>
#include <cstdint>

// Read exactly n bytes into buf, looping over short reads.
// Returns 0 on success, -1 on EOF or error.
int32_t read_full(int fd, char* buf, size_t n);

// Write exactly n bytes from buf, looping over short writes.
// Returns 0 on success, -1 on error.
int32_t write_all(int fd, const char* buf, size_t n);

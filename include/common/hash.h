#pragma once

#include <cstddef>
#include <cstdint>

// FNV-1a-style string hash, shared by the keyspace and the sorted set's
// name index so a given string always hashes the same everywhere.
inline uint64_t str_hash(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193u;
    }
    return h;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

// Max payload we accept for a single frame body.
constexpr size_t MAX_MSG = 32 * 1024;   // 32 KiB

// Response values are serialized by protocol/serialize.h.

// Little-endian u32 read/write (host is LE on x86/ARM; memcpy avoids UB).
inline uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
inline void write_u32(uint8_t* p, uint32_t v) {
    std::memcpy(p, &v, 4);
}

// Parse a request body: [u32 nstr][u32 len1][str1]...  -> out.
// Returns 0 on success, -1 if malformed/truncated.
int32_t parse_req(const uint8_t* data, size_t size, std::vector<std::string>& out);

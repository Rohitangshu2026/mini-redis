#include <catch2/catch_test_macros.hpp>

#include "protocol/wire.h"

#include <cstdint>

TEST_CASE("write_u32 / read_u32 round-trip", "[wire]") {
    const uint32_t values[] = {0u, 1u, 255u, 256u, 65535u, 0xDEADBEEFu, 0xFFFFFFFFu};
    for (uint32_t v : values) {
        uint8_t buf[4];
        write_u32(buf, v);
        REQUIRE(read_u32(buf) == v);
    }
}

TEST_CASE("write_u32 is little-endian", "[wire]") {
    uint8_t buf[4];
    write_u32(buf, 0x01020304u);
    REQUIRE(buf[0] == 0x04);
    REQUIRE(buf[1] == 0x03);
    REQUIRE(buf[2] == 0x02);
    REQUIRE(buf[3] == 0x01);
}

TEST_CASE("MAX_MSG is 32 KiB", "[wire]") {
    REQUIRE(MAX_MSG == 32u * 1024u);
}

#include <catch2/catch_test_macros.hpp>

#include "protocol/serialize.h"

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>

TEST_CASE("serialize: nil is a single tag byte", "[serialize]"){
    std::vector<uint8_t> out;
    out_nil(out);
    REQUIRE(out.size() == 1);
    REQUIRE(out[0] == SER_NIL);
}

TEST_CASE("serialize: int carries an 8-byte value", "[serialize]"){
    std::vector<uint8_t> out;
    out_int(out, -42);
    REQUIRE(out.size() == 1 + 8);
    REQUIRE(out[0] == SER_INT);
    int64_t v; std::memcpy(&v, out.data() + 1, 8);
    REQUIRE(v == -42);
}

TEST_CASE("serialize: str is tag + u32 length + bytes", "[serialize]"){
    std::vector<uint8_t> out;
    out_str(out, "hello");
    REQUIRE(out[0] == SER_STR);
    uint32_t len; std::memcpy(&len, out.data() + 1, 4);
    REQUIRE(len == 5);
    REQUIRE(std::string(out.begin() + 5, out.end()) == "hello");
}

TEST_CASE("serialize: array header then elements", "[serialize]"){
    std::vector<uint8_t> out;
    out_arr(out, 2);
    out_str(out, "a");
    out_str(out, "bb");
    REQUIRE(out[0] == SER_ARR);
    uint32_t n; std::memcpy(&n, out.data() + 1, 4);
    REQUIRE(n == 2);
    // 5 (arr header) + (1+4+1) "a" + (1+4+2) "bb"
    REQUIRE(out.size() == 5 + 6 + 7);
}

TEST_CASE("serialize: err has code + message", "[serialize]"){
    std::vector<uint8_t> out;
    out_err(out, ERR_UNKNOWN, "nope");
    REQUIRE(out[0] == SER_ERR);
    uint32_t code, len;
    std::memcpy(&code, out.data() + 1, 4);
    std::memcpy(&len,  out.data() + 5, 4);
    REQUIRE(code == (uint32_t)ERR_UNKNOWN);
    REQUIRE(len == 4);
    REQUIRE(std::string(out.begin() + 9, out.end()) == "nope");
}

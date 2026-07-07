#include <catch2/catch_test_macros.hpp>

#include "protocol/wire.h"

#include <vector>
#include <string>
#include <cstdint>

static void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t t[4]; write_u32(t, v);
    b.insert(b.end(), t, t + 4);
}
static std::vector<uint8_t> make_req(const std::vector<std::string>& args) {
    std::vector<uint8_t> b;
    put_u32(b, (uint32_t)args.size());
    for (const auto& a : args) {
        put_u32(b, (uint32_t)a.size());
        b.insert(b.end(), a.begin(), a.end());
    }
    return b;
}

TEST_CASE("parse_req: happy path", "[parser]") {
    auto b = make_req({"set", "foo", "bar"});
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == 0);
    REQUIRE(out == std::vector<std::string>{"set", "foo", "bar"});
}

TEST_CASE("parse_req: zero args", "[parser]") {
    auto b = make_req({});
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == 0);
    REQUIRE(out.empty());
}

TEST_CASE("parse_req: empty-string argument is preserved", "[parser]") {
    auto b = make_req({"get", ""});
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == 0);
    REQUIRE(out.size() == 2);
    REQUIRE(out[1].empty());
}

TEST_CASE("parse_req: truncated header is rejected", "[parser]") {
    uint8_t b[2] = {0, 0};
    std::vector<std::string> out;
    REQUIRE(parse_req(b, sizeof(b), out) == -1);
}

TEST_CASE("parse_req: missing argument body is rejected", "[parser]") {
    std::vector<uint8_t> b;
    put_u32(b, 2);            // claim 2 args
    put_u32(b, 1);            // arg0 length = 1
    b.push_back('x');         // arg0 = "x"; arg1 entirely absent
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == -1);
}

TEST_CASE("parse_req: arg length past end is rejected", "[parser]") {
    std::vector<uint8_t> b;
    put_u32(b, 1);            // 1 arg
    put_u32(b, 100);          // claims 100 bytes...
    b.push_back('a'); b.push_back('b'); b.push_back('c');   // ...only 3 present
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == -1);
}

TEST_CASE("parse_req: trailing garbage is rejected", "[parser]") {
    auto b = make_req({"a"});
    b.push_back(0xFF);        // extra byte after a well-formed request
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == -1);
}

TEST_CASE("parse_req: absurd argument count is rejected", "[parser]") {
    std::vector<uint8_t> b;
    put_u32(b, 300000);       // > k_max_args (200000)
    std::vector<std::string> out;
    REQUIRE(parse_req(b.data(), b.size(), out) == -1);
}

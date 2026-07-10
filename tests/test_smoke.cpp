#include <catch2/catch_test_macros.hpp>

// Trivial smoke test to confirm Catch2 + CTest wiring works.
// Real tests start landing on Day 8 (hashtable).
TEST_CASE("smoke: arithmetic works", "[smoke]"){
    REQUIRE(1 + 1 == 2);
    REQUIRE(2 * 3 == 6);
}
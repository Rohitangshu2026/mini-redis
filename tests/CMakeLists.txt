# Fetch Catch2 v3 at configure time
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(Catch2)

# Make Catch2's CMake helpers available
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Unit test binary — grows as we add more tests
add_executable(unit_tests
    test_smoke.cpp
    unit/test_socket.cpp
    unit/test_wire.cpp
    unit/test_parser.cpp
    unit/test_hmap.cpp
    unit/test_serialize.cpp
    unit/test_avl.cpp
    unit/test_zset.cpp
    unit/test_dlist.cpp
    unit/test_heap.cpp
)
target_link_libraries(unit_tests PRIVATE Catch2::Catch2WithMain mini-redis-core)

# Auto-register each TEST_CASE with CTest
catch_discover_tests(unit_tests)

cmake_minimum_required(VERSION 3.20)
project(mini_redis
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Redis-compatible in-memory data store, built from scratch"
)

# ---- C++ standard ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Default build type ----
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---- Warnings (treat as errors later when stable) ----
add_compile_options(-Wall -Wextra -Wpedantic)

# ---- Export compile_commands.json (for clangd, IDEs) ----
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Include path ----
include_directories(${CMAKE_SOURCE_DIR}/include)

# ---- Server binary ----
add_executable(mini-redis-server
    src/main.cpp
    src/server/server.cpp
    src/server/client_connection.cpp
)

# ---- Client binary ----
add_executable(mini-redis-client
    client/client.cpp
)

# ---- Tests ----
enable_testing()
add_subdirectory(tests)
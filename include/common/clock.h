#pragma once

#include <cstdint>
#include <ctime>

// Monotonic time in milliseconds. Wall-clock time can jump (NTP, manual
// adjustment), so durations measured with it can be wrong or negative;
// monotonic time only moves forward. poll() times out against the monotonic
// clock internally, so timers must use the same basis.
inline uint64_t get_monotonic_msec() {
    struct timespec tv = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_nsec / 1000 / 1000;
}

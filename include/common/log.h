#pragma once

#include<cstdio>
#include<cstdlib>
#include<cstring>
#include<cerrno>

// Print a message to stderr (no abort).
inline void msg(const char* m){
    std::fprintf(stderr, "%s\n", m);
}

// Print a message + errno description to stderr (no abort).
inline void msg_errno(const char* m){
    std::fprintf(stderr, "[errno:%d] %s: %s\n", errno, m, std::strerror(errno));
}

// Print message + errno and abort the process. Never returns.
[[noreturn]] inline void die(const char* m){
    std::fprintf(stderr, "[FATAL][errno:%d] %s: %s\n", errno, m, std::strerror(errno));
    std::abort();
}
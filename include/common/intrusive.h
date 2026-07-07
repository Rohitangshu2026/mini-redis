#pragma once

#include <cstddef>

// Recover the address of the enclosing struct from a pointer to its member.
//   Entry* e = container_of(hnode, Entry, node);
// Requires `member` to be the FIRST member for the offset-0 round-trip we rely on.
#define container_of(ptr, T, member) \
    ((T*)((char*)(ptr) - offsetof(T, member)))

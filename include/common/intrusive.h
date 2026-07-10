#pragma once

#include <cstddef>

// Recover the address of the enclosing struct from a pointer to one of its
// members:
//
//     struct Entry{ HNode node; ... };
//     HNode* n = ...;                              // from the hashtable
//     Entry* e = container_of(n, Entry, node);     // back to the owner
//
// This is the trick that makes intrusive data structures work: the container
// (hashtable, tree, list, heap) only ever sees its own small node type, and
// the owner is recovered by subtracting the member's byte offset. It works
// for a member at ANY offset, not just the first one — the sorted set relies
// on this to recover a ZNode from either of its two embedded index hooks.
//
// The cast goes through char* because pointer arithmetic on char is measured
// in bytes, which is what offsetof() gives back.
#define container_of(ptr, T, member) \
    ((T*)((char*)(ptr) - offsetof(T, member)))

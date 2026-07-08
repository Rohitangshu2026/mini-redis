#pragma once

#include "store/hashtable.h"
#include "ds/zset.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Value types a key can hold.
enum : uint32_t {
    T_STR  = 1,
    T_ZSET = 2,
};

// A key and its value in the keyspace. HNode must stay the first member
// (container_of relies on it). The two value members could share a union,
// but std::string's constructor/destructor would then need manual placement
// calls everywhere; an empty std::string and an empty ZSet cost little, so
// the plain tagged form is the simpler, safer trade.
struct Entry {
    HNode       node;           // intrusive hashtable hook (FIRST member)
    std::string key;
    uint32_t    type = T_STR;
    std::string str;            // value when type == T_STR
    ZSet        zset;           // value when type == T_ZSET

    // Index of this key's timer in Database::ttl, or (size_t)-1 when the
    // key has no TTL. The heap maintains it through its back-pointer.
    size_t heap_idx = (size_t)-1;
};

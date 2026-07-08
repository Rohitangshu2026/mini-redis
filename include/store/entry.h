#pragma once

#include "store/hashtable.h"
#include "ds/zset.h"

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
};

// Free an entry and everything its value owns.
inline void entry_del(Entry* ent) {
    if (ent->type == T_ZSET) {
        zset_clear(&ent->zset);
    }
    delete ent;
}

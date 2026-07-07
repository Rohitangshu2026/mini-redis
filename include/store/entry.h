#pragma once

#include "store/hashtable.h"
#include <string>

// A key/value pair in the keyspace. HNode MUST be first (container_of relies on it).
// In Ch 11+ this grows a type tag + union for list/hash/set/zset; for now it's a string.
struct Entry {
    HNode       node;   // intrusive hashtable hook (FIRST member)
    std::string key;
    std::string val;
};

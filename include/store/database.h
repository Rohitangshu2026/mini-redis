#pragma once

#include "store/hashtable.h"
#include "store/entry.h"
#include "ds/heap.h"

#include <cstdint>
#include <vector>

// The keyspace: the primary hash index over entries plus the TTL timer
// heap. ttl[0] is always the nearest expiration; every entry with a TTL
// carries its heap position in Entry::heap_idx, which the heap keeps in
// sync through its back-pointers.
struct Database {
    HMap                  map;
    std::vector<HeapItem> ttl;
};

// Set or update a key's TTL (ttl_ms >= 0), or remove it (ttl_ms < 0).
void entry_set_ttl(Database& db, Entry* ent, int64_t ttl_ms);

// Free an entry: drops its timer, tears down its value, deletes it.
// (Does NOT unlink it from db.map - callers detach it first.)
void entry_del(Database& db, Entry* ent);

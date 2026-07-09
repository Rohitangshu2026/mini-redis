#pragma once

#include "store/hashtable.h"
#include "store/entry.h"
#include "ds/heap.h"

#include <cstdint>
#include <vector>

class ThreadPool;   // full definition in threadpool/thread_pool.h

// The keyspace: the primary hash index over entries plus the TTL timer
// heap. ttl[0] is always the nearest expiration; every entry with a TTL
// carries its heap position in Entry::heap_idx, which the heap keeps in
// sync through its back-pointers.
struct Database {
    HMap                  map;
    std::vector<HeapItem> ttl;
    ThreadPool*           pool = nullptr;   // for async teardown of large values
};

// Set or update a key's TTL (ttl_ms >= 0), or remove it (ttl_ms < 0).
void entry_set_ttl(Database& db, Entry* ent, int64_t ttl_ms);

// Free an entry: drops its timer, then tears down its value and deletes it.
// Large sorted sets are handed to the worker pool (when one is attached) so
// an O(n) destructor never runs on the event loop.
// (Does NOT unlink it from db.map - callers detach it first.)
void entry_del(Database& db, Entry* ent);

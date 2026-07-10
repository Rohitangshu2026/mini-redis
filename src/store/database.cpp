#include "store/database.h"
#include "threadpool/thread_pool.h"
#include "common/clock.h"

void entry_set_ttl(Database& db, Entry* ent, int64_t ttl_ms){
    if(ttl_ms < 0){
        // negative TTL means: remove the timer, the key becomes permanent
        if(ent->heap_idx != (size_t)-1){
            heap_delete(db.ttl, ent->heap_idx);
            ent->heap_idx = (size_t)-1;
        }
    }else{
        uint64_t expire_at = get_monotonic_msec() + (uint64_t)ttl_ms;
        HeapItem item = {expire_at, &ent->heap_idx};
        heap_upsert(db.ttl, ent->heap_idx, item);   // heap writes heap_idx back
    }
}

// Tear down the value and free the entry. This is the O(n) part for big
// containers, so it's what gets shipped to a worker thread.
static void entry_del_sync(Entry* ent){
    if(ent->type == T_ZSET){
        zset_clear(&ent->zset);
    }
    delete ent;
}

static void entry_del_task(void* arg){
    entry_del_sync(static_cast<Entry*>(arg));
}

void entry_del(Database& db, Entry* ent){
    entry_set_ttl(db, ent, -1);        // drop the timer (event-loop thread owns db)
    // Freeing a huge sorted set is O(n); hand it to a worker so the event
    // loop isn't stalled. Small values are freed inline — a context switch
    // would cost more than the teardown itself. By this point the entry is
    // unlinked from every shared structure, so the worker touches only the
    // orphaned value; keyspace atomicity is unaffected.
    constexpr size_t k_large_container_size = 1000;
    size_t set_size = (ent->type == T_ZSET) ? hm_size(&ent->zset.hmap) : 0;
    if(db.pool && set_size > k_large_container_size){
        db.pool->queue(&entry_del_task, ent);
    }else{
        entry_del_sync(ent);
    }
}

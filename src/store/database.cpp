#include "store/database.h"
#include "common/clock.h"

void entry_set_ttl(Database& db, Entry* ent, int64_t ttl_ms) {
    if (ttl_ms < 0) {
        // negative TTL means: remove the timer, the key becomes permanent
        if (ent->heap_idx != (size_t)-1) {
            heap_delete(db.ttl, ent->heap_idx);
            ent->heap_idx = (size_t)-1;
        }
    } else {
        uint64_t expire_at = get_monotonic_msec() + (uint64_t)ttl_ms;
        HeapItem item = {expire_at, &ent->heap_idx};
        heap_upsert(db.ttl, ent->heap_idx, item);   // heap writes heap_idx back
    }
}

void entry_del(Database& db, Entry* ent) {
    entry_set_ttl(db, ent, -1);        // drop the timer, if any
    if (ent->type == T_ZSET) {
        zset_clear(&ent->zset);
    }
    delete ent;
}

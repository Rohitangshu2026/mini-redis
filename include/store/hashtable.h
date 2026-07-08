#pragma once

#include <cstddef>
#include <cstdint>

// Intrusive node — embed this as the FIRST member of your own struct.
struct HNode {
    HNode*   next  = nullptr;
    uint64_t hcode = 0;          // cached hash code
};

// One fixed-size, open-chained table. Capacity is always a power of two.
struct HTab {
    HNode** tab  = nullptr;      // array of (mask+1) chain heads
    size_t  mask = 0;            // capacity - 1
    size_t  size = 0;            // number of nodes stored
};

// Two tables enable progressive (incremental) rehashing.
struct HMap {
    HTab   newer;                // inserts always go here
    HTab   older;                // being migrated away from (empty when not resizing)
    size_t migrate_pos = 0;
};

// Public API (all amortized O(1), no O(n) resize pause):
HNode* hm_lookup(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*));
void   hm_insert(HMap* hmap, HNode* node);
HNode* hm_delete(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*));   // detaches, returns node
void   hm_foreach(HMap* hmap, bool (*f)(HNode*, void*), void* arg);
void   hm_clear(HMap* hmap);                                            // frees table arrays only
size_t hm_size(const HMap* hmap);

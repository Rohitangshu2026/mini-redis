#include "store/hashtable.h"

#include <cassert>
#include <cstdlib>

// ===== Ch 9: fixed-size table primitives (internal) =====

// n must be a power of two
static void h_init(HTab* htab, size_t n) {
    assert(n > 0 && ((n - 1) & n) == 0);
    htab->tab  = (HNode**)calloc(n, sizeof(HNode*));
    htab->mask = n - 1;
    htab->size = 0;
}

// prepend node to its slot's chain
static void h_insert(HTab* htab, HNode* node) {
    size_t pos     = node->hcode & htab->mask;
    node->next     = htab->tab[pos];
    htab->tab[pos] = node;
    htab->size++;
}

// Return the ADDRESS of the pointer that owns the matching node (so the caller
// can splice it out in O(1) without re-walking the chain), or nullptr.
static HNode** h_lookup(HTab* htab, HNode* key, bool (*eq)(HNode*, HNode*)) {
    if (!htab->tab) return nullptr;
    size_t pos = key->hcode & htab->mask;
    HNode** from = &htab->tab[pos];                 // points at the slot head first
    for (HNode* cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) return from;
    }
    return nullptr;
}

// unlink the node addressed by *from
static HNode* h_detach(HTab* htab, HNode** from) {
    HNode* node = *from;
    *from = node->next;
    htab->size--;
    return node;
}

// ===== Ch 10: progressive rehashing =====

static const size_t k_rehashing_work  = 128;   // slots migrated per operation
static const size_t k_max_load_factor = 8;     // size/capacity before growing

static void hm_help_rehashing(HMap* hmap) {
    size_t nwork = 0;
    while (nwork < k_rehashing_work && hmap->older.size > 0) {
        HNode** from = &hmap->older.tab[hmap->migrate_pos];
        if (!*from) { hmap->migrate_pos++; continue; }      // skip empty slot
        h_insert(&hmap->newer, h_detach(&hmap->older, from));
        nwork++;
    }
    if (hmap->older.size == 0 && hmap->older.tab) {         // migration complete
        free(hmap->older.tab);
        hmap->older = HTab{};
    }
}

static void hm_trigger_rehashing(HMap* hmap) {
    assert(hmap->older.tab == nullptr);
    hmap->older = hmap->newer;                              // demote current table
    h_init(&hmap->newer, (hmap->newer.mask + 1) * 2);      // fresh table, 2x capacity
    hmap->migrate_pos = 0;
}

HNode* hm_lookup(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*)) {
    hm_help_rehashing(hmap);
    HNode** from = h_lookup(&hmap->newer, key, eq);
    if (!from) from = h_lookup(&hmap->older, key, eq);      // MUST check both tables
    return from ? *from : nullptr;
}

void hm_insert(HMap* hmap, HNode* node) {
    if (!hmap->newer.tab) h_init(&hmap->newer, 4);          // lazy first table
    h_insert(&hmap->newer, node);                           // always into newer
    if (!hmap->older.tab) {                                 // not already migrating?
        size_t threshold = (hmap->newer.mask + 1) * k_max_load_factor;
        if (hmap->newer.size >= threshold) hm_trigger_rehashing(hmap);
    }
    hm_help_rehashing(hmap);
}

HNode* hm_delete(HMap* hmap, HNode* key, bool (*eq)(HNode*, HNode*)) {
    hm_help_rehashing(hmap);
    if (HNode** from = h_lookup(&hmap->newer, key, eq)) return h_detach(&hmap->newer, from);
    if (HNode** from = h_lookup(&hmap->older, key, eq)) return h_detach(&hmap->older, from);
    return nullptr;
}

static bool h_foreach(HTab* htab, bool (*f)(HNode*, void*), void* arg) {
    for (size_t i = 0; htab->tab && i <= htab->mask; i++) {
        for (HNode* node = htab->tab[i]; node != nullptr; node = node->next) {
            if (!f(node, arg)) return false;
        }
    }
    return true;
}

void hm_foreach(HMap* hmap, bool (*f)(HNode*, void*), void* arg) {
    if (h_foreach(&hmap->newer, f, arg)) h_foreach(&hmap->older, f, arg);
}

void hm_clear(HMap* hmap) {
    free(hmap->newer.tab);
    free(hmap->older.tab);
    *hmap = HMap{};
}

size_t hm_size(const HMap* hmap) {
    return hmap->newer.size + hmap->older.size;
}

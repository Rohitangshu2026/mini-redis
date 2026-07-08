#pragma once

#include "ds/avl.h"
#include "store/hashtable.h"

#include <cstddef>
#include <cstdint>

// Sorted set: a collection of (score, name) pairs kept in sort order.
// One copy of the data, indexed two ways:
//   - by (score, name) through the AVL tree  -> range and rank queries
//   - by name through the hash map           -> O(1) point queries
struct ZSet {
    AVLNode* root = nullptr;
    HMap     hmap;
};

// One pair. Both index nodes are embedded, so a single allocation carries
// the data plus its position in both indexes. The name is stored inline at
// the end of the struct (flexible-array idiom; the real length is `len`).
struct ZNode {
    AVLNode tree;      // (score, name) index hook
    HNode   hmap;      // name index hook
    double  score = 0;
    size_t  len   = 0;
    char    name[1];   // allocated as sizeof(ZNode) + len
};

// Add a pair, or update the score of an existing pair (re-sorting it).
// Returns true if a new pair was created, false if one was updated.
bool zset_insert(ZSet* zset, const char* name, size_t len, double score);

// Point query by name. Returns nullptr if absent.
ZNode* zset_lookup(ZSet* zset, const char* name, size_t len);

// Remove a pair (obtained from lookup/seek) from both indexes and free it.
void zset_delete(ZSet* zset, ZNode* node);

// Range query: the first pair >= (score, name) in tuple order, or nullptr.
ZNode* zset_seekge(ZSet* zset, double score, const char* name, size_t len);

// Rank walk: the pair `offset` positions away in sort order (negative walks
// backward). O(log n) regardless of distance. nullptr if out of range.
ZNode* znode_offset(ZNode* node, int64_t offset);

// Free every pair and both index structures.
void zset_clear(ZSet* zset);

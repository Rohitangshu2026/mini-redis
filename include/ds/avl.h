#pragma once

#include <cstdint>

// Intrusive AVL tree node — embed inside your own struct and recover the
// container with container_of. The tree stores no keys itself; ordering is
// decided by the caller at insert time (walk + link), and the tree only
// maintains balance and subtree sizes.
//
// height: subtree height, used for rebalancing decisions.
// cnt:    subtree node count, which is what makes O(log n) rank queries
//         (avl_offset) possible.
struct AVLNode{
    AVLNode* parent = nullptr;
    AVLNode* left   = nullptr;
    AVLNode* right  = nullptr;
    uint32_t height = 1;
    uint32_t cnt    = 1;
};

inline void avl_init(AVLNode* node){
    node->parent = node->left = node->right = nullptr;
    node->height = 1;
    node->cnt    = 1;
}

// Subtree accessors that tolerate null (empty subtree).
inline uint32_t avl_height(const AVLNode* node){ return node ? node->height : 0; }
inline uint32_t avl_cnt(const AVLNode* node)    { return node ? node->cnt : 0; }

// Rebalance from a freshly linked/unlinked node up to the root.
// Returns the (possibly new) root of the whole tree.
AVLNode* avl_fix(AVLNode* node);

// Detach a node from the tree it belongs to.
// Returns the (possibly new) root of the whole tree; nullptr if it emptied.
AVLNode* avl_del(AVLNode* node);

// Walk `offset` positions through the in-order sequence relative to `node`
// (positive = successors, negative = predecessors) in O(log n) using the
// subtree counts. Returns nullptr if the target rank is out of range.
AVLNode* avl_offset(AVLNode* node, int64_t offset);

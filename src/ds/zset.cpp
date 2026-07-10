#include "ds/zset.h"
#include "common/hash.h"
#include "common/intrusive.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

// ---- node lifetime ----
// The name is stored inline after the struct, so allocation goes through
// malloc with a computed size; placement-new runs the member initializers.
static ZNode* znode_new(const char* name, size_t len, double score){
    ZNode* node = static_cast<ZNode*>(malloc(sizeof(ZNode) + len));
    assert(node);
    new (node) ZNode();
    avl_init(&node->tree);
    node->hmap.hcode = str_hash((const uint8_t*)name, len);
    node->score = score;
    node->len   = len;
    memcpy(node->name, name, len);
    return node;
}

static void znode_del(ZNode* node){
    node->~ZNode();
    free(node);
}

// ---- tuple comparison: (score, name) with memcmp tie-break ----
static bool zless(AVLNode* lhs, double score, const char* name, size_t len){
    ZNode* zl = container_of(lhs, ZNode, tree);
    if(zl->score != score){
        return zl->score < score;
    }
    int rv = memcmp(zl->name, name, std::min(zl->len, len));
    if(rv != 0){
        return rv < 0;
    }
    return zl->len < len;   // prefix compares equal: shorter name sorts first
}

static bool zless(AVLNode* lhs, AVLNode* rhs){
    ZNode* zr = container_of(rhs, ZNode, tree);
    return zless(lhs, zr->score, zr->name, zr->len);
}

// ---- name index (hash map) ----
// Stack-allocated probe for hash lookups by raw name bytes.
struct HKey{
    HNode       node;
    const char* name = nullptr;
    size_t      len  = 0;
};

static bool hcmp(HNode* node, HNode* key){
    ZNode* znode = container_of(node, ZNode, hmap);
    HKey*  hkey  = container_of(key, HKey, node);
    if(znode->len != hkey->len){
        return false;
    }
    return 0 == memcmp(znode->name, hkey->name, znode->len);
}

ZNode* zset_lookup(ZSet* zset, const char* name, size_t len){
    if(!zset->root){
        return nullptr;
    }
    HKey key;
    key.node.hcode = str_hash((const uint8_t*)name, len);
    key.name = name;
    key.len  = len;
    HNode* found = hm_lookup(&zset->hmap, &key.node, &hcmp);
    return found ? container_of(found, ZNode, hmap) : nullptr;
}

// ---- score index (AVL tree) ----
static void tree_insert(ZSet* zset, ZNode* node){
    AVLNode*  parent = nullptr;
    AVLNode** from   = &zset->root;
    while(*from){                 // walk down to an empty link
        parent = *from;
        from = zless(&node->tree, parent) ? &parent->left : &parent->right;
    }
    *from = &node->tree;
    node->tree.parent = parent;
    zset->root = avl_fix(&node->tree);
}

// A score change can move the pair anywhere in the order, so the tree node
// is detached and re-inserted; the hash index is untouched (name unchanged).
static void zset_update(ZSet* zset, ZNode* node, double score){
    if(node->score == score){
        return;
    }
    zset->root = avl_del(&node->tree);
    avl_init(&node->tree);
    node->score = score;
    tree_insert(zset, node);
}

bool zset_insert(ZSet* zset, const char* name, size_t len, double score){
    if(ZNode* node = zset_lookup(zset, name, len)){
        zset_update(zset, node, score);
        return false;               // updated an existing pair
    }
    ZNode* node = znode_new(name, len, score);
    hm_insert(&zset->hmap, &node->hmap);
    tree_insert(zset, node);
    return true;                    // created a new pair
}

void zset_delete(ZSet* zset, ZNode* node){
    HKey key;                       // detach from the name index
    key.node.hcode = node->hmap.hcode;
    key.name = node->name;
    key.len  = node->len;
    HNode* found = hm_delete(&zset->hmap, &key.node, &hcmp);
    assert(found);
    (void)found;
    zset->root = avl_del(&node->tree);
    znode_del(node);
}

ZNode* zset_seekge(ZSet* zset, double score, const char* name, size_t len){
    AVLNode* found = nullptr;
    for(AVLNode* node = zset->root; node;){
        if(zless(node, score, name, len)){
            node = node->right;     // node < key: everything left is smaller
        }else{
            found = node;           // candidate; keep looking for a smaller one
            node = node->left;
        }
    }
    return found ? container_of(found, ZNode, tree) : nullptr;
}

ZNode* znode_offset(ZNode* node, int64_t offset){
    AVLNode* tnode = node ? avl_offset(&node->tree, offset) : nullptr;
    return tnode ? container_of(tnode, ZNode, tree) : nullptr;
}

static void tree_dispose(AVLNode* node){
    if(!node){
        return;
    }
    tree_dispose(node->left);
    tree_dispose(node->right);
    znode_del(container_of(node, ZNode, tree));
}

void zset_clear(ZSet* zset){
    hm_clear(&zset->hmap);          // frees bucket arrays; nodes freed below
    tree_dispose(zset->root);
    zset->root = nullptr;
}

#include <catch2/catch_test_macros.hpp>

#include "ds/avl.h"
#include "common/intrusive.h"

#include <cstdint>
#include <random>
#include <set>
#include <vector>

namespace{

struct Data{
    AVLNode  node;
    uint32_t val = 0;
};

struct Tree{
    AVLNode* root = nullptr;
};

void add(Tree& t, uint32_t val){
    Data* data = new Data();
    avl_init(&data->node);
    data->val = val;

    AVLNode*  cur  = nullptr;                 // standard BST insert...
    AVLNode** from = &t.root;
    while(*from){
        cur = *from;
        uint32_t node_val = container_of(cur, Data, node)->val;
        from = (val < node_val) ? &cur->left : &cur->right;
    }
    *from = &data->node;
    data->node.parent = cur;
    t.root = avl_fix(&data->node);            // ...then rebalance up the path
}

bool del(Tree& t, uint32_t val){
    AVLNode* cur = t.root;
    while(cur){
        uint32_t node_val = container_of(cur, Data, node)->val;
        if(val == node_val) break;
        cur = (val < node_val) ? cur->left : cur->right;
    }
    if(!cur) return false;
    t.root = avl_del(cur);
    delete container_of(cur, Data, node);
    return true;
}

void dispose(AVLNode* node){
    if(!node) return;
    dispose(node->left);
    dispose(node->right);
    delete container_of(node, Data, node);
}

// Recursively validate every structural invariant; collects in-order values.
// Returns false on the first violation instead of asserting, so the caller
// can REQUIRE once (keeps 100k-op runs fast).
bool verify_node(AVLNode* parent, AVLNode* node, std::vector<uint32_t>& inorder){
    if(!node) return true;
    if(node->parent != parent) return false;

    if(!verify_node(node, node->left, inorder)) return false;
    inorder.push_back(container_of(node, Data, node)->val);
    if(!verify_node(node, node->right, inorder)) return false;

    uint32_t lh = avl_height(node->left), rh = avl_height(node->right);
    if(node->height != 1 + (lh > rh ? lh : rh)) return false;         // height
    if(node->cnt != 1 + avl_cnt(node->left) + avl_cnt(node->right)) return false; // size
    if(lh > rh + 1 || rh > lh + 1) return false;                      // balance

    uint32_t v = container_of(node, Data, node)->val;
    if(node->left  && container_of(node->left,  Data, node)->val > v) return false;
    if(node->right && container_of(node->right, Data, node)->val < v) return false;
    return true;
}

// Full check: structure + exact content match against the oracle.
bool verify(const Tree& t, const std::multiset<uint32_t>& oracle){
    std::vector<uint32_t> inorder;
    if(!verify_node(nullptr, t.root, inorder)) return false;
    if(inorder.size() != oracle.size()) return false;
    return std::equal(inorder.begin(), inorder.end(), oracle.begin());
}

} // namespace

TEST_CASE("avl: sequential inserts stay balanced", "[avl]"){
    Tree t;
    std::multiset<uint32_t> oracle;
    for(uint32_t i = 0; i < 1000; i++){     // ascending = worst case for a plain BST
        add(t, i);
        oracle.insert(i);
    }
    REQUIRE(verify(t, oracle));
    // 1000 nodes in a valid AVL tree can't be taller than ~1.44*log2(n)
    REQUIRE(t.root->height <= 15);
    dispose(t.root);
}

TEST_CASE("avl: deleting a node with two children (successor transplant)", "[avl]"){
    Tree t;
    std::multiset<uint32_t> oracle;
    for(uint32_t v : {50, 30, 70, 20, 40, 60, 80}){
        add(t, v);
        oracle.insert(v);
    }
    REQUIRE(del(t, 50));                      // root, two children
    oracle.erase(50);
    REQUIRE(verify(t, oracle));
    REQUIRE(del(t, 30));                      // internal, two children
    oracle.erase(30);
    REQUIRE(verify(t, oracle));
    dispose(t.root);
}

TEST_CASE("avl: empty and single-node edge cases", "[avl]"){
    Tree t;
    add(t, 7);
    REQUIRE(t.root != nullptr);
    REQUIRE(t.root->cnt == 1);
    REQUIRE(del(t, 7));
    REQUIRE(t.root == nullptr);               // tree empties cleanly
    REQUIRE_FALSE(del(t, 7));                 // deleting from empty fails
}

TEST_CASE("avl: 100k random ops match a multiset oracle", "[avl][property]"){
    Tree t;
    std::multiset<uint32_t> oracle;
    std::mt19937 rng(42);                     // seeded: failures reproduce

    bool ok = true;
    for(int i = 0; i < 100000 && ok; i++){
        uint32_t val = rng() % 3000;          // small key space forces churn + dupes
        if(rng() % 2 == 0){
            add(t, val);
            oracle.insert(val);
        }else{
            bool tree_had = del(t, val);
            auto it = oracle.find(val);
            if(tree_had != (it != oracle.end())){ ok = false; break; }
            if(it != oracle.end()) oracle.erase(it);
        }
        if(i % 1000 == 999) ok = verify(t, oracle);   // full audit every 1k ops
    }
    REQUIRE(ok);
    REQUIRE(verify(t, oracle));
    dispose(t.root);
}

TEST_CASE("avl: offset walks the in-order sequence by rank", "[avl]"){
    const int N = 200;
    Tree t;
    for(int i = 0; i < N; i++) add(t, (uint32_t)i);

    AVLNode* min = t.root;
    while(min->left) min = min->left;

    // From every position, jump to every other position; O(log n) each.
    for(int i = 0; i < N; i++){
        AVLNode* a = avl_offset(min, i);
        REQUIRE(a != nullptr);
        REQUIRE(container_of(a, Data, node)->val == (uint32_t)i);
        for(int j = 0; j < N; j += 17){     // sampled cross-jumps
            AVLNode* b = avl_offset(a, j - i);
            REQUIRE(b != nullptr);
            REQUIRE(container_of(b, Data, node)->val == (uint32_t)j);
        }
    }
    REQUIRE(avl_offset(min, N) == nullptr);   // one past the end
    REQUIRE(avl_offset(min, -1) == nullptr);  // one before the start
    dispose(t.root);
}

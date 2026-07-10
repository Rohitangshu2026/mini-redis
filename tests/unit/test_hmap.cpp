#include <catch2/catch_test_macros.hpp>

#include "store/hashtable.h"
#include "common/intrusive.h"

#include <vector>
#include <cstdint>

namespace{

struct TNode{ HNode node; uint64_t key; };

bool teq(HNode* a, HNode* b){
    return container_of(a, TNode, node)->key == container_of(b, TNode, node)->key;
}
uint64_t mix(uint64_t x){                 // cheap 64-bit hash for the test keys
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33;
    return x;
}
TNode* mk(uint64_t k){
    TNode* n = new TNode();
    n->key = k; n->node.hcode = mix(k);
    return n;
}
TNode* find(HMap& m, uint64_t k){
    TNode probe; probe.key = k; probe.node.hcode = mix(k);
    HNode* f = hm_lookup(&m, &probe.node, &teq);
    return f ? container_of(f, TNode, node) : nullptr;
}

} // namespace

TEST_CASE("hmap: insert / lookup / delete 10k keys", "[hmap]"){
    HMap m{};
    const int N = 10000;
    for(int i = 0; i < N; i++) hm_insert(&m, &mk(i)->node);
    REQUIRE(hm_size(&m) == (size_t)N);

    for(int i = 0; i < N; i++){
        TNode* t = find(m, i);
        REQUIRE(t != nullptr);
        REQUIRE(t->key == (uint64_t)i);
    }
    REQUIRE(find(m, N + 7) == nullptr);            // absent key

    for(int i = 0; i < N; i++){
        TNode probe; probe.key = i; probe.node.hcode = mix(i);
        HNode* d = hm_delete(&m, &probe.node, &teq);
        REQUIRE(d != nullptr);
        delete container_of(d, TNode, node);
    }
    REQUIRE(hm_size(&m) == 0);
    hm_clear(&m);
}

TEST_CASE("hmap: 1M keys all survive progressive rehashing", "[hmap][stress]"){
    HMap m{};
    const int N = 1'000'000;
    std::vector<TNode*> nodes; nodes.reserve(N);
    for(int i = 0; i < N; i++){ TNode* n = mk(i); nodes.push_back(n); hm_insert(&m, &n->node); }
    REQUIRE(hm_size(&m) == (size_t)N);

    int missing = 0;                                // the key-loss bug would show up here
    for(int i = 0; i < N; i++) if(!find(m, i)) missing++;
    REQUIRE(missing == 0);

    for(TNode* n : nodes) delete n;                // free objects, then table arrays
    hm_clear(&m);
}

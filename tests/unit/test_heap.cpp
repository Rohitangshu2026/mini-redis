#include <catch2/catch_test_macros.hpp>

#include "ds/heap.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace{

// Stand-in for Entry: something that stores its own heap index.
struct Owner{
    size_t idx = (size_t)-1;
};

// Both heap invariants: parent <= children, and every item's back-pointer
// names its actual position.
bool valid(const std::vector<HeapItem>& a){
    for(size_t i = 0; i < a.size(); i++){
        if(i > 0 && a[(i + 1) / 2 - 1].val > a[i].val) return false;
        if(*a[i].ref != i) return false;
    }
    return true;
}

void insert(std::vector<HeapItem>& a, Owner& o, uint64_t val){
    heap_upsert(a, o.idx, HeapItem{val, &o.idx});
}

} // namespace

TEST_CASE("heap: inserts keep the minimum at the root", "[heap]"){
    std::vector<HeapItem> a;
    std::vector<Owner> owners(100);
    std::mt19937 rng(1);

    uint64_t min_val = UINT64_MAX;
    for(auto& o : owners){
        uint64_t v = rng() % 100000;
        insert(a, o, v);
        min_val = std::min(min_val, v);
        REQUIRE(valid(a));
    }
    REQUIRE(a.size() == owners.size());
    REQUIRE(a[0].val == min_val);
}

TEST_CASE("heap: updating a value moves it up or down", "[heap]"){
    std::vector<HeapItem> a;
    std::vector<Owner> owners(50);
    std::mt19937 rng(2);
    for(auto& o : owners) insert(a, o, 1000 + rng() % 1000);

    insert(a, owners[30], 1);               // now the smallest: must bubble up
    REQUIRE(valid(a));
    REQUIRE(a[0].ref == &owners[30].idx);
    REQUIRE(owners[30].idx == 0);

    insert(a, owners[30], 999999);          // now the largest: must sink down
    REQUIRE(valid(a));
    REQUIRE(a[0].ref != &owners[30].idx);
    REQUIRE(a.size() == owners.size());     // updates never change the count
}

TEST_CASE("heap: repeatedly deleting the root drains in sorted order", "[heap]"){
    std::vector<HeapItem> a;
    std::vector<Owner> owners(200);
    std::mt19937 rng(3);

    std::vector<uint64_t> vals;
    for(auto& o : owners){
        uint64_t v = rng() % 5000;          // duplicates likely
        vals.push_back(v);
        insert(a, o, v);
    }
    std::sort(vals.begin(), vals.end());

    std::vector<uint64_t> drained;
    while(!a.empty()){
        drained.push_back(a[0].val);
        heap_delete(a, 0);
        REQUIRE(valid(a));
    }
    REQUIRE(drained == vals);               // a heap IS a sorting machine
}

TEST_CASE("heap: 5k random upserts and deletes hold both invariants", "[heap][property]"){
    std::vector<HeapItem> a;
    std::vector<Owner> owners(300);         // fixed addresses; may or may not be in the heap
    std::mt19937 rng(4);

    bool ok = true;
    size_t expected = 0;
    for(int i = 0; i < 5000 && ok; i++){
        Owner& o = owners[rng() % owners.size()];
        if(rng() % 3 != 0){               // upsert
            if(o.idx == (size_t)-1) expected++;
            insert(a, o, rng() % 100000);
        } else if(o.idx != (size_t)-1){   // delete via the owner's index
            heap_delete(a, o.idx);
            o.idx = (size_t)-1;
            expected--;
        }
        ok = valid(a) && a.size() == expected;
    }
    REQUIRE(ok);
}

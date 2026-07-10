#include <catch2/catch_test_macros.hpp>

#include "ds/zset.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace{

const double NEG_INF = -std::numeric_limits<double>::infinity();

bool zadd(ZSet& z, const std::string& name, double score){
    return zset_insert(&z, name.data(), name.size(), score);
}
ZNode* zfind(ZSet& z, const std::string& name){
    return zset_lookup(&z, name.data(), name.size());
}
std::string zname(const ZNode* n){
    return std::string(n->name, n->len);
}

// Walk the whole set in sort order via seek + rank steps.
std::vector<std::pair<double, std::string>> walk(ZSet& z){
    std::vector<std::pair<double, std::string>> out;
    ZNode* node = zset_seekge(&z, NEG_INF, "", 0);
    while(node){
        out.emplace_back(node->score, zname(node));
        node = znode_offset(node, +1);
    }
    return out;
}

// Exact content match against a (score, name)-ordered oracle.
bool matches(ZSet& z, const std::set<std::pair<double, std::string>>& oracle){
    auto got = walk(z);
    if(got.size() != oracle.size()) return false;
    return std::equal(got.begin(), got.end(), oracle.begin());
}

} // namespace

TEST_CASE("zset: insert, update, and point lookup by name", "[zset]"){
    ZSet z;
    REQUIRE(zadd(z, "alice", 100));         // new pair
    REQUIRE(zadd(z, "bob", 200));
    REQUIRE_FALSE(zadd(z, "alice", 150));   // same name: update, not insert

    ZNode* a = zfind(z, "alice");
    REQUIRE(a != nullptr);
    REQUIRE(a->score == 150);
    REQUIRE(zfind(z, "carol") == nullptr);

    // The score update must have re-sorted the tree index too.
    auto got = walk(z);
    REQUIRE(got.size() == 2);
    REQUIRE(got[0].second == "alice");
    REQUIRE(got[1].second == "bob");
    zset_clear(&z);
}

TEST_CASE("zset: delete removes a pair from both indexes", "[zset]"){
    ZSet z;
    zadd(z, "a", 1);
    zadd(z, "b", 2);
    zadd(z, "c", 3);

    zset_delete(&z, zfind(z, "b"));
    REQUIRE(zfind(z, "b") == nullptr);      // gone from the name index
    auto got = walk(z);                     // gone from the score index
    REQUIRE(got.size() == 2);
    REQUIRE(got[0].second == "a");
    REQUIRE(got[1].second == "c");
    zset_clear(&z);
}

TEST_CASE("zset: equal scores order by name; seekge lands on tuple bounds", "[zset]"){
    ZSet z;
    for(const char* n : {"delta", "alpha", "charlie", "bravo"}) zadd(z, n, 7);

    auto got = walk(z);                     // same score: lexicographic by name
    REQUIRE(got[0].second == "alpha");
    REQUIRE(got[1].second == "bravo");
    REQUIRE(got[2].second == "charlie");
    REQUIRE(got[3].second == "delta");

    ZNode* n = zset_seekge(&z, 7, "bravo", 5);      // exact hit
    REQUIRE(zname(n) == "bravo");
    n = zset_seekge(&z, 7, "bravoo", 6);            // between bravo and charlie
    REQUIRE(zname(n) == "charlie");
    n = zset_seekge(&z, 7.5, "", 0);                // past every score
    REQUIRE(n == nullptr);
    n = zset_seekge(&z, 6.5, "zzz", 3);             // before every score
    REQUIRE(zname(n) == "alpha");
    zset_clear(&z);
}

TEST_CASE("zset: rank offsets jump both directions in O(log n)", "[zset]"){
    const int N = 100;
    ZSet z;
    char buf[16];
    for(int i = 0; i < N; i++){
        std::snprintf(buf, sizeof(buf), "m%03d", i);
        zadd(z, buf, i);
    }

    ZNode* first = zset_seekge(&z, NEG_INF, "", 0);
    REQUIRE(first->score == 0);
    for(int i = 0; i < N; i += 7){
        ZNode* n = znode_offset(first, i);
        REQUIRE(n != nullptr);
        REQUIRE(n->score == i);
        ZNode* back = znode_offset(n, -i);          // and walk back again
        REQUIRE(back == first);
    }
    REQUIRE(znode_offset(first, N) == nullptr);     // one past the end
    REQUIRE(znode_offset(first, -1) == nullptr);    // one before the start
    zset_clear(&z);
}

TEST_CASE("zset: 10k random ops match a two-index oracle", "[zset][property]"){
    ZSet z;
    std::map<std::string, double> by_name;              // oracle index 1
    std::set<std::pair<double, std::string>> by_score;  // oracle index 2
    std::mt19937 rng(7);                                // seeded: reproducible

    char buf[16];
    bool ok = true;
    for(int i = 0; i < 10000 && ok; i++){
        std::snprintf(buf, sizeof(buf), "n%03d", (int)(rng() % 300));
        std::string name = buf;
        double score = (double)(int)(rng() % 400) / 4.0;    // exact in binary

        uint32_t action = rng() % 10;
        if(action < 6){                       // insert or update
            bool added = zadd(z, name, score);
            auto it = by_name.find(name);
            if(added != (it == by_name.end())){ ok = false; break; }
            if(it != by_name.end()) by_score.erase({it->second, name});
            by_name[name] = score;
            by_score.insert({score, name});
        } else if(action < 9){                // delete by name
            ZNode* node = zfind(z, name);
            auto it = by_name.find(name);
            if((node != nullptr) != (it != by_name.end())){ ok = false; break; }
            if(node){
                zset_delete(&z, node);
                by_score.erase({it->second, name});
                by_name.erase(it);
            }
        }else{                                // point lookup
            ZNode* node = zfind(z, name);
            auto it = by_name.find(name);
            if((node != nullptr) != (it != by_name.end())){ ok = false; break; }
            if(node && node->score != it->second){ ok = false; break; }
        }

        if(i % 500 == 499) ok = matches(z, by_score);  // full audit
    }
    REQUIRE(ok);
    REQUIRE(matches(z, by_score));
    zset_clear(&z);
}

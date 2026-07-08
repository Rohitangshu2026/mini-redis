#include <catch2/catch_test_macros.hpp>

#include "ds/dlist.h"

#include <vector>

namespace {

// Collect the list contents (as indexes into `nodes`) by walking forward.
std::vector<int> order(const DList& head, const DList* nodes, int n) {
    std::vector<int> out;
    for (const DList* p = head.next; p != &head; p = p->next) {
        for (int i = 0; i < n; i++) {
            if (p == &nodes[i]) out.push_back(i);
        }
    }
    return out;
}

} // namespace

TEST_CASE("dlist: a fresh head is empty and self-linked", "[dlist]") {
    DList head;
    dlist_init(&head);
    REQUIRE(dlist_empty(&head));
    REQUIRE(head.next == &head);
    REQUIRE(head.prev == &head);
}

TEST_CASE("dlist: insert_before the head appends in FIFO order", "[dlist]") {
    DList head;
    dlist_init(&head);
    DList n[3];
    for (auto& x : n) dlist_insert_before(&head, &x);

    REQUIRE_FALSE(dlist_empty(&head));
    REQUIRE(order(head, n, 3) == std::vector<int>{0, 1, 2});
    // backward links mirror the forward walk
    REQUIRE(head.prev == &n[2]);
    REQUIRE(n[2].prev == &n[1]);
    REQUIRE(n[0].prev == &head);
}

TEST_CASE("dlist: detach relinks neighbors and the node is reusable", "[dlist]") {
    DList head;
    dlist_init(&head);
    DList n[3];
    for (auto& x : n) dlist_insert_before(&head, &x);

    dlist_detach(&n[1]);                       // pull from the middle
    REQUIRE(order(head, n, 3) == std::vector<int>{0, 2});

    dlist_detach(&n[1]);                       // double-detach is harmless
    REQUIRE(order(head, n, 3) == std::vector<int>{0, 2});

    dlist_insert_before(&head, &n[1]);         // move-to-back pattern
    REQUIRE(order(head, n, 3) == std::vector<int>{0, 2, 1});

    dlist_detach(&n[0]);
    dlist_detach(&n[2]);
    dlist_detach(&n[1]);
    REQUIRE(dlist_empty(&head));
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Array-encoded binary min-heap for timers. Two invariants:
//   1. a node's value is <= both of its children (min at index 0)
//   2. every level is full except possibly the last (array encoding)
//
// Unlike the idle-connection list, TTLs are arbitrary values, so a real
// sorting structure is needed; a heap finds the nearest deadline in O(1)
// and updates in O(log n) with no per-node allocations.
//
// Each item back-points to its owner's stored index (e.g. Entry::heap_idx).
// Every move re-writes *ref, so the owner always knows where its timer
// lives and can update or remove it directly.
struct HeapItem{
    uint64_t val = 0;          // sort key: expiration time, monotonic ms
    size_t*  ref = nullptr;    // -> the owner's index field
};

// Restore invariant 1 after a[pos] changed (moves it up or down).
void heap_update(HeapItem* a, size_t pos, size_t len);

// Update the item at pos, or append when pos is out of range ((size_t)-1
// for "no item yet"), then re-heapify.
void heap_upsert(std::vector<HeapItem>& a, size_t pos, HeapItem t);

// Remove the item at pos: O(1) swap with the tail plus one re-heapify.
void heap_delete(std::vector<HeapItem>& a, size_t pos);

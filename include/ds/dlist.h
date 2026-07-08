#pragma once

// Intrusive circular doubly-linked list. The list head is a dummy node
// linked to itself, so insertion and removal never special-case an empty
// list. Embed a DList node in your struct and recover it via container_of.
struct DList {
    DList* prev = nullptr;
    DList* next = nullptr;
};

inline void dlist_init(DList* node) {
    node->prev = node->next = node;
}

inline bool dlist_empty(const DList* node) {
    return node->next == node;
}

// Unlink a node and re-initialize it, so detaching twice is harmless.
inline void dlist_detach(DList* node) {
    DList* prev = node->prev;
    DList* next = node->next;
    prev->next = next;
    next->prev = prev;
    dlist_init(node);
}

// Insert `rookie` immediately before `target`. Inserting before the dummy
// head appends to the back of the list.
inline void dlist_insert_before(DList* target, DList* rookie) {
    DList* prev = target->prev;
    prev->next   = rookie;
    rookie->prev = prev;
    rookie->next = target;
    target->prev = rookie;
}

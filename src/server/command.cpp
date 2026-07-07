#include "server/command.h"
#include "store/entry.h"
#include "common/intrusive.h"
#include "protocol/serialize.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// FNV-1a string hash (matches the book).
static uint64_t str_hash(const uint8_t* data, size_t len) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < len; i++) h = (h + data[i]) * 0x01000193u;
    return h;
}

static bool entry_eq(HNode* a, HNode* b) {
    return container_of(a, Entry, node)->key == container_of(b, Entry, node)->key;
}

static std::string to_upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// A stack "probe" carrying just the key + its hash, for lookup/delete.
static Entry make_probe(const std::string& key) {
    Entry e;
    e.key = key;
    e.node.hcode = str_hash((const uint8_t*)key.data(), key.size());
    return e;
}

// KEYS callback: append each key as a serialized string.
static bool keys_cb(HNode* node, void* arg) {
    auto* out = static_cast<std::vector<uint8_t>*>(arg);
    out_str(*out, container_of(node, Entry, node)->key);
    return true;
}

void do_request(const std::vector<std::string>& cmd, HMap& db, std::vector<uint8_t>& out) {
    if (cmd.empty()) { out_err(out, ERR_ARG, "empty command"); return; }
    const std::string op = to_upper(cmd[0]);

    if (op == "GET" && cmd.size() == 2) {
        Entry probe = make_probe(cmd[1]);
        HNode* found = hm_lookup(&db, &probe.node, &entry_eq);
        if (!found) { out_nil(out); return; }
        out_str(out, container_of(found, Entry, node)->val);
    }
    else if (op == "SET" && cmd.size() == 3) {
        Entry probe = make_probe(cmd[1]);
        HNode* found = hm_lookup(&db, &probe.node, &entry_eq);
        if (found) {
            container_of(found, Entry, node)->val = cmd[2];   // overwrite existing
        } else {
            Entry* ent = new Entry();
            ent->key = cmd[1];
            ent->val = cmd[2];
            ent->node.hcode = probe.node.hcode;
            hm_insert(&db, &ent->node);
        }
        out_nil(out);                                          // book returns nil for SET
    }
    else if (op == "DEL" && cmd.size() == 2) {
        Entry probe = make_probe(cmd[1]);
        HNode* removed = hm_delete(&db, &probe.node, &entry_eq);
        if (removed) delete container_of(removed, Entry, node);
        out_int(out, removed ? 1 : 0);                         // count removed
    }
    else if (op == "KEYS" && cmd.size() == 1) {
        out_arr(out, (uint32_t)hm_size(&db));                  // array header, then one str per key
        hm_foreach(&db, keys_cb, &out);
    }
    else {
        out_err(out, ERR_UNKNOWN, "unknown command");
    }
}

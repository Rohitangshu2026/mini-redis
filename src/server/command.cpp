// Command execution: one function per verb, plus the dispatch table at the
// bottom. Shared conventions across every handler:
//   - cmd[0] is the verb (case-insensitive), cmd[1] is the key
//   - the reply is exactly one serialized typed value appended to `out`
//   - a key holding the wrong value type answers with the WRONGTYPE error
//   - lookups go through lookup_entry(), which also expires overdue keys,
//     so no handler ever observes a key past its TTL deadline
// The dispatcher checks arity before calling, so handlers can index cmd[]
// without re-validating argument counts.

#include "server/command.h"
#include "store/entry.h"
#include "ds/zset.h"
#include "common/hash.h"
#include "common/intrusive.h"
#include "common/clock.h"
#include "protocol/serialize.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

static const char* WRONGTYPE_MSG =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

static bool entry_eq(HNode* a, HNode* b){
    return container_of(a, Entry, node)->key == container_of(b, Entry, node)->key;
}

static std::string to_upper(std::string s){
    for(char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// A stack "probe" carrying just the key + its hash, for lookups.
static Entry make_probe(const std::string& key){
    Entry e;
    e.key = key;
    e.node.hcode = str_hash((const uint8_t*)key.data(), key.size());
    return e;
}

// Look up a key, expiring it lazily: a key whose deadline has passed is
// deleted on sight and reported as absent, even if the active reaper
// hasn't gotten to it yet.
static Entry* lookup_entry(Database& db, const std::string& key){
    Entry probe = make_probe(key);
    HNode* found = hm_lookup(&db.map, &probe.node, &entry_eq);
    if(!found) return nullptr;
    Entry* ent = container_of(found, Entry, node);
    if(ent->heap_idx != (size_t)-1 && db.ttl[ent->heap_idx].val <= get_monotonic_msec()){
        hm_delete(&db.map, &ent->node, &entry_eq);
        entry_del(db, ent);
        return nullptr;
    }
    return ent;
}

// Strict numeric parsers: the whole argument must be consumed.
static bool str2dbl(const std::string& s, double& out){
    if(s.empty()) return false;
    char* end = nullptr;
    out = strtod(s.c_str(), &end);
    return *end == '\0' && !std::isnan(out);
}

static bool str2int(const std::string& s, int64_t& out){
    if(s.empty()) return false;
    char* end = nullptr;
    out = strtoll(s.c_str(), &end, 10);
    return *end == '\0';
}

// ---- string commands ----

static void do_get(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    Entry* ent = lookup_entry(db, cmd[1]);
    if(!ent){ out_nil(out); return; }
    if(ent->type != T_STR){ out_err(out, ERR_TYPE, WRONGTYPE_MSG); return; }
    out_str(out, ent->str);
}

static void do_set(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    Entry* ent = lookup_entry(db, cmd[1]);
    if(ent){
        if(ent->type != T_STR){ out_err(out, ERR_TYPE, WRONGTYPE_MSG); return; }
        ent->str = cmd[2];
        entry_set_ttl(db, ent, -1);     // overwriting a key discards its TTL
    }else{
        ent = new Entry();
        ent->key = cmd[1];
        ent->node.hcode = str_hash((const uint8_t*)cmd[1].data(), cmd[1].size());
        ent->type = T_STR;
        ent->str = cmd[2];
        hm_insert(&db.map, &ent->node);
    }
    out_nil(out);
}

static void do_del(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    Entry* ent = lookup_entry(db, cmd[1]);      // lazily expired keys count as gone
    if(ent){
        hm_delete(&db.map, &ent->node, &entry_eq);
        entry_del(db, ent);
    }
    out_int(out, ent ? 1 : 0);
}

static bool keys_cb(HNode* node, void* arg){
    auto* out = static_cast<std::vector<uint8_t>*>(arg);
    out_str(*out, container_of(node, Entry, node)->key);
    return true;
}

static void do_keys(Database& db, std::vector<uint8_t>& out){
    out_arr(out, (uint32_t)hm_size(&db.map));
    hm_foreach(&db.map, keys_cb, &out);
}

// ---- TTL commands ----

// EXPIRE key seconds · PEXPIRE key milliseconds
// A non-positive TTL means "already due": the key is deleted immediately.
static void do_expire(const std::vector<std::string>& cmd, Database& db,
                      std::vector<uint8_t>& out, int64_t unit_ms){
    int64_t n = 0;
    if(!str2int(cmd[2], n)){
        out_err(out, ERR_ARG, "value is not an integer");
        return;
    }
    Entry* ent = lookup_entry(db, cmd[1]);
    if(!ent){ out_int(out, 0); return; }
    if(n <= 0){
        hm_delete(&db.map, &ent->node, &entry_eq);
        entry_del(db, ent);
        out_int(out, 1);
        return;
    }
    if(n > INT64_MAX / unit_ms) n = INT64_MAX / unit_ms;   // clamp, no overflow
    entry_set_ttl(db, ent, n * unit_ms);
    out_int(out, 1);
}

// TTL key (seconds, rounded up) · PTTL key (milliseconds)
// -2 = no such key, -1 = key exists but has no TTL.
static void do_ttl(const std::vector<std::string>& cmd, Database& db,
                   std::vector<uint8_t>& out, bool in_ms){
    Entry* ent = lookup_entry(db, cmd[1]);
    if(!ent){ out_int(out, -2); return; }
    if(ent->heap_idx == (size_t)-1){ out_int(out, -1); return; }
    uint64_t now = get_monotonic_msec();
    uint64_t due = db.ttl[ent->heap_idx].val;
    uint64_t rem = due > now ? due - now : 0;
    out_int(out, in_ms ? (int64_t)rem : (int64_t)((rem + 999) / 1000));
}

// PERSIST key: drop the TTL. 1 if a TTL was removed, else 0.
static void do_persist(const std::vector<std::string>& cmd, Database& db,
                       std::vector<uint8_t>& out){
    Entry* ent = lookup_entry(db, cmd[1]);
    if(!ent || ent->heap_idx == (size_t)-1){ out_int(out, 0); return; }
    entry_set_ttl(db, ent, -1);
    out_int(out, 1);
}

// ---- sorted-set commands ----

// Resolve a key that must hold a sorted set. Returns the entry, or nullptr
// after emitting an error. A missing key stays nullptr with *missing = true
// so each command can pick its own "empty set" behavior.
static Entry* expect_zset(Database& db, const std::string& key,
                          std::vector<uint8_t>& out, bool* missing){
    Entry* ent = lookup_entry(db, key);
    *missing = (ent == nullptr);
    if(ent && ent->type != T_ZSET){
        out_err(out, ERR_TYPE, WRONGTYPE_MSG);
        return nullptr;
    }
    return ent;
}

// ZADD key score name
static void do_zadd(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    double score = 0;
    if(!str2dbl(cmd[2], score)){
        out_err(out, ERR_ARG, "value is not a valid float");
        return;
    }
    bool missing = false;
    Entry* ent = expect_zset(db, cmd[1], out, &missing);
    if(!ent && !missing) return;               // wrong type, error already sent
    if(!ent){                                 // first pair creates the set
        ent = new Entry();
        ent->key = cmd[1];
        ent->node.hcode = str_hash((const uint8_t*)cmd[1].data(), cmd[1].size());
        ent->type = T_ZSET;
        hm_insert(&db.map, &ent->node);
    }
    bool added = zset_insert(&ent->zset, cmd[3].data(), cmd[3].size(), score);
    out_int(out, added ? 1 : 0);                // 1 = new pair, 0 = score updated
}

// ZREM key name
static void do_zrem(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    bool missing = false;
    Entry* ent = expect_zset(db, cmd[1], out, &missing);
    if(!ent && !missing) return;
    if(!ent){ out_int(out, 0); return; }      // no such set: nothing removed
    ZNode* znode = zset_lookup(&ent->zset, cmd[2].data(), cmd[2].size());
    if(znode) zset_delete(&ent->zset, znode);
    out_int(out, znode ? 1 : 0);
}

// ZSCORE key name
static void do_zscore(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    bool missing = false;
    Entry* ent = expect_zset(db, cmd[1], out, &missing);
    if(!ent && !missing) return;
    if(!ent){ out_nil(out); return; }
    ZNode* znode = zset_lookup(&ent->zset, cmd[2].data(), cmd[2].size());
    if(znode) out_dbl(out, znode->score);
    else       out_nil(out);
}

// ZQUERY key score name offset limit
// Seek to the first pair >= (score, name), walk `offset` positions by rank,
// then emit up to `limit` output elements as [name, score, name, score, ...].
static void do_zquery(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    double score = 0;
    int64_t offset = 0, limit = 0;
    if(!str2dbl(cmd[2], score)){
        out_err(out, ERR_ARG, "value is not a valid float");
        return;
    }
    if(!str2int(cmd[4], offset) || !str2int(cmd[5], limit)){
        out_err(out, ERR_ARG, "value is not an integer");
        return;
    }
    bool missing = false;
    Entry* ent = expect_zset(db, cmd[1], out, &missing);
    if(!ent && !missing) return;
    if(!ent || limit <= 0){ out_arr(out, 0); return; }

    ZNode* znode = zset_seekge(&ent->zset, score, cmd[3].data(), cmd[3].size());
    znode = znode_offset(znode, offset);

    size_t ctx = out_begin_arr(out);            // length patched after the walk
    int64_t n = 0;
    while(znode && n < limit){
        out_str(out, std::string(znode->name, znode->len));
        out_dbl(out, znode->score);
        znode = znode_offset(znode, +1);
        n += 2;
    }
    out_end_arr(out, ctx, (uint32_t)n);
}

// ---- dispatch ----

void do_request(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out){
    if(cmd.empty()){ out_err(out, ERR_ARG, "empty command"); return; }
    const std::string op = to_upper(cmd[0]);

    if      (op == "GET"     && cmd.size() == 2) do_get(cmd, db, out);
    else if(op == "SET"     && cmd.size() == 3) do_set(cmd, db, out);
    else if(op == "DEL"     && cmd.size() == 2) do_del(cmd, db, out);
    else if(op == "KEYS"    && cmd.size() == 1) do_keys(db, out);
    else if(op == "EXPIRE"  && cmd.size() == 3) do_expire(cmd, db, out, 1000);
    else if(op == "PEXPIRE" && cmd.size() == 3) do_expire(cmd, db, out, 1);
    else if(op == "TTL"     && cmd.size() == 2) do_ttl(cmd, db, out, false);
    else if(op == "PTTL"    && cmd.size() == 2) do_ttl(cmd, db, out, true);
    else if(op == "PERSIST" && cmd.size() == 2) do_persist(cmd, db, out);
    else if(op == "ZADD"    && cmd.size() == 4) do_zadd(cmd, db, out);
    else if(op == "ZREM"    && cmd.size() == 3) do_zrem(cmd, db, out);
    else if(op == "ZSCORE"  && cmd.size() == 3) do_zscore(cmd, db, out);
    else if(op == "ZQUERY"  && cmd.size() == 6) do_zquery(cmd, db, out);
    else out_err(out, ERR_UNKNOWN, "unknown command");
}

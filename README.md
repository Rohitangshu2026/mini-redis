# mini-redis

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![build: CMake](https://img.shields.io/badge/build-CMake-informational)
[![CI](https://github.com/Rohitangshu2026/mini-redis/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Rohitangshu2026/mini-redis/actions/workflows/ci.yml)

An in-memory key–value store written from scratch in C++17. It pairs a
single-threaded, `poll()`-driven network server with a custom binary wire
protocol and a hand-written hash table that rehashes incrementally, so a
growing keyspace never causes a latency spike.

It is not a wrapper around existing libraries: the socket layer, the wire
format, the response serializer, and the storage engine are all implemented
directly.

```text
$ ./build/mini-redis-server 6379 &
$ ./build/mini-redis-client set user:1 alice
(nil)
$ ./build/mini-redis-client get user:1
"alice"
$ ./build/mini-redis-client del user:1
(integer) 1
$ ./build/mini-redis-client keys
(array of 0)
```

---

## Contents

- [At a glance](#at-a-glance)
- [System architecture](#system-architecture)
- [Component internals](#component-internals)
- [Class model (UML)](#class-model-uml)
- [Execution flow](#execution-flow)
  - [Request/response lifecycle](#requestresponse-lifecycle)
  - [Connection state machine](#connection-state-machine)
  - [Incremental rehashing](#incremental-rehashing)
- [Wire protocol](#wire-protocol)
- [Commands](#commands)
- [Build, run, test](#build-run-test)
- [Project layout](#project-layout)
- [Design decisions](#design-decisions)
- [Roadmap](#roadmap)

---

## At a glance

| Aspect | Choice |
|---|---|
| Concurrency model | Single-threaded `poll()` event loop + a small worker pool for async teardown of large values |
| Transport | TCP, length-prefixed binary framing |
| Request encoding | `[u32 nstr] ([u32 len][bytes])*` |
| Response encoding | Type-tagged value (nil / string / int / double / error / array) |
| Keyspace | Custom intrusive hash table with incremental rehashing |
| Sorted set | AVL tree + hash map; `O(log n)` range and rank queries |
| Commands | Strings: `GET` `SET` `DEL` `KEYS` · Sorted sets: `ZADD` `ZREM` `ZSCORE` `ZQUERY` |
| Pipelining | Yes — every complete request buffered from one read is served |
| Idle connections | Reaped after an idle timeout (default 30 s), monotonic clock |
| Expiration | Per-key TTL on a timer min-heap; lazy + active expiry |
| Tests | Catch2 suite in CTest; data structures property-tested against reference oracles |
| CI | Linux + macOS, gcc + clang, Debug + Release, ASan/UBSan + TSan |
| Build | CMake; server, client and tests share one static library |

A deeper, forward-looking design document lives in
[`docs/architecture.md`](docs/architecture.md).

---

## System architecture

The server is organized as a thin stack of layers. A byte stream enters at the
event loop, is framed and parsed into a command, dispatched against the
keyspace, and the result is serialized back into the connection's outgoing
buffer.

```mermaid
flowchart LR
    subgraph client["mini-redis-client"]
        CLI["argv / REPL"]
    end

    subgraph server["mini-redis-server (single thread)"]
        direction TB
        POLL["poll() event loop"]
        CONNS["connection table<br/>per-conn read + write buffers"]
        WIRE["wire codec<br/>framing + parse_req"]
        DISP["command dispatch<br/>strings + sorted sets"]
        SER["response serializer<br/>type-tagged values"]
        STORE["keyspace<br/>hash table + incremental rehashing"]

        POLL --> CONNS
        CONNS --> WIRE
        WIRE --> DISP
        DISP --> STORE
        DISP --> SER
        SER --> CONNS
    end

    CLI -->|"TCP: length-prefixed request"| POLL
    SER -.->|"length-prefixed typed reply"| CLI
```

Seen through the usual database decomposition:

| Layer | In mini-redis |
|---|---|
| **Transport** | `poll()` loop, non-blocking sockets, framing, buffered I/O |
| **Query processing** | Command lookup + argument/arity validation (no query language, so this stays thin) |
| **Execution** | Command handlers run directly against the store; the single thread gives per-command atomicity with no locks |
| **Storage** | Hash-table keyspace with incremental rehashing |

---

## Component internals

### Networking (`net/`)
- **`Socket`** — a move-only RAII wrapper that owns a file descriptor and closes
  it on destruction, so listener and client fds cannot leak or be double-closed.
  It also exposes `set_nonblocking()` (via `fcntl`), which every socket in the
  event loop uses.
- **`read_full` / `write_all`** — loop over `read()`/`write()` until the exact
  requested byte count has been transferred, hiding short reads and writes. The
  client uses these for its simple blocking round-trips.

### Wire codec (`protocol/wire.*`)
- **Framing** — every message is a 4-byte little-endian length followed by that
  many payload bytes, capped at `MAX_MSG` (32 KiB) so a single client cannot
  force an unbounded allocation.
- **`parse_req`** — decodes a request payload `[u32 nstr]([u32 len][bytes])*`
  into a vector of argument strings. It validates lengths against the buffer and
  rejects truncated or trailing-garbage input instead of over-reading — the
  behavior the parser edge-case tests pin down.

### Response serialization (`protocol/serialize.*`)
Replies are encoded as a single **type-tagged value** so the client never has to
guess what it received:

| Tag | Type | Encoding after the tag byte |
|----:|------|-----------------------------|
| 0 | nil | — |
| 1 | error | `[u32 code][u32 len][msg]` |
| 2 | string | `[u32 len][bytes]` |
| 3 | int64 | `[i64]` |
| 4 | double | `[f64]` |
| 5 | array | `[u32 count]` then `count` nested values |

Arrays nest, which is what commands returning collections (`KEYS`, and sorted-set
ranges later) rely on.

### Storage engine (`store/`)
- **`HNode` / `HTab` / `HMap`** — an intrusive, open-chained hash table. The node
  (`HNode`) is embedded as the first member of each `Entry` and recovered with
  `container_of`, so there is one allocation per entry and good cache locality.
- **Incremental rehashing** — an `HMap` holds two tables, `newer` and `older`.
  When the load factor is exceeded, the current table is demoted to `older`, a
  table of twice the size becomes `newer`, and every subsequent operation
  migrates a bounded number of buckets (128) from `older` to `newer`. Growth is
  therefore spread across many operations instead of one `O(n)` stop-the-world
  resize. Lookups consult **both** tables while a migration is in flight.
- **`Entry`** — a key and its typed value (string or sorted set), carrying the
  intrusive node. Deleting an entry tears down whatever its value owns,
  including its TTL timer.
- **`Database` + the TTL heap** — the keyspace bundles the hash index with an
  array-encoded **timer min-heap**: `ttl[0]` is always the nearest deadline.
  Arbitrary deadlines need real sorting (unlike the fixed idle timeout, where
  a list suffices), and a heap does it with no per-node allocations. Each heap
  item back-points to its entry's `heap_idx` and every move rewrites that
  index, so a key can update or drop its own timer in `O(log n)`. Expiration
  is **lazy** (an overdue key is deleted on sight at lookup) plus **active**
  (each loop tick reaps due keys, capped at 2,000 per iteration so a mass
  expiry can't stall the loop).

### Data structures (`ds/`)
- **AVL tree** — intrusive, self-balancing, with parent pointers. Every node
  also maintains its **subtree size**, so `avl_offset` can jump an arbitrary
  number of positions through the in-order sequence in `O(log n)` — an order
  statistic tree, which is what makes rank queries cheap.
- **Sorted set (`ZSet`)** — a collection of `(score, name)` pairs stored once
  and indexed twice: a hash map by name (O(1) point lookups) and the AVL tree
  by `(score, name)` tuple order (range scans). Each `ZNode` embeds both index
  hooks plus the name bytes inline, in a single allocation. Score updates
  detach and re-insert the tree node so ordering always holds.

### Thread pool (`threadpool/`)
The one place blocking is allowed. Destroying a sorted set is `O(n)` — freeing
a million pairs on the event loop would stall every other client — so `DEL`
unlinks the key immediately and hands values larger than 1,000 members to a
fixed pool of workers (classic producer/consumer: mutex + condition variable,
with the wait condition re-checked in a loop to survive spurious wakeups and
competing consumers). Tasks are a bare function pointer + argument, so
queueing never allocates, and the producer — the event loop — never blocks.
Small values are freed inline; a context switch costs more than the teardown.
By the time a worker sees an entry it is unlinked from every shared structure,
so workers touch only orphaned data and command atomicity is preserved.
Measured: `DEL` of a 1,000,000-member sorted set round-trips in ~0.07 ms while
concurrent commands keep a sub-0.1 ms p50.

### Server / event loop (`server/`)
- **`Conn`** — one connection: its fd, intent flags (`want_read` / `want_write` /
  `want_close`), its own `incoming` / `outgoing` byte buffers, plus its
  last-activity timestamp and its hook in the idle-timer list.
- **`Server::run`** — builds a `pollfd` set (the listener plus every connection),
  calls `poll()` with a timeout derived from the nearest timer, accepts new
  connections, and drives readable/writable ones.
- **Idle timeouts** — connections live in an intrusive doubly-linked list kept
  in last-active order: any I/O readiness moves the connection to the back
  (O(1)), so expired connections are always at the front. Because every
  connection shares one timeout value, a list is all the sorting needed — the
  nearest expiry — the smaller of the idle list's front and the TTL heap's
  root — feeds the `poll()` timeout, and each loop iteration reaps whatever
  is due. Timestamps come from the monotonic clock, which can't jump the way
  wall time can.
- **`try_one_request`** — pulls one complete frame from `incoming`, parses and
  dispatches it, and appends the framed, serialized reply to `outgoing`.
  `handle_read` calls it in a loop, so several requests received in one read are
  processed back to back (**pipelining**).
- **Command dispatch** — `do_request` upper-cases the verb, validates arity and
  the key's value type (mismatches get a `WRONGTYPE` error), and routes to the
  string or sorted-set handlers, writing a typed value into the response buffer.

### Client (`client/`)
Sends a command either from `argv` (one-shot) or interactively, encodes it in the
request framing, and pretty-prints the typed reply — strings quoted, integers and
errors labelled, array elements indented.

---

## Class model (UML)

```mermaid
classDiagram
    class Socket {
        -int fd_
        +fd() int
        +valid() bool
        +release() int
        +reset(int)
        +set_nonblocking()
    }
    class Conn {
        +int fd
        +bool want_read
        +bool want_write
        +bool want_close
        +bytes incoming
        +bytes outgoing
    }
    class Server {
        -Socket listen_sock_
        -HMap db_
        +run()
        -accept_new()
        -handle_read(Conn)
        -handle_write(Conn)
        -try_one_request(Conn) bool
    }
    class HMap {
        +HTab newer
        +HTab older
        +size_t migrate_pos
    }
    class HTab {
        +size_t mask
        +size_t size
    }
    class HNode {
        +uint64_t hcode
        +HNode next
    }
    class Entry {
        +HNode node
        +string key
        +string val
    }

    Server o-- Socket : owns listener
    Server "1" o-- "0..*" Conn : tracks
    Server *-- HMap : keyspace
    HMap *-- "2" HTab : newer + older
    HTab o-- "0..*" HNode : bucket chains
    Entry *-- HNode : embeds as first member
```

---

## Execution flow

### Request/response lifecycle

```mermaid
sequenceDiagram
    participant C as client
    participant L as event loop
    participant W as wire codec
    participant D as dispatch
    participant H as hash table
    participant S as serializer

    C->>L: connect, then send framed request
    L->>L: readable, read bytes into conn.incoming
    loop each complete frame
        L->>W: parse_req(frame)
        W-->>D: argv e.g. GET key
        D->>H: hm_lookup / hm_insert / hm_delete
        H-->>D: entry or none
        D->>S: out_str / out_int / out_nil / out_err
        S-->>L: append framed typed value to conn.outgoing
    end
    L->>C: writable, flush conn.outgoing
    C->>C: decode typed reply and print
```

### Connection state machine

Each `Conn` alternates between reading requests and flushing the response
buffer; back-pressure keeps the server from reading faster than it can write.

```mermaid
stateDiagram-v2
    [*] --> Reading
    Reading --> Reading: bytes buffered, request incomplete
    Reading --> Writing: request parsed, response queued
    Writing --> Writing: partial write, socket not yet ready
    Writing --> Reading: outgoing fully flushed
    Reading --> Closed: EOF, error, or oversized frame
    Reading --> Closed: idle past the timeout
    Writing --> Closed: write error
    Closed --> [*]
```

### Incremental rehashing

A resize is triggered lazily and then advanced a little on each operation, rather
than all at once — which is why the table never stalls.

```mermaid
flowchart TD
    A["hm_insert(node)"] --> B["insert into 'newer' table"]
    B --> C{"load factor exceeded<br/>and not already migrating?"}
    C -->|yes| D["demote 'newer' to 'older'<br/>allocate a 2x 'newer'"]
    C -->|no| E["help_rehashing()"]
    D --> E
    E --> F["migrate up to 128 buckets<br/>from 'older' to 'newer'"]
    F --> G{"'older' now empty?"}
    G -->|yes| H["free 'older' table"]
    G -->|no| I["return"]
    H --> I
```

`hm_lookup` and `hm_delete` run the same `help_rehashing` step and then search
`newer` first, falling back to `older`, so no key is ever lost mid-migration.

---

## Wire protocol

All integers are little-endian.

**Request**

```text
frame    = u32 length , payload           # length <= 32768 (MAX_MSG)
payload  = u32 nstr , argument{nstr}
argument = u32 len , byte{len}
```

Example — `SET user:1 alice`:

```text
nstr = 3
"SET"     -> 03 00 00 00 | 53 45 54
"user:1"  -> 06 00 00 00 | 75 73 65 72 3a 31
"alice"   -> 05 00 00 00 | 61 6c 69 63 65
```

**Response**

```text
frame = u32 length , value
value = tag , body        # tag/body per the serialization table above
```

---

## Commands

| Command | Arguments | Reply |
|---|---|---|
| `GET`  | `key`         | string, or nil if absent |
| `SET`  | `key value`   | nil |
| `DEL`  | `key`         | integer (1 if removed, else 0) |
| `KEYS` | —             | array of all keys |
| `ZADD` | `key score name` | integer (1 = new pair, 0 = score updated) |
| `ZREM` | `key name`    | integer (1 if removed, else 0) |
| `ZSCORE` | `key name`  | double, or nil if absent |
| `ZQUERY` | `key score name offset limit` | array of alternating name, score |
| `EXPIRE` | `key seconds` | integer (1 if the key exists, else 0) |
| `PEXPIRE` | `key milliseconds` | integer (1 if the key exists, else 0) |
| `TTL` | `key` | integer seconds left (−2 no key, −1 no TTL) |
| `PTTL` | `key` | integer milliseconds left (−2 no key, −1 no TTL) |
| `PERSIST` | `key` | integer (1 if a TTL was removed, else 0) |

`ZQUERY` is a generic range query: seek to the first pair `>= (score, name)`
in tuple order, walk `offset` positions by rank, then emit up to `limit`
output elements. Unknown verbs and malformed frames return a typed error;
commands against a key holding the wrong kind of value return `WRONGTYPE`.
`SET` discards any existing TTL on overwrite, and a non-positive expiration
deletes the key immediately — both matching Redis.

---

## Build, run, test

Requires CMake ≥ 3.20 and a C++17 compiler. Catch2 is fetched automatically.

```bash
cmake -B build
cmake --build build -j

# start the server (default port 6379)
./build/mini-redis-server 6379 &

# one-shot commands
./build/mini-redis-client set greeting hello
./build/mini-redis-client get greeting        # -> "hello"

# or an interactive session
./build/mini-redis-client
> set a 1
> keys
> exit
```

Run the test suite:

```bash
ctest --test-dir build --output-on-failure
```

The suite covers socket fd ownership and move semantics, the wire codec and
framing, `parse_req` on malformed and truncated input, the byte layout of the
response serializer, the intrusive list, the timer heap's ordering and
back-pointer invariants, the thread pool, and property tests that drive the
hash table (a one-million-key rehash-survival run), the AVL tree (100k random
ops with a full structural audit every 1k), and the sorted set (10k random
ops against a two-index oracle). CI runs it all on Linux and macOS with gcc
and clang, plus AddressSanitizer/UBSan and ThreadSanitizer jobs.

---

## Project layout

```text
mini-redis/
├── CMakeLists.txt              # builds a shared mini-redis-core static lib
├── include/
│   ├── common/                 # log helpers, container_of, string hash
│   ├── ds/                     # AVL tree (order statistics), sorted set, dlist, timer heap
│   ├── net/                    # Socket, read_full/write_all
│   ├── protocol/               # wire framing/parser, response serializer
│   ├── server/                 # Conn, Server, command dispatch
│   ├── store/                  # hash table, typed entries, Database + TTL
│   └── threadpool/             # worker pool for async value teardown
├── src/                        # implementations mirroring include/
├── client/                     # command-line client
├── tests/unit/                 # Catch2 unit tests
└── docs/architecture.md        # extended design document
```

---

## Design decisions

- **`poll()` over `epoll`/`kqueue`.** Portable across Linux and macOS and more
  than sufficient at this scale; the poller is isolated so a platform-specific
  backend could be added behind it later.
- **A custom intrusive hash table instead of `std::unordered_map`.** It enables
  incremental rehashing (no `O(n)` resize pause), keeps one allocation per entry,
  and gives full control over layout and iteration.
- **Single-threaded execution.** Commands run to completion on one thread, so
  each is atomic by construction — no locks, latches, or races on the keyspace.
  The worker pool doesn't weaken this: workers only ever free values that are
  already unlinked from the keyspace, never shared state.
- **A compact binary protocol.** Fixed-width lengths and type tags make framing
  and parsing trivial and allocation-bounded, and let the client render replies
  without guessing types.

---

## Roadmap

Implemented today: the networking stack, wire protocol, response serialization,
the hash-table keyspace with incremental rehashing, an AVL-backed sorted
set with `O(log n)` range and rank queries behind a typed command surface,
idle-connection timeouts, per-key TTL on a timer min-heap with lazy +
active expiry, and a worker pool that tears down large values off the
event loop.

Planned next:

- Additional value types (list, hash, set)
- Approximate LRU eviction under a memory cap
- Durability: append-only log and point-in-time snapshots
- Publish/subscribe
- Throughput and latency benchmarks against real Redis

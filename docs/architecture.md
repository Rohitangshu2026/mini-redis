# mini-redis Architecture

> Living document covering the architecture, design, and internal behaviour of mini-redis. Diagrams are Mermaid (renders natively on GitHub). Some components below are aspirational (planned by June 30, 2026); the **Status** column in the Component Inventory shows what is implemented vs in-progress.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Layered Architecture](#2-layered-architecture)
3. [Component Inventory](#3-component-inventory)
4. [Component Diagram](#4-component-diagram)
5. [Class Diagrams](#5-class-diagrams)
   - [5.1 Net + Server core](#51-net--server-core)
   - [5.2 Storage layer](#52-storage-layer)
   - [5.3 Data structures](#53-data-structures)
   - [5.4 Persistence](#54-persistence)
   - [5.5 Pub/Sub & threading](#55-pubsub--threading)
6. [Sequence Diagrams](#6-sequence-diagrams)
   - [6.1 Connection lifecycle](#61-connection-lifecycle)
   - [6.2 GET command](#62-get-command)
   - [6.3 SET command (with AOF)](#63-set-command-with-aof)
   - [6.4 BGSAVE (background snapshot)](#64-bgsave-background-snapshot)
   - [6.5 Pub/Sub PUBLISH fan-out](#65-pubsub-publish-fan-out)
   - [6.6 TTL expiration (lazy + active)](#66-ttl-expiration-lazy--active)
   - [6.7 Progressive rehashing](#67-progressive-rehashing)
   - [6.8 Server startup & recovery](#68-server-startup--recovery)
7. [Activity Diagrams](#7-activity-diagrams)
   - [7.1 Event loop iteration](#71-event-loop-iteration)
   - [7.2 Request parsing (try_one_request)](#72-request-parsing-try_one_request)
   - [7.3 LRU eviction sweep](#73-lru-eviction-sweep)
8. [State Diagrams](#8-state-diagrams)
   - [8.1 Connection state machine](#81-connection-state-machine)
   - [8.2 HMap rehashing state](#82-hmap-rehashing-state)
9. [Use Case Diagram](#9-use-case-diagram)
10. [Data Model (ER-style)](#10-data-model-er-style)
11. [Wire & Storage Formats](#11-wire--storage-formats)
12. [Deployment / System Design](#12-deployment--system-design)
13. [Concurrency Model](#13-concurrency-model)
14. [Failure Modes & Recovery](#14-failure-modes--recovery)
15. [Performance Budget](#15-performance-budget)
16. [Future / Post-v1.0 Extensions](#16-future--post-v10-extensions)

---

## 1. System Overview

```mermaid
flowchart LR
    subgraph Clients
        C1[redis-cli]
        C2[mini-redis-client]
        C3[Application]
        C4[mini-redis-bench]
    end

    subgraph mini-redis-server
        NET[Network I/O<br/>poll loop]
        PARSE[Wire Parser]
        DISP[Command Dispatcher]
        DB[(Keyspace)]
        AOF[AOF Writer]
        TP[Thread Pool]
        PSM[Pub/Sub Manager]
        TH[Timer Heap]
    end

    DISK[(Disk:<br/>appendonly.aof<br/>dump.rdb)]

    C1 & C2 & C3 & C4 -->|TCP| NET
    NET --> PARSE --> DISP
    DISP --> DB
    DISP --> AOF
    DISP --> PSM
    AOF --> DISK
    TP --> DISK
    DB --> TH
    PSM -.->|message fan-out| NET
```

**One-paragraph summary.** mini-redis is a single-threaded, event-loop-based, RESP-style in-memory data store written in C++17. A `poll()`-based reactor multiplexes thousands of TCP connections on the main thread. Every command is parsed, dispatched against a typed keyspace (custom hashtable with progressive rehashing), and may emit a side effect (AOF append, Pub/Sub fan-out, TTL registration). A worker thread pool handles only background tasks that would block the reactor: large-object deletion, RDB snapshots, AOF rewrites.

---

## 2. Layered Architecture

```mermaid
flowchart TB
    subgraph L1[Layer 1 — Transport]
        L1a[Socket RAII]
        L1b[Poller poll]
        L1c[EventLoop]
    end
    subgraph L2[Layer 2 — Protocol]
        L2a[Wire Framing]
        L2b[Parser]
        L2c[Serializer]
    end
    subgraph L3[Layer 3 — Command Plane]
        L3a[Command struct]
        L3b[CommandTable dispatch]
        L3c[Type-check / argc helpers]
    end
    subgraph L4[Layer 4 — Data Plane]
        L4a[Database]
        L4b[Entry tagged union]
        L4c[Keyspace HMap]
    end
    subgraph L5[Layer 5 — Data Structures]
        L5a[Intrusive HMap]
        L5b[DList]
        L5c[AVL + ZSet]
        L5d[Min-Heap timer]
    end
    subgraph L6[Layer 6 — Cross-cutting Services]
        L6a[AOF Writer]
        L6b[RDB Save/Load]
        L6c[LRU Eviction]
        L6d[Pub/Sub Manager]
        L6e[Thread Pool]
        L6f[INFO/CONFIG/CLIENT]
    end

    L1 --> L2 --> L3 --> L4 --> L5
    L3 --> L6
    L4 --> L6
```

Dependencies point downward. Higher layers know about lower layers; lower layers know nothing about higher layers. The `EventLoop` (L1) does not know what RESP is; the `CommandTable` (L3) does not know what `poll()` is; the `HMap` (L5) does not know there is a database.

---

## 3. Component Inventory

| # | Component | Purpose | File(s) | Implemented? |
|--:|---|---|---|:--:|
| 1 | `Socket` | RAII wrapper around fd | `include/net/socket.h` | ❌ (Day 2) |
| 2 | `io_helpers` | `read_full`, `write_all` | `src/net/io_helpers.cpp` | ❌ (Day 3) |
| 3 | `EventLoop` | poll() reactor, conn table | `src/net/event_loop.cpp` | ❌ (Day 5-6) |
| 4 | `Conn` | Per-connection state + buffers | `include/server/conn.h` | ❌ (Day 5) |
| 5 | Wire parser | Length-prefixed → argv | `src/protocol/wire.cpp` | ❌ (Day 3, 7) |
| 6 | Serializer | `out_str/int/dbl/nil/err/arr` | `src/protocol/wire.cpp` | ❌ (Day 10) |
| 7 | `CommandTable` | Dispatch map | `include/server/cmd_table.h` | ❌ (Day 7) |
| 8 | `HMap` | Intrusive open-chained hashtable | `src/store/hashtable.cpp` | ❌ (Day 8-9) |
| 9 | `Database` | Keyspace facade | `include/store/database.h` | ❌ (Day 7+) |
| 10 | `Entry` | Tagged-union value record | `include/store/entry.h` | ❌ (Day 7+) |
| 11 | `DList` | Intrusive doubly-linked list | `include/ds/dlist.h` | ❌ (Day 17) |
| 12 | `AVLNode` | Balanced BST with subtree count | `src/ds/avl.cpp` | ❌ (Day 11) |
| 13 | `ZSet` | AVL + HMap for sorted sets | `src/ds/zset.cpp` | ❌ (Day 12) |
| 14 | `TimerHeap` | Min-heap of (deadline, ref) | `src/ds/heap.cpp` | ❌ (Day 14) |
| 15 | `AofWriter` | Append + fsync policies | `src/persistence/aof.cpp` | ❌ (Day 20-21) |
| 16 | `RDB` | Binary snapshot save/load | `src/persistence/rdb.cpp` | ❌ (Day 22-23) |
| 17 | `LruEvictor` | Approximated LRU | `src/eviction/lru.cpp` | ❌ (Day 24) |
| 18 | `PubSubManager` | Channels + patterns | `src/pubsub/pubsub.cpp` | ❌ (Day 25) |
| 19 | `ThreadPool` | Worker threads + task queue | `src/threadpool/thread_pool.cpp` | ❌ (Day 15) |
| 20 | CMake build | Build system | `CMakeLists.txt` | ✅ (Day 1) |
| 21 | Catch2 tests | Unit test framework | `tests/CMakeLists.txt` | ✅ (Day 1) |
| 22 | `Server` | Acceptor + glue (current) | `src/server/server.cpp` | 🟡 (skeleton) |
| 23 | `mini-redis-client` | Interactive CLI | `client/client.cpp` | 🟡 (REPL) |
| 24 | `mini-redis-bench` | Multi-threaded bench | `benchmark/bench.cpp` | ❌ (Day 27-28) |

Legend: ✅ done · 🟡 partial · ❌ not started

---

## 4. Component Diagram

```mermaid
flowchart TB
    subgraph netpkg[net]
        Socket
        Poller
        EventLoop
    end
    subgraph protocolpkg[protocol]
        WireParser
        Serializer
    end
    subgraph serverpkg[server]
        Server
        Conn
        CommandTable
        CommandHandlers
    end
    subgraph storepkg[store]
        Database
        Entry
    end
    subgraph dspkg[ds]
        HMap
        DList
        AVL
        ZSet
        Heap
    end
    subgraph persistpkg[persistence]
        AofWriter
        RdbWriter
        RdbLoader
        CRC64
    end
    subgraph crosscut[cross-cutting]
        ThreadPool
        PubSubManager
        LruEvictor
        Config
        Info
    end

    Server --> EventLoop
    EventLoop --> Poller --> Socket
    EventLoop --> Conn
    Conn --> WireParser
    Conn --> Serializer
    Conn --> CommandTable
    CommandTable --> CommandHandlers
    CommandHandlers --> Database
    CommandHandlers --> AofWriter
    CommandHandlers --> PubSubManager
    Database --> Entry --> HMap
    Database --> Heap
    Entry --> DList
    Entry --> ZSet
    ZSet --> AVL
    ZSet --> HMap
    AofWriter --> ThreadPool
    RdbWriter --> ThreadPool
    RdbWriter --> CRC64
    RdbLoader --> CRC64
    Database --> LruEvictor
    Server --> Config
    Server --> Info
```

---

## 5. Class Diagrams

### 5.1 Net + Server core

```mermaid
classDiagram
    class Socket {
        -int fd_
        +Socket(int fd)
        +~Socket()
        +set_nonblocking() void
        +fd() int
        +release() int
        +Socket(Socket&&) noexcept
        +Socket(const Socket&) = delete
    }

    class Poller {
        <<interface>>
        +add(int fd, EventType) void
        +modify(int fd, EventType) void
        +remove(int fd) void
        +poll(int timeout_ms) vector~IoEvent~
    }

    class PollPoller {
        -vector~pollfd~ fds_
        -unordered_map~int,size_t~ fd_to_idx_
        +add(int, EventType) void
        +poll(int) vector~IoEvent~
    }

    class EventLoop {
        -unique_ptr~Poller~ poller_
        -unordered_map~int, unique_ptr~Conn~~ conns_
        -TimerHeap timers_
        -DList idle_list_
        +run() void
        +add_conn(unique_ptr~Conn~) void
        +remove_conn(int fd) void
        -accept_new(int listen_fd) void
        -handle_conn(Conn&) void
        -process_expired() void
    }

    class Server {
        -Socket listen_sock_
        -EventLoop loop_
        -Database db_
        -CommandTable cmds_
        -AofWriter aof_
        -PubSubManager pubsub_
        -ThreadPool pool_
        -Config cfg_
        +Server(Config)
        +run() void
    }

    class Conn {
        +int fd
        +ConnState state
        +vector~uint8_t~ incoming
        +vector~uint8_t~ outgoing
        +uint64_t idle_start_ms
        +bool in_pubsub_mode
        +unordered_set~string~ subscribed
        +Conn(int fd)
    }

    Poller <|.. PollPoller
    EventLoop --> Poller
    EventLoop --> Conn
    Server --> EventLoop
    Server --> Socket
```

### 5.2 Storage layer

```mermaid
classDiagram
    class HNode {
        +HNode* next
        +uint64_t hcode
    }

    class HTab {
        +HNode** tab
        +size_t mask
        +size_t size
    }

    class HMap {
        +HTab newer
        +HTab older
        +size_t migrate_pos
        +hm_insert(HNode*) void
        +hm_lookup(HNode* key, eq) HNode**
        +hm_pop(HNode* key, eq) HNode*
        +hm_help_resizing() void
    }

    class Entry {
        +HNode node
        +string key
        +Type type
        +union~str,list,hash,set,zset~ data
        +size_t heap_idx
        +uint32_t last_access_sec
    }

    class Database {
        -HMap keyspace_
        -TimerHeap heap_
        -atomic~size_t~ mem_used_
        -uint64_t startup_sec_
        +get(string& key) Entry*
        +set(string&& key, Entry&&) void
        +del(string& key) bool
        +expire(string& key, uint64_t ms) bool
        +foreach(callback) void
        +random_entry() Entry*
    }

    HMap *-- HTab
    HTab *-- HNode
    Entry *-- HNode
    Database *-- HMap
```

### 5.3 Data structures

```mermaid
classDiagram
    class DListNode {
        +DListNode* prev
        +DListNode* next
    }
    class DList {
        +DListNode head
        +size_t len
        +push_front(DListNode*) void
        +push_back(DListNode*) void
        +pop_front() DListNode*
        +pop_back() DListNode*
        +empty() bool
    }
    class ListItem {
        +DListNode node
        +string value
    }

    class AVLNode {
        +AVLNode* parent
        +AVLNode* left
        +AVLNode* right
        +uint32_t height
        +uint32_t cnt
    }
    class AVL {
        <<static>>
        +avl_fix(AVLNode*) AVLNode*
        +avl_del(AVLNode*) AVLNode*
        +avl_offset(AVLNode*, int64_t) AVLNode*
    }

    class ZNode {
        +AVLNode tree_node
        +HNode hmap_node
        +double score
        +size_t len
        +char[] name
    }
    class ZSet {
        +AVLNode* tree
        +HMap hmap
        +zset_add(name, score) void
        +zset_lookup(name) ZNode*
        +zset_pop(name) ZNode*
        +zset_query(score, name, offset) ZNode*
    }

    class HeapItem {
        +uint64_t deadline
        +size_t* ref
    }
    class TimerHeap {
        -vector~HeapItem~ heap_
        +push(uint64_t, size_t*) void
        +update(size_t) void
        +pop() HeapItem
        +peek() HeapItem*
        +size() size_t
    }

    DList *-- DListNode
    ListItem --|> DListNode : embeds
    ZSet *-- AVLNode
    ZSet *-- HMap
    ZNode --|> AVLNode : embeds
    ZNode --|> HNode : embeds
    TimerHeap *-- HeapItem
```

### 5.4 Persistence

```mermaid
classDiagram
    class FsyncPolicy {
        <<enumeration>>
        ALWAYS
        EVERYSEC
        NO
    }

    class AofWriter {
        -int fd_
        -FsyncPolicy policy_
        -thread fsync_thread_
        -atomic~bool~ stop_
        -mutex write_mu_
        +open(path, policy) bool
        +append(vector~string~ argv) void
        +rewrite_atomic(callback) void
        +close() void
        -fsync_loop() void
    }

    class RdbWriter {
        -int fd_
        -uint64_t crc_
        +save(path, Database&) bool
        -write_string(string&) void
        -write_entry(Entry&) void
        -write_footer() void
    }

    class RdbLoader {
        -int fd_
        -uint64_t crc_
        +load(path, Database&) bool
        -read_entry() Entry
        -verify_crc() bool
    }

    class CRC64 {
        <<static>>
        +update(uint64_t state, bytes) uint64_t
        +finalize(uint64_t state) uint64_t
    }

    AofWriter ..> FsyncPolicy
    RdbWriter ..> CRC64
    RdbLoader ..> CRC64
```

### 5.5 Pub/Sub & threading

```mermaid
classDiagram
    class PubSubManager {
        -unordered_map~string, unordered_set~Conn*~~ channels_
        -vector~pair~string, Conn*~~ patterns_
        +subscribe(Conn*, string channel) void
        +unsubscribe(Conn*, string channel) void
        +psubscribe(Conn*, string pattern) void
        +publish(string channel, string_view msg) int
        +on_disconnect(Conn*) void
        -glob_match(pattern, channel) bool
    }

    class ThreadPool {
        -vector~thread~ workers_
        -queue~function~~ tasks_
        -mutex mu_
        -condition_variable cv_
        -bool stop_
        +ThreadPool(size_t n)
        +~ThreadPool()
        +submit(function~void()~) void
    }

    class LruEvictor {
        -Database* db_
        -size_t max_memory_
        -Policy policy_
        +evict_if_needed() size_t
        -sample_victim() Entry*
    }

    class Config {
        +size_t maxmemory
        +string maxmemory_policy
        +FsyncPolicy appendfsync
        +bool appendonly
        +string dir
        +int port
        +get(string) string
        +set(string, string) bool
    }

    PubSubManager ..> Conn
    ThreadPool --> "0..*" thread
    LruEvictor --> Database
```

---

## 6. Sequence Diagrams

### 6.1 Connection lifecycle

```mermaid
sequenceDiagram
    autonumber
    actor C as Client
    participant L as Listener
    participant EL as EventLoop
    participant Conn as Conn
    participant CT as CommandTable
    participant DB as Database

    C->>L: TCP SYN
    L-->>C: SYN-ACK
    C->>L: ACK
    L->>EL: accept() returns fd
    EL->>Conn: new Conn(fd)
    EL->>EL: register fd POLLIN
    loop while connected
        C->>Conn: write(request)
        EL->>Conn: poll wakes READ
        Conn->>Conn: read into incoming buffer
        Conn->>CT: try_one_request(argv)
        CT->>DB: lookup/mutate
        DB-->>CT: result
        CT-->>Conn: serialized reply into outgoing
        EL->>EL: register fd POLLOUT
        Conn-->>C: write(reply)
    end
    C->>Conn: close()
    EL->>EL: poll detects POLLHUP
    EL->>Conn: destroy
```

### 6.2 GET command

```mermaid
sequenceDiagram
    autonumber
    participant Conn
    participant Parser
    participant Dispatcher
    participant DB as Database
    participant HMap
    participant Heap as TimerHeap
    participant Ser as Serializer

    Conn->>Parser: parse_req(incoming, argv)
    Parser-->>Conn: argv = ["GET", "foo"]
    Conn->>Dispatcher: dispatch(argv)
    Dispatcher->>DB: get("foo")
    DB->>HMap: hm_lookup(hash("foo"))
    HMap-->>DB: HNode*
    DB->>DB: Entry* via container_of
    alt has TTL and expired
        DB->>Heap: remove(entry.heap_idx)
        DB->>HMap: hm_pop()
        DB-->>Dispatcher: nullptr
        Dispatcher->>Ser: out_nil()
    else found and valid
        DB->>DB: entry.last_access_sec = now
        DB-->>Dispatcher: Entry*
        Dispatcher->>Ser: out_str(entry.str)
    end
    Ser-->>Conn: outgoing += response
```

### 6.3 SET command (with AOF)

```mermaid
sequenceDiagram
    autonumber
    participant Conn
    participant Dispatcher
    participant DB as Database
    participant Evict as LruEvictor
    participant AOF as AofWriter
    participant FS as Disk

    Conn->>Dispatcher: argv=["SET","foo","bar","EX","30"]
    Dispatcher->>Dispatcher: argc + flag check
    Dispatcher->>DB: set("foo", "bar")
    DB->>DB: mem_used += sizeof
    DB-->>Dispatcher: ok
    Dispatcher->>DB: expire("foo", 30000)
    DB->>DB: heap.push(deadline, &heap_idx)
    Dispatcher->>Evict: evict_if_needed()
    alt mem_used > maxmemory
        Evict->>DB: sample 16 keys
        Evict->>DB: del(lru_victim)
    end
    Note over Dispatcher,AOF: Convert EX → PXAT absolute
    Dispatcher->>AOF: append(["SET","foo","bar","PXAT","<deadline>"])
    AOF->>FS: write(resp_bytes)
    alt policy = ALWAYS
        AOF->>FS: fdatasync()
    end
    Dispatcher-->>Conn: +OK
```

### 6.4 BGSAVE (background snapshot)

```mermaid
sequenceDiagram
    autonumber
    participant Conn
    participant Dispatcher
    participant DB as Database
    participant TP as ThreadPool
    participant W as Worker thread
    participant FS as Disk

    Conn->>Dispatcher: BGSAVE
    Dispatcher->>DB: deep_copy()
    DB-->>Dispatcher: snapshot (shared_ptr)
    Dispatcher->>TP: submit(save_task)
    Dispatcher-->>Conn: +Background saving started
    TP->>W: schedule task
    W->>FS: open dump.rdb.tmp
    loop each entry in snapshot
        W->>FS: write encoded entry
        W->>W: update CRC64
    end
    W->>FS: write 0xFF + CRC64
    W->>FS: fsync()
    W->>FS: rename dump.rdb.tmp → dump.rdb
```

### 6.5 Pub/Sub PUBLISH fan-out

```mermaid
sequenceDiagram
    autonumber
    actor Pub as Publisher
    actor Sub1 as Subscriber-1
    actor Sub2 as Subscriber-2
    participant Conn1 as Conn(Pub)
    participant Dispatcher
    participant PSM as PubSubManager
    participant ConnA as Conn(Sub1)
    participant ConnB as Conn(Sub2)
    participant EL as EventLoop

    Sub1->>ConnA: SUBSCRIBE news
    ConnA->>PSM: subscribe(connA, "news")
    PSM-->>ConnA: ["subscribe","news",1]
    Sub2->>ConnB: SUBSCRIBE news
    ConnB->>PSM: subscribe(connB, "news")
    PSM-->>ConnB: ["subscribe","news",1]

    Pub->>Conn1: PUBLISH news hello
    Conn1->>Dispatcher: PUBLISH
    Dispatcher->>PSM: publish("news","hello")
    PSM->>ConnA: outgoing += ["message","news","hello"]
    PSM->>ConnB: outgoing += ["message","news","hello"]
    PSM-->>Dispatcher: 2
    Dispatcher-->>Conn1: :2
    EL->>ConnA: poll fires POLLOUT
    ConnA-->>Sub1: write message
    EL->>ConnB: poll fires POLLOUT
    ConnB-->>Sub2: write message
```

### 6.6 TTL expiration (lazy + active)

```mermaid
sequenceDiagram
    autonumber
    participant Loop as EventLoop
    participant Heap as TimerHeap
    participant DB as Database
    participant Client

    Note over Loop: top of each iteration
    Loop->>Heap: peek()
    alt heap empty
        Loop->>Loop: timeout = idle_timeout
    else top.deadline <= now
        loop up to ACTIVE_EXPIRE_MAX
            Heap->>DB: get entry via ref
            DB->>DB: hm_pop, free
            DB->>Heap: pop
        end
    else top.deadline > now
        Loop->>Loop: timeout = min(idle, top.deadline-now)
    end
    Loop->>Loop: poll(timeout)

    Note over Client,DB: lazy path
    Client->>DB: GET foo
    DB->>DB: entry.expires_at <= now?
    alt expired
        DB->>Heap: remove(entry.heap_idx)
        DB->>DB: hm_pop, free
        DB-->>Client: nil
    end
```

### 6.7 Progressive rehashing

```mermaid
sequenceDiagram
    autonumber
    participant Op as hm_insert/lookup/pop
    participant HMap
    participant Newer as HTab.newer
    participant Older as HTab.older

    Op->>HMap: hm_help_resizing()
    alt older.size > 0
        loop up to REHASH_WORK slots
            HMap->>Older: slot at migrate_pos
            alt slot not empty
                HMap->>Older: detach chain
                HMap->>Newer: re-insert each node
            end
            HMap->>HMap: migrate_pos++
        end
        alt all migrated
            HMap->>HMap: free older.tab
        end
    end
    Op->>HMap: original lookup/insert/pop
    alt insert and newer.size > load_factor
        HMap->>HMap: start_resizing()
        Note over HMap: newer → older,<br/>newer = realloc(2× size)
    end
```

### 6.8 Server startup & recovery

```mermaid
sequenceDiagram
    autonumber
    participant M as main()
    participant Cfg as Config
    participant DB as Database
    participant RDB as RdbLoader
    participant AOF as AofWriter
    participant EL as EventLoop

    M->>Cfg: parse args + config file
    M->>DB: construct (empty)
    alt appendonly.aof exists
        M->>AOF: replay file
        loop each RESP array
            AOF->>DB: dispatch as command (AOF write disabled)
        end
    else dump.rdb exists
        M->>RDB: load("dump.rdb", db)
        RDB->>RDB: verify CRC64
        RDB->>DB: insert each entry + restore TTL
    end
    M->>AOF: open for append
    M->>EL: setup, register listen socket
    M->>EL: run()  // never returns
```

---

## 7. Activity Diagrams

### 7.1 Event loop iteration

```mermaid
flowchart TD
    Start([loop start]) --> A[compute timeout: min idle, next TTL]
    A --> B[build pollfd vector]
    B --> C[poll fds timeout]
    C --> D{rv > 0?}
    D -- no --> E[tick timers]
    D -- yes --> F[iterate revents]
    F --> G{fd = listen?}
    G -- yes --> H[accept_new]
    G -- no --> I{POLLIN?}
    I -- yes --> J[read into incoming]
    J --> K[while try_one_request<br/>process pipelined]
    I -- no --> L{POLLOUT?}
    L -- yes --> M[write from outgoing]
    M --> N{outgoing empty?}
    N -- yes --> O[clear POLLOUT]
    F --> P{POLLHUP or POLLERR?}
    P -- yes --> Q[mark END]
    H --> E
    K --> E
    O --> E
    Q --> E
    E --> R[reap closed conns]
    R --> S[evict if mem > maxmemory]
    S --> Start
```

### 7.2 Request parsing (try_one_request)

```mermaid
flowchart TD
    Start([call]) --> A{incoming.size >= 4?}
    A -- no --> R1[return false]
    A -- yes --> B[len = read_u32_le first 4 bytes]
    B --> C{len > MAX_MSG?}
    C -- yes --> ERR[mark END, return true]
    C -- no --> D{incoming.size >= 4+len?}
    D -- no --> R1
    D -- yes --> E[parse_req on payload → argv]
    E --> F{parse ok?}
    F -- no --> ERR
    F -- yes --> G[dispatch argv → out buffer]
    G --> H[prepend out.size as u32 LE]
    H --> I[append to outgoing]
    I --> J[erase 4+len bytes from incoming]
    J --> K[return true]
```

### 7.3 LRU eviction sweep

```mermaid
flowchart TD
    Start([evict_if_needed]) --> A{mem_used > maxmemory?}
    A -- no --> Done([return])
    A -- yes --> B{policy = noeviction?}
    B -- yes --> ERR[write command returns OOM]
    B -- no --> C[sample 16 random entries]
    C --> D{policy = volatile-lru?}
    D -- yes --> E[filter entries with heap_idx != -1]
    D -- no --> F[use all sampled]
    E --> F
    F --> G[pick min last_access_sec]
    G --> H[del victim]
    H --> A
```

---

## 8. State Diagrams

### 8.1 Connection state machine

```mermaid
stateDiagram-v2
    [*] --> ACCEPTED
    ACCEPTED --> REQ: register POLLIN
    REQ --> REQ: read, partial request
    REQ --> RES: complete request, response queued
    RES --> RES: writing, more pending
    RES --> REQ: outgoing drained
    REQ --> PUBSUB: SUBSCRIBE seen
    PUBSUB --> PUBSUB: PUBLISH push to outgoing
    PUBSUB --> END: UNSUBSCRIBE all or disconnect
    REQ --> END: read returns 0 / POLLHUP
    RES --> END: write returns -1 / POLLERR
    END --> [*]
```

### 8.2 HMap rehashing state

```mermaid
stateDiagram-v2
    [*] --> STABLE
    STABLE --> RESIZING: load_factor > threshold
    note right of RESIZING
        newer → older
        newer = realloc(2× size)
        migrate_pos = 0
    end note
    RESIZING --> RESIZING: each op migrates REHASH_WORK slots
    RESIZING --> STABLE: older.size == 0 → free older
    STABLE --> SHRINKING: load_factor < min_threshold (optional)
    SHRINKING --> STABLE: migration done
```

---

## 9. Use Case Diagram

Mermaid doesn't render true UML use case bubbles; this is a stylised equivalent.

```mermaid
flowchart LR
    classDef actor fill:#fff,stroke:#000,stroke-width:2px
    classDef usecase fill:#e6f3ff,stroke:#03f,stroke-width:1px,rx:30,ry:30

    A1([Application Client]):::actor
    A2([Admin / Operator]):::actor
    A3([Bench Tool]):::actor
    A4([Monitoring System]):::actor

    UC1[Store/retrieve key-value]:::usecase
    UC2[Manipulate lists]:::usecase
    UC3[Manipulate hashes]:::usecase
    UC4[Manipulate sets]:::usecase
    UC5[Manipulate sorted sets]:::usecase
    UC6[Set TTL on key]:::usecase
    UC7[Subscribe to channel]:::usecase
    UC8[Publish to channel]:::usecase
    UC9[Inspect server INFO]:::usecase
    UC10[Change config]:::usecase
    UC11[Trigger SAVE / BGSAVE]:::usecase
    UC12[Trigger BGREWRITEAOF]:::usecase
    UC13[Flush database]:::usecase
    UC14[Manage clients<br/>CLIENT LIST/KILL]:::usecase
    UC15[Run benchmark]:::usecase
    UC16[Scrape metrics]:::usecase

    A1 --> UC1 & UC2 & UC3 & UC4 & UC5 & UC6 & UC7 & UC8
    A2 --> UC9 & UC10 & UC11 & UC12 & UC13 & UC14
    A3 --> UC15
    A4 --> UC9 & UC16
```

**Extends / Includes (text form):**
- `Store key with TTL` *includes* `Store/retrieve key-value` + `Set TTL on key`
- `BGSAVE` *extends* `SAVE` (background variant via thread pool)
- `BGREWRITEAOF` *extends* `Append to AOF` (full keyspace re-emission)
- `Evict LRU key` is triggered by `Store/retrieve key-value` when memory > maxmemory

---

## 10. Data Model (ER-style)

```mermaid
erDiagram
    DATABASE ||--o{ ENTRY : contains
    ENTRY ||--|| HNODE : "embeds as hashtable hook"
    ENTRY ||--o| HEAP_ITEM : "if TTL set, refers to"
    ENTRY ||--o| STRING : "type=STR"
    ENTRY ||--o| LIST : "type=LIST"
    ENTRY ||--o| HASH : "type=HASH"
    ENTRY ||--o| SET : "type=SET"
    ENTRY ||--o| ZSET : "type=ZSET"
    LIST ||--o{ LIST_ITEM : "doubly-linked"
    HASH ||--o{ HASH_FIELD : ""
    SET ||--o{ SET_MEMBER : ""
    ZSET ||--|| AVL_TREE : "ordered by (score,name)"
    ZSET ||--|| HMAP_INNER : "indexed by name"
    AVL_TREE ||--o{ ZNODE : ""

    DATABASE {
        HMap keyspace
        TimerHeap heap
        atomic_size_t mem_used
    }
    ENTRY {
        string key
        Type type
        uint64 last_access_sec
        size_t heap_idx
    }
    STRING { string value }
    LIST_ITEM { string value }
    HASH_FIELD { string field; string value }
    SET_MEMBER { string member }
    ZNODE { string name; double score }
    HEAP_ITEM { uint64 deadline; size_t* ref }
```

---

## 11. Wire & Storage Formats

### 11.1 Wire format (book-style, used Days 3-onwards)

```
Request:
  ┌─────────┬─────────────────────────────────────────────────┐
  │ u32 len │ payload                                         │
  └─────────┴─────────────────────────────────────────────────┘
              ┌─────────┬─────────┬───────┬─────────┬───────┐
              │ u32 nstr│ u32 len1│  str1 │ u32 len2│  str2 │
              └─────────┴─────────┴───────┴─────────┴───────┘

Response:
  ┌─────────┬──────────────┬────────────────────────────────┐
  │ u32 len │ u32 status   │  serialised value(s)           │
  └─────────┴──────────────┴────────────────────────────────┘
```

Serialised value types (Day 10+):
```
SER_NIL  = 0  →  [u8 0]
SER_ERR  = 1  →  [u8 1][u32 code][u32 mlen][msg]
SER_STR  = 2  →  [u8 2][u32 len][bytes]
SER_INT  = 3  →  [u8 3][i64]
SER_DBL  = 4  →  [u8 4][f64]
SER_ARR  = 5  →  [u8 5][u32 n][value...]
```

### 11.2 AOF format (RESP2, Day 20+)

Each appended command is a RESP2 array, plain ASCII, fsynced per policy.

```
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
*3\r\n$6\r\nPXEXPIREAT\r\n$3\r\nfoo\r\n$13\r\n1717900000000\r\n
*5\r\n$5\r\nLPUSH\r\n$1\r\nl\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n
```

### 11.3 RDB format (Day 22)

```
┌─────────────┐
│ "MREDB0001" │   9-byte magic + version
├─────────────┤
│ entry 1     │
│ ┌─────────┐ │
│ │ u8 type │ │   0=STR 1=LIST 2=HASH 3=SET 4=ZSET
│ │ u64 exp │ │   absolute expiry ms, 0=none
│ │ u32 klen│ │
│ │ key     │ │
│ │ payload │ │   type-specific (see below)
│ └─────────┘ │
│ entry 2     │
│ ...         │
├─────────────┤
│ 0xFF        │   EOF marker
│ u64 CRC64   │   over all preceding bytes
└─────────────┘
```

Per-type payload:
```
STR   : u32 len; bytes
LIST  : u32 count; (u32 len; bytes) × count
HASH  : u32 count; (u32 flen; bytes; u32 vlen; bytes) × count
SET   : u32 count; (u32 len; bytes) × count
ZSET  : u32 count; (u32 nlen; bytes; f64 score) × count
```

---

## 12. Deployment / System Design

### 12.1 v1.0 — Single-node deployment

```mermaid
flowchart LR
    subgraph host[Single host / container]
        direction TB
        proc[mini-redis-server<br/>port 6379]
        proc -- writes --> aof[appendonly.aof]
        proc -- snapshots --> rdb[dump.rdb]
        proc -- logs --> stderr[stderr]
    end
    cli1[redis-cli]
    cli2[Application]
    bench[mini-redis-bench]
    grafana[Future: Prometheus/Grafana]

    cli1 -->|TCP 6379| proc
    cli2 -->|TCP 6379| proc
    bench -->|TCP 6379| proc
    grafana -.->|HTTP /metrics| proc
```

### 12.2 Post-v1.0 — Replicated deployment (planned, Appendix B)

```mermaid
flowchart TB
    subgraph cluster[3-node Raft cluster]
        L[Leader]
        F1[Follower 1]
        F2[Follower 2]
    end
    app[Application] -->|writes| L
    app -->|reads| L
    app -.->|reads| F1
    app -.->|reads| F2
    L <-->|AppendEntries| F1
    L <-->|AppendEntries| F2
    F1 <-->|RequestVote on timeout| F2
```

### 12.3 Process / thread model

```mermaid
flowchart LR
    subgraph proc[mini-redis-server process]
        MT[Main thread<br/>event loop]
        W1[Worker 1]
        W2[Worker 2]
        W3[Worker 3]
        W4[Worker 4]
        FT[AOF fsync thread]
    end
    MT -- submits big-DEL,<br/>BGSAVE, BGREWRITEAOF --> W1
    MT -- submits --> W2
    MT -- submits --> W3
    MT -- submits --> W4
    MT -.-> FT
    FT -.->|fdatasync every 1s| disk[(disk)]
```

All command execution is on `MT` — no command handler ever runs on a worker. Workers only do disk I/O and large frees. This is why mini-redis can call itself "single-threaded execution" while still using threads.

---

## 13. Concurrency Model

| Resource | Owner | Accessed from | Synchronisation |
|---|---|---|---|
| `Database` keyspace | main thread | main thread | none (single-threaded) |
| `TimerHeap` | main thread | main thread | none |
| `EventLoop` | main thread | main thread | none |
| `Conn` objects | main thread | main thread | none |
| `ThreadPool::tasks_` queue | pool | main + workers | `mutex + cv` |
| `AofWriter::fd_` | aof | main + fsync thread | `mutex` for write; `atomic` stop_ |
| `Database::mem_used_` | DB | main + readers of INFO | `std::atomic<size_t>` (relaxed) |
| Per-thread benchmark histograms | bench | own thread only | none, merge at end |
| `PubSubManager` channels | main thread | main thread | none |

Rule of thumb: if it's data the worker thread touches, it's either *immutable* (copy-on-submit) or *atomic*. There are no `std::mutex`-protected shared structures in the request path. This is the single most important invariant for correctness.

---

## 14. Failure Modes & Recovery

| Failure | Detection | Recovery |
|---|---|---|
| Process killed mid-write | OS | On next start: replay AOF; restored to last fsynced command |
| Disk full during AOF append | `write()` returns EIO | Log error; switch to read-only mode; alert via INFO |
| Disk full during RDB save | worker thread errors | Drop the .tmp; keep last dump.rdb |
| Corrupt RDB (bad CRC) | RdbLoader checksum mismatch | Refuse to start; require manual intervention |
| Client sends oversized message | wire parser detects len > MAX_MSG | Close connection with -ERR |
| Client sends malformed RESP | parser returns ERROR | Close connection |
| Subscriber outgoing buffer full | overflow check in publish() | Drop message or close conn (configurable) |
| Heap corruption (assert) | runtime assert | Process aborts → systemd/k8s restart → AOF replay |
| OOM | malloc returns null OR mem_used > limit | Eviction (LRU) or return -OOM to client |

---

## 15. Performance Budget

Targets at v1.0 launch (June 30, 2026), on Apple M2 / 16GB RAM, single-threaded mini-redis vs real Redis 7.x on identical hardware:

| Metric | Target | Stretch |
|---|---|---|
| `PING` throughput | ≥ 90% of Redis | 95% |
| `GET` throughput | ≥ 70% of Redis | 85% |
| `SET` throughput | ≥ 70% of Redis | 80% |
| `ZADD` throughput | ≥ 50% of Redis | 70% |
| `GET` p99 latency | ≤ 1.5× Redis | 1.2× |
| Max concurrent connections | 10,000 | 50,000 |
| Memory overhead per key (string, small) | ≤ 100 bytes | 80 |
| BGSAVE pause on main thread | ≤ 50 ms for 100k keys | 10 ms |
| Differential test correctness | 100k commands byte-identical | 1M |

These numbers go into the README on Day 36 as a results table once Day 29's benchmarks land.

---

## 16. Future / Post-v1.0 Extensions

See plan Appendix B. In priority order for placement-tier impact:

1. **RESP3 compatibility** — drop-in `redis-cli` interop
2. **Raft replication** — leader election + log replication for 3-node HA
3. **io_uring backend** — `IoUringPoller` swapped behind `Poller` interface on Linux
4. **Transactions** — MULTI/EXEC/WATCH with optimistic locking
5. **C++20 coroutines** — `co_await read_command()`-style connection handlers
6. **Prometheus metrics** — `/metrics` HTTP endpoint + Grafana dashboard
7. **Cluster sharding** — CRC16 16384-slot partitioning
8. **Lua scripting** — EVAL/EVALSHA with embedded Lua 5.4
9. **TLS** — mbedTLS-backed `--tls-port`
10. **Docker + multi-arch CI** — GHCR image for `linux/amd64` and `linux/arm64`

Each is sketched in plan Appendix B with effort estimates.

---

## Appendix A — Conventions

- All multi-byte integers on the wire are **little-endian unless noted**.
- All timestamps are `uint64_t` milliseconds since UNIX epoch unless noted.
- All hash codes are `uint64_t` produced by `xxhash` (Day 30 swap) or FNV-1a (default).
- All errors begin with an uppercase identifier: `ERR`, `WRONGTYPE`, `NOAUTH`, `OOM`.
- Source layout: headers in `include/<package>/`, implementations in `src/<package>/`. Package names: `net`, `protocol`, `server`, `store`, `ds`, `persistence`, `eviction`, `pubsub`, `threadpool`.
- All public APIs in headers; nothing else. Implementation details stay in `.cpp`.

## Appendix B — Diagram Maintenance

When a class signature or sequence changes:
1. Update the relevant section above.
2. Re-render locally with `mermaid-cli` if you want to commit SVGs (optional): `npx -p @mermaid-js/mermaid-cli mmdc -i docs/architecture.md -o docs/architecture.svg`.
3. GitHub renders Mermaid inline — no build step needed for the rendered view.

## Appendix C — Glossary

- **AOF** — Append-Only File. Write-ahead log of every mutation.
- **RDB** — Redis Database file. Point-in-time binary snapshot.
- **RESP** — REdis Serialization Protocol. Text-based wire format.
- **Intrusive** — Data structure where the link nodes are embedded inside the user object, not allocated separately.
- **Progressive rehashing** — Spreading the cost of growing a hashtable over many operations so no single op pauses.
- **Lazy expiration** — Removing expired keys only when accessed.
- **Active expiration** — Removing expired keys proactively from a timer loop.
- **Robin Hood probing** — Open-addressing variant that swaps to minimise variance in probe distance. (Not used; the book takes the open-chained approach.)

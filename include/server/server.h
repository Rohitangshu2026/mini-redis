#pragma once

#include "net/socket.h"
#include "store/database.h"
#include "ds/dlist.h"
#include "threadpool/thread_pool.h"

#include <cstdint>
#include <memory>
#include <vector>

struct Conn;   // full definition in server/conn.h

class Server{
public:
    static constexpr uint64_t k_default_idle_timeout_ms = 30'000;
    static constexpr size_t   k_num_workers = 4;

    explicit Server(int port, uint64_t idle_timeout_ms = k_default_idle_timeout_ms);
    ~Server();                       // defined in .cpp (frees keyspace entries; Conn incomplete here)
    void run();

private:
    void    accept_new();
    void    handle_read(Conn& c);
    void    handle_write(Conn& c);
    bool    try_one_request(Conn& c);
    int32_t next_timer_ms();         // poll() timeout from the nearest timer
    void    process_timers();        // reap connections idle past the timeout

    Socket listen_sock_;
    int    port_;
    std::vector<std::unique_ptr<Conn>> conns_;   // indexed by fd; null if unused
    Database db_;                                 // keyspace (hash index) + TTL timer heap

    // Connections ordered by last activity: nearest expiry at the front,
    // most recently active at the back. Timers with one fixed timeout expire
    // in insertion order, so a list gives O(1) updates — no heap needed.
    DList    idle_list_;
    uint64_t idle_timeout_ms_;

    // Declared after db_: destroyed first, so pending async deletions are
    // drained (workers joined) before the rest of the server goes away.
    ThreadPool pool_{k_num_workers};
};

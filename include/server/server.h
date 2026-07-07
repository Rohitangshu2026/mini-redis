#pragma once

#include "net/socket.h"
#include "store/hashtable.h"

#include <memory>
#include <vector>

struct Conn;   // full definition in server/conn.h

class Server {
public:
    explicit Server(int port);
    ~Server();                       // defined in .cpp (frees keyspace entries; Conn incomplete here)
    void run();

private:
    void accept_new();
    void handle_read(Conn& c);
    void handle_write(Conn& c);
    bool try_one_request(Conn& c);

    Socket listen_sock_;
    int    port_;
    std::vector<std::unique_ptr<Conn>> conns_;   // indexed by fd; null if unused
    HMap   db_;                                   // keyspace: intrusive hashtable + progressive rehashing
};

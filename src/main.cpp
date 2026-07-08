#include <iostream>
#include <string>
#include "server/server.h"

// usage: mini-redis-server [port] [idle_timeout_ms]
int main(int argc, char* argv[]) {
    int port = 6379;    // default redis port number
    if (argc >= 2)
        port = std::stoi(argv[1]);
    uint64_t idle_timeout_ms = Server::k_default_idle_timeout_ms;
    if (argc >= 3)
        idle_timeout_ms = (uint64_t)std::stoull(argv[2]);
    Server server(port, idle_timeout_ms);
    server.run();
}

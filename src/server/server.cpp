#include"server/server.h"
#include"server/client_connection.h"

#include<iostream>
#include<cstring>
#include<unistd.h>
#include<cstdlib>
#include<arpa/inet.h>


Server::Server(int port) : server_fd(-1), port_(port){
    // create IPv4 (AF_INET) + TCP (SOCK_STREAM) socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){
        perror("Failed to create socket!");
        exit(1);
    }

    // allow rebinding to the same port after restart (avoid TIME_WAIT issues)
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt failed!");
        exit(1);
    }

    // bind address
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);                // TCP port in Network Byte Order 
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // bind to all local interfaces (wildcard IP 0.0.0.0)

    if(bind(server_fd,(sockaddr*)&addr, sizeof(addr)) < 0){
        perror("Failed to bind!");
        exit(1);
    }

    // start listening (backlog = 16 pending connections)
    if(listen(server_fd,16) < 0){
        perror("Failed to listen!");
        exit(1);
    }
    std::cout << "Server listening on port number " << port_ << std::endl;
}

Server::~Server(){
    if(server_fd >= 0)
        close(server_fd);
    std::cout << "Server shutdown..." << std::endl;
}

void Server::run(){
    while(true){
        // accept a new client connection
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if(client_fd < 0){
            perror("Failed to accept!");
            continue;
        }
        ClientConnection client(client_fd);
        client.handle();
    }
}
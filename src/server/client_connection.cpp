#include "server/client_connection.h"

#include <iostream>
#include <cstring>
#include <unistd.h>

ClientConnection::ClientConnection(int client_fd_) : client_fd(client_fd_){}

ClientConnection::~ClientConnection() {
    if (client_fd >= 0) 
        close(client_fd);
    
}

void ClientConnection::handle() {
    char read_buffer[64] = {};
    ssize_t n = read(client_fd,read_buffer,sizeof(read_buffer) - 1);
    if(n < 0){
        perror("Failed to read!");
        return;
    }
    if(n == 0){
        // client closed connection;
        return;
    }
    read_buffer[n] = '\0';
    std::cout << read_buffer << std::endl;
    char write_buffer[64] = "Message read!";
    ssize_t w = write(client_fd, write_buffer, strlen(write_buffer));
    if(w < 0){
        perror("Failed to write!");
        return;
    }
}
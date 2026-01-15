#include<iostream>
#include<cstring>
#include<unistd.h>
#include<arpa/inet.h>

static void do_something(int connection_fd){
    char read_buffer[64] = {};
    ssize_t n = read(connection_fd,read_buffer,sizeof(read_buffer) - 1);
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
    ssize_t w = write(connection_fd, write_buffer, strlen(write_buffer));
    if(w < 0){
        perror("Failed to write!");
        return;
    }
}

int main(){
    // create IPv4 (AF_INET) + TCP (SOCK_STREAM) socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0){
        perror("Failed to create socket!");
        return 1;
    }

    // allow rebinding to the same port after restart (avoid TIME_WAIT issues)
    int opt = 1;
    if(setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt failed!");
        return 1;
    }

    // bind address
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6379);                // TCP port in Network Byte Order 
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // bind to all local interfaces (wildcard IP 0.0.0.0)

    if(bind(server_fd,(sockaddr*)&addr, sizeof(addr)) < 0){
        perror("Failed to bind!");
        return 1;
    }

    // start listening (backlog = 16 pending connections)
    if(listen(server_fd,16) < 0){
        perror("Failed to listen!");
        return 1;
    }
    std::cout << "Server listening on port number 6379" << std::endl;

    while(true){
        // accept a new client connection
        struct sockaddr_in client_addr = {};
        socklen_t addrlen = sizeof(client_addr);
        int connection_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if(connection_fd < 0){
            perror("Failed to accept!");
            continue;
        }
        do_something(connection_fd);
        close(connection_fd);
    }

    return 0;
}
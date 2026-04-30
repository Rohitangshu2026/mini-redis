#include<iostream>
#include<cstring>
#include<unistd.h>
#include<arpa/inet.h>

int main(){
    // create IPv4 (AF_INET) + TCP (SOCK_STREAM) socket
    int client_fd = socket(AF_INET,SOCK_STREAM,0);
    if(client_fd < 0){
        perror("Failed to create socket!");
        return 1;
    }

    // server address
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(6379);

    // convert 127.0.0.1 to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0){
        perror("Invalid address");
        return 1;
    }

    // connect to server
    if(connect(client_fd,(struct sockaddr*)&server_addr,sizeof(server_addr)) < 0){
        perror("Failed to connect!");
        return 1;
    }

    std::cout << "Connected to server!\n";

    while(true){
        // read user input
        std::string input;
        std::getline(std::cin, input);

        if(input == "exit")
            break;

        // send message
        ssize_t w = write(client_fd, input.c_str(), input.size());
        if(w < 0){
            perror("Failed to send message!");
            return 1;
        }

        // read response
        char read_buffer[64] = {};
        ssize_t n = read(client_fd, read_buffer,sizeof(read_buffer) - 1);
        if(n <= 0){
            std::cout << "Server connection closed!!";
            break;
        }
        std::cout << "Server : " << read_buffer << std::endl;
    }

    
    close(client_fd);
    return 0;
}
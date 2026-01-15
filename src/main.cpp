#include<iostream>
#include"../include/server/server.h"
int main(int argc, char* argv[]){
    int port = 6379;    // default redis port nuber
    if(argc >= 2)
        port = std::stoi(argv[1]);
    Server server(port);
    server.run();
}
#pragma once

class ClientConnection{
public:
    ClientConnection(int client_fd_);
    ~ClientConnection();
    void handle();
private:
    int client_fd;
};
#ifndef SERVER_H
#define SERVER_H

class Server{
public:
    Server(int port);
    ~Server();
    void run();
private:
    int server_fd;
    int port_;
};

#endif
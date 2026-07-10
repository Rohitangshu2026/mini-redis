#include "net/io_helpers.h"

#include <unistd.h>

int32_t read_full(int fd, char* buf, size_t n){
    while(n > 0){
        ssize_t rv = ::read(fd, buf, n);
        if(rv <= 0){
            return -1;              // error (<0) or EOF (0) mid-message
        }
        // rv is in (0, n]: advance the cursor and shrink what's left
        n   -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

int32_t write_all(int fd, const char* buf, size_t n){
    while(n > 0){
        ssize_t rv = ::write(fd, buf, n);
        if(rv <= 0){
            return -1;              // write() returning 0 shouldn't happen
        }                           // on sockets; treat it as an error too
        n   -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

#include <catch2/catch_test_macros.hpp>

#include "net/socket.h"

#include <unistd.h>
#include <fcntl.h>
#include <utility>

// A fresh, valid fd (a dup of stdout) — no files or network needed.
static int new_fd(){
    int fd = ::dup(STDOUT_FILENO);
    REQUIRE(fd >= 0);
    return fd;
}
// True if fd is currently open.
static bool is_open(int fd){
    return ::fcntl(fd, F_GETFD) != -1;
}

TEST_CASE("Socket closes its fd on destruction", "[socket]"){
    int fd = new_fd();
    {
        Socket s(fd);
        REQUIRE(s.valid());
        REQUIRE(s.fd() == fd);
        REQUIRE(is_open(fd));
    }
    REQUIRE_FALSE(is_open(fd));        // destructor closed it
}

TEST_CASE("Socket move construction transfers ownership", "[socket]"){
    int fd = new_fd();
    Socket a(fd);
    Socket b(std::move(a));
    REQUIRE(b.valid());
    REQUIRE(b.fd() == fd);
    REQUIRE_FALSE(a.valid());          // moved-from is empty
    REQUIRE(a.fd() == -1);
    REQUIRE(is_open(fd));              // still open (owned by b)
}

TEST_CASE("Socket move assignment closes the old fd", "[socket]"){
    int fd1 = new_fd();
    int fd2 = new_fd();
    Socket a(fd1);
    Socket b(fd2);
    b = std::move(a);
    REQUIRE(b.fd() == fd1);
    REQUIRE_FALSE(a.valid());
    REQUIRE_FALSE(is_open(fd2));       // b's old fd was closed
    REQUIRE(is_open(fd1));             // b now owns fd1
}

TEST_CASE("Socket release gives up ownership without closing", "[socket]"){
    int fd = new_fd();
    Socket s(fd);
    int r = s.release();
    REQUIRE(r == fd);
    REQUIRE_FALSE(s.valid());
    REQUIRE(is_open(fd));              // not closed
    ::close(fd);                       // manual cleanup
}

TEST_CASE("Socket reset closes the held fd", "[socket]"){
    int fd = new_fd();
    Socket s(fd);
    s.reset();
    REQUIRE_FALSE(s.valid());
    REQUIRE_FALSE(is_open(fd));        // closed by reset
}

TEST_CASE("Socket self move-assignment is safe", "[socket]"){
    int fd = new_fd();
    Socket s(fd);
    Socket& ref = s;                   // alias avoids the obvious self-move warning
    s = std::move(ref);
    REQUIRE(s.valid());
    REQUIRE(s.fd() == fd);
    REQUIRE(is_open(fd));
}

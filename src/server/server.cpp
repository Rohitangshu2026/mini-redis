#include "server/server.h"
#include "server/conn.h"
#include "server/command.h"
#include "store/entry.h"
#include "common/intrusive.h"
#include "common/log.h"
#include "protocol/wire.h"
#include "protocol/serialize.h"

#include <iostream>
#include <cstdint>
#include <cerrno>
#include <vector>
#include <string>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// ---- byte-buffer helpers ----
static void buf_append(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}
static void buf_consume(std::vector<uint8_t>& buf, size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

Server::Server(int port) : port_(port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) die("socket()");

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        die("setsockopt(SO_REUSEADDR)");

    struct sockaddr_in addr = {};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) die("bind()");
    if (listen(fd, SOMAXCONN) < 0) die("listen()");

    listen_sock_.reset(fd);
    listen_sock_.set_nonblocking();
    std::cout << "Server listening on port " << port_ << std::endl;
}

namespace {
bool collect_node(HNode* node, void* arg) {
    static_cast<std::vector<HNode*>*>(arg)->push_back(node);
    return true;
}
}

Server::~Server() {
    std::vector<HNode*> all;
    hm_foreach(&db_, collect_node, &all);                          // collect first...
    for (HNode* n : all) entry_del(container_of(n, Entry, node));  // ...then free (no use-after-free)
    hm_clear(&db_);
}

void Server::accept_new() {
    struct sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int connfd = accept(listen_sock_.fd(), (sockaddr*)&client_addr, &addrlen);
    if (connfd < 0) { msg_errno("accept()"); return; }

    int flags = fcntl(connfd, F_GETFL, 0);
    if (flags < 0 || fcntl(connfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        msg_errno("fcntl(connfd)");
        ::close(connfd);
        return;
    }

    auto conn = std::make_unique<Conn>();
    conn->fd        = connfd;
    conn->want_read = true;
    if ((size_t)connfd >= conns_.size()) conns_.resize(connfd + 1);
    conns_[connfd] = std::move(conn);
}

// Process at most ONE complete request from incoming; returns true if one was processed.
bool Server::try_one_request(Conn& c) {
    if (c.incoming.size() < 4) return false;                 // need length header
    uint32_t len = read_u32(c.incoming.data());
    if (len > MAX_MSG) { c.want_close = true; return false; }
    if (c.incoming.size() < 4 + (size_t)len) return false;   // need full body

    const uint8_t* body = c.incoming.data() + 4;

    std::vector<std::string> cmd;
    std::vector<uint8_t> resp;
    if (parse_req(body, len, cmd) != 0) {
        out_err(resp, ERR_ARG, "bad request");               // malformed frame
    } else {
        do_request(cmd, db_, resp);
    }

    uint8_t hdr[4];
    write_u32(hdr, (uint32_t)resp.size());
    buf_append(c.outgoing, hdr, 4);
    buf_append(c.outgoing, resp.data(), resp.size());

    buf_consume(c.incoming, 4 + (size_t)len);
    return true;
}

void Server::handle_read(Conn& c) {
    uint8_t buf[64 * 1024];
    ssize_t rv = ::read(c.fd, buf, sizeof(buf));
    if (rv < 0 && errno == EAGAIN) return;        // spurious wakeup
    if (rv <= 0) { c.want_close = true; return; } // error or EOF
    buf_append(c.incoming, buf, (size_t)rv);

    while (try_one_request(c)) {}                  // drain all buffered requests (pipelining)

    if (!c.outgoing.empty()) { c.want_read = false; c.want_write = true; }
}

void Server::handle_write(Conn& c) {
    if (c.outgoing.empty()) return;
    ssize_t rv = ::write(c.fd, c.outgoing.data(), c.outgoing.size());
    if (rv < 0 && errno == EAGAIN) return;        // not writable yet
    if (rv < 0) { c.want_close = true; return; }
    buf_consume(c.outgoing, (size_t)rv);
    if (c.outgoing.empty()) { c.want_read = true; c.want_write = false; }
}

void Server::run() {
    auto make_pfd = [](int fd, short events) {
        struct pollfd p; p.fd = fd; p.events = events; p.revents = 0; return p;
    };

    std::vector<struct pollfd> poll_args;
    while (true) {
        poll_args.clear();
        poll_args.push_back(make_pfd(listen_sock_.fd(), POLLIN));   // [0] = listener

        for (auto& up : conns_) {
            if (!up) continue;
            short ev = 0;
            if (up->want_read)  ev = (short)(ev | POLLIN);
            if (up->want_write) ev = (short)(ev | POLLOUT);
            poll_args.push_back(make_pfd(up->fd, ev));
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
        if (rv < 0 && errno == EINTR) continue;
        if (rv < 0) die("poll()");

        if (poll_args[0].revents & POLLIN) accept_new();

        for (size_t i = 1; i < poll_args.size(); ++i) {
            short re = poll_args[i].revents;
            if (re == 0) continue;
            int fd = poll_args[i].fd;
            if (fd < 0 || (size_t)fd >= conns_.size() || !conns_[fd]) continue;
            Conn& c = *conns_[fd];

            if (re & POLLIN)  handle_read(c);
            if (re & POLLOUT) handle_write(c);
            if (c.want_close || (re & (POLLERR | POLLHUP))) conns_[fd].reset();  // ~Conn closes fd
        }
    }
}

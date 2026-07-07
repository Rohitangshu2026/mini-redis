#include "net/io_helpers.h"
#include "protocol/wire.h"
#include "protocol/serialize.h"

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int connect_to(const char* ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) { perror("inet_pton"); return -1; }
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return -1; }
    return fd;
}

// Print one serialized value; returns bytes consumed, or -1 on truncation.
static int64_t print_value(const uint8_t* p, size_t n, int depth) {
    auto indent = [&]() { for (int i = 0; i < depth; i++) std::cout << "  "; };
    if (n < 1) { std::cout << "(truncated)\n"; return -1; }
    switch (p[0]) {
        case SER_NIL:
            indent(); std::cout << "(nil)\n";
            return 1;
        case SER_INT: {
            if (n < 9) return -1;
            int64_t v; std::memcpy(&v, p + 1, 8);
            indent(); std::cout << "(integer) " << v << "\n";
            return 9;
        }
        case SER_DBL: {
            if (n < 9) return -1;
            double v; std::memcpy(&v, p + 1, 8);
            indent(); std::cout << "(double) " << v << "\n";
            return 9;
        }
        case SER_STR: {
            if (n < 5) return -1;
            uint32_t len; std::memcpy(&len, p + 1, 4);
            if (n < 5 + (size_t)len) return -1;
            indent(); std::cout << '"' << std::string((const char*)p + 5, len) << "\"\n";
            return 5 + (int64_t)len;
        }
        case SER_ERR: {
            if (n < 9) return -1;
            uint32_t code, len;
            std::memcpy(&code, p + 1, 4);
            std::memcpy(&len,  p + 5, 4);
            if (n < 9 + (size_t)len) return -1;
            indent(); std::cout << "(error " << code << ") "
                                << std::string((const char*)p + 9, len) << "\n";
            return 9 + (int64_t)len;
        }
        case SER_ARR: {
            if (n < 5) return -1;
            uint32_t cnt; std::memcpy(&cnt, p + 1, 4);
            indent(); std::cout << "(array of " << cnt << ")\n";
            int64_t used = 5;
            for (uint32_t i = 0; i < cnt; i++) {
                int64_t c = print_value(p + used, n - (size_t)used, depth + 1);
                if (c < 0) return -1;
                used += c;
            }
            return used;
        }
        default:
            std::cout << "(bad tag " << (int)p[0] << ")\n";
            return -1;
    }
}

static int send_command(int fd, const std::vector<std::string>& args) {
    // request body: [u32 nstr][u32 len][str]...
    std::vector<uint8_t> body;
    auto put = [&](uint32_t v){ uint8_t b[4]; write_u32(b, v); body.insert(body.end(), b, b + 4); };
    put((uint32_t)args.size());
    for (const auto& a : args) { put((uint32_t)a.size()); body.insert(body.end(), a.begin(), a.end()); }
    if (body.size() > MAX_MSG) { std::cerr << "command too long\n"; return -1; }

    uint8_t hdr[4]; write_u32(hdr, (uint32_t)body.size());
    if (write_all(fd, (char*)hdr, 4) != 0) return -1;
    if (write_all(fd, (char*)body.data(), body.size()) != 0) return -1;

    // reply: [u32 total][serialized value]
    if (read_full(fd, (char*)hdr, 4) != 0) { std::cerr << "server closed\n"; return -1; }
    uint32_t total = read_u32(hdr);
    if (total < 1 || total > MAX_MSG) { std::cerr << "bad reply\n"; return -1; }
    std::vector<uint8_t> resp(total);
    if (read_full(fd, (char*)resp.data(), total) != 0) return -1;
    print_value(resp.data(), resp.size(), 0);
    return 0;
}

static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out; std::istringstream is(line); std::string t;
    while (is >> t) out.push_back(t);
    return out;
}

int main(int argc, char** argv) {
    int fd = connect_to("127.0.0.1", 6379);
    if (fd < 0) return 1;

    if (argc >= 2) {                                  // one-shot: ./client set foo bar
        std::vector<std::string> args(argv + 1, argv + argc);
        int rc = send_command(fd, args);
        close(fd);
        return rc == 0 ? 0 : 1;
    }

    std::cout << "Connected. Type commands (e.g. 'set foo bar', 'keys'); 'exit' to quit.\n";
    std::string line;
    while (std::cout << "> " && std::getline(std::cin, line)) {
        if (line == "exit") break;
        auto args = split(line);
        if (!args.empty() && send_command(fd, args) != 0) break;
    }
    close(fd);
    return 0;
}

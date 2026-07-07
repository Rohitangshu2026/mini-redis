#include "protocol/wire.h"

static const size_t k_max_args = 200 * 1000;

int32_t parse_req(const uint8_t* data, size_t size, std::vector<std::string>& out) {
    const uint8_t* end = data + size;
    if (data + 4 > end) return -1;
    uint32_t nstr = read_u32(data); data += 4;
    if (nstr > k_max_args) return -1;

    while (out.size() < nstr) {
        if (data + 4 > end) return -1;
        uint32_t len = read_u32(data); data += 4;
        if (data + len > end) return -1;
        out.push_back(std::string((const char*)data, len));
        data += len;
    }
    if (data != end) return -1;   // trailing garbage
    return 0;
}

#include "protocol/wire.h"

// Cap on the argument count in one request. A malicious length prefix could
// otherwise claim four billion arguments and make the parser loop and
// allocate accordingly; anything near this limit is garbage by definition
// since MAX_MSG bounds the payload anyway.
static const size_t k_max_args = 200 * 1000;

int32_t parse_req(const uint8_t* data, size_t size, std::vector<std::string>& out){
    // The payload is [u32 nstr] followed by nstr length-prefixed strings.
    // Every read below is bounds-checked against `end` BEFORE dereferencing,
    // so a truncated or lying frame fails cleanly instead of over-reading.
    const uint8_t* end = data + size;
    if(data + 4 > end) return -1;
    uint32_t nstr = read_u32(data); data += 4;
    if(nstr > k_max_args) return -1;

    while(out.size() < nstr){
        if(data + 4 > end) return -1;              // no room for a length
        uint32_t len = read_u32(data); data += 4;
        if(data + len > end) return -1;            // length overruns buffer
        out.push_back(std::string((const char*)data, len));
        data += len;
    }
    if(data != end) return -1;   // trailing bytes after the last argument
    return 0;
}

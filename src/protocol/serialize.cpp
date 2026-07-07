#include "protocol/serialize.h"

#include <cstring>

static void put_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
static void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t b[4]; std::memcpy(b, &v, 4); out.insert(out.end(), b, b + 4);
}
static void put_i64(std::vector<uint8_t>& out, int64_t v) {
    uint8_t b[8]; std::memcpy(b, &v, 8); out.insert(out.end(), b, b + 8);
}
static void put_f64(std::vector<uint8_t>& out, double v) {
    uint8_t b[8]; std::memcpy(b, &v, 8); out.insert(out.end(), b, b + 8);
}

void out_nil(std::vector<uint8_t>& out) {
    put_u8(out, SER_NIL);
}
void out_str(std::vector<uint8_t>& out, const std::string& s) {
    put_u8(out, SER_STR);
    put_u32(out, (uint32_t)s.size());
    out.insert(out.end(), s.begin(), s.end());
}
void out_int(std::vector<uint8_t>& out, int64_t x) {
    put_u8(out, SER_INT);
    put_i64(out, x);
}
void out_dbl(std::vector<uint8_t>& out, double x) {
    put_u8(out, SER_DBL);
    put_f64(out, x);
}
void out_err(std::vector<uint8_t>& out, int32_t code, const std::string& msg) {
    put_u8(out, SER_ERR);
    put_u32(out, (uint32_t)code);
    put_u32(out, (uint32_t)msg.size());
    out.insert(out.end(), msg.begin(), msg.end());
}
void out_arr(std::vector<uint8_t>& out, uint32_t n) {
    put_u8(out, SER_ARR);
    put_u32(out, n);
}

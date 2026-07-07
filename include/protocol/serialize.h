#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Response value type tags (Ch 11). Every reply is one serialized value:
//   nil : [u8 SER_NIL]
//   err : [u8 SER_ERR][u32 code][u32 len][msg]
//   str : [u8 SER_STR][u32 len][bytes]
//   int : [u8 SER_INT][i64]
//   dbl : [u8 SER_DBL][f64]
//   arr : [u8 SER_ARR][u32 n][ n serialized values ]
enum {
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_DBL = 4,
    SER_ARR = 5,
};

// Error codes carried inside SER_ERR.
enum {
    ERR_UNKNOWN = 1,
    ERR_ARG     = 2,
    ERR_TYPE    = 3,
};

// Append a typed value to a response buffer.
void out_nil(std::vector<uint8_t>& out);
void out_str(std::vector<uint8_t>& out, const std::string& s);
void out_int(std::vector<uint8_t>& out, int64_t x);
void out_dbl(std::vector<uint8_t>& out, double x);
void out_err(std::vector<uint8_t>& out, int32_t code, const std::string& msg);
void out_arr(std::vector<uint8_t>& out, uint32_t n);   // header only; append n values after

#pragma once

#include "store/database.h"

#include <cstdint>
#include <string>
#include <vector>

// Execute one command against db; appends one serialized typed value to out.
void do_request(const std::vector<std::string>& cmd, Database& db, std::vector<uint8_t>& out);

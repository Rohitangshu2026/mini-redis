#pragma once

#include "store/hashtable.h"
#include <string>
#include <vector>
#include <cstdint>

// Execute one command against db; appends [u32 status][data] to out.
void do_request(const std::vector<std::string>& cmd, HMap& db, std::vector<uint8_t>& out);

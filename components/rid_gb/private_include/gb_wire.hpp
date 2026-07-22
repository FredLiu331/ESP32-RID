#pragma once

#include <cstdint>
#include <vector>

namespace rid::gb_wire {

inline void put_u16_le(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

inline void put_i32_le(std::vector<uint8_t> &out, int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    out.push_back(static_cast<uint8_t>(bits));
    out.push_back(static_cast<uint8_t>(bits >> 8));
    out.push_back(static_cast<uint8_t>(bits >> 16));
    out.push_back(static_cast<uint8_t>(bits >> 24));
}

inline void put_u48_le(std::vector<uint8_t> &out, uint64_t value) {
    for (uint8_t shift = 0; shift < 48; shift += 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

}  // namespace rid::gb_wire

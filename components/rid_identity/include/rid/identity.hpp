#pragma once

#include <cstdint>
#include <string_view>

#include "rid/model.hpp"

namespace rid {

TestIdentity derive_test_id(const DeviceId &device, std::string_view group_name,
                            uint32_t group_index, Protocol protocol);

}  // namespace rid

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "rid/config.hpp"

namespace rid {

constexpr uint32_t kStoredConfigMagic = 0x52494443U;
constexpr uint16_t kStoredConfigSchemaVersion = 1;
constexpr size_t kStoredConfigHeaderSize = 16;

enum class NvsStoreError : uint8_t {
    None,
    NotFound,
    InvalidConfig,
    InvalidGeneration,
    PayloadTooLarge,
    StorageError,
    Corrupt,
    IncompatibleVersion,
};

struct LoadedConfig {
    SystemConfig config;
    uint32_t generation{0};
};

struct NvsLoadResult {
    NvsStoreError error{NvsStoreError::NotFound};
    std::optional<LoadedConfig> value;

    bool ok() const { return error == NvsStoreError::None && value.has_value(); }
};

class NvsConfigStore {
public:
    explicit NvsConfigStore(std::string namespace_name = "rid_cfg");

    NvsStoreError save(const SystemConfig &config, uint32_t generation);
    NvsLoadResult load() const;

private:
    std::string namespace_name_;
};

}  // namespace rid

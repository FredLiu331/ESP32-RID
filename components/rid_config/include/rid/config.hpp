#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rid/model.hpp"

namespace rid {

constexpr size_t kMessageKindCount = static_cast<size_t>(MessageKind::OperatorId) + 1;
constexpr size_t kMaxAircraft = 50;

struct GeoPoint {
    double latitude_deg;
    double longitude_deg;
};

struct GroupConfig {
    GroupConfig() = default;
    GroupConfig(std::string group_name, uint16_t aircraft_count, Protocol rid_protocol,
                Transport radio_transport)
        : name(std::move(group_name)),
          count(aircraft_count),
          protocol(rid_protocol),
          transport(radio_transport) {}

    std::string name;
    uint16_t count{0};
    Protocol protocol{Protocol::OpenDroneId};
    Transport transport{Transport::Ble4};
    WifiMode wifi_mode{WifiMode::Beacon};
    std::optional<std::chrono::milliseconds> default_period;
    std::array<std::optional<std::chrono::milliseconds>, kMessageKindCount> period_overrides{};
};

struct SystemConfig {
    std::optional<GeoPoint> site;
    std::chrono::milliseconds default_period{1000};
    std::vector<GroupConfig> groups;
};

struct AircraftConfig {
    TestIdentity identity;
    uint16_t group_index;
    uint16_t instance_index;
    Protocol protocol;
    Transport transport;
    WifiMode wifi_mode;
};

enum class ConfigError : uint8_t {
    None,
    MissingSite,
    InvalidSite,
    NoAircraft,
    TooManyAircraft,
    InvalidGroupName,
    DuplicateGroupName,
    InvalidGroupCount,
    InvalidPeriod,
    UnsupportedCombination,
    IdentityDerivationFailed,
};

class ValidationResult {
public:
    explicit ValidationResult(ConfigError error = ConfigError::None) : error_(error) {}

    bool ok() const { return error_ == ConfigError::None; }
    ConfigError error() const { return error_; }

private:
    ConfigError error_;
};

class ExpandResult {
public:
    explicit ExpandResult(ConfigError error) : error_(error) {}
    explicit ExpandResult(std::vector<AircraftConfig> aircraft)
        : error_(ConfigError::None), aircraft_(std::move(aircraft)) {}

    bool ok() const { return error_ == ConfigError::None; }
    ConfigError error() const { return error_; }
    const std::vector<AircraftConfig> &value() const { return aircraft_; }

private:
    ConfigError error_;
    std::vector<AircraftConfig> aircraft_;
};

SystemConfig default_config();
ValidationResult validate(const SystemConfig &config);
ExpandResult validate_and_expand(const SystemConfig &config, const DeviceId &device);
std::chrono::milliseconds effective_period(const SystemConfig &system, const GroupConfig &group,
                                           MessageKind kind);

}  // namespace rid

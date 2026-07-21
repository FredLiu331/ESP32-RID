#include "rid/config.hpp"

#include <cmath>
#include <set>
#include <utility>

#include "rid/identity.hpp"

namespace rid {
namespace {

constexpr int64_t kMinPeriodMs = 100;
constexpr int64_t kMaxPeriodMs = 60000;

bool valid_period(std::chrono::milliseconds period) {
    return period.count() >= kMinPeriodMs && period.count() <= kMaxPeriodMs;
}

bool valid_site(const GeoPoint &site) {
    return std::isfinite(site.latitude_deg) && site.latitude_deg >= -90.0 &&
           site.latitude_deg <= 90.0 && std::isfinite(site.longitude_deg) &&
           site.longitude_deg >= -180.0 && site.longitude_deg <= 180.0;
}

bool supported(const GroupConfig &group) {
    const bool known_wifi_mode =
        group.wifi_mode == WifiMode::Beacon || group.wifi_mode == WifiMode::Nan;
    const bool wifi = group.transport == Transport::Wifi24 || group.transport == Transport::Wifi58;
    const bool ble = group.transport == Transport::Ble4 || group.transport == Transport::Ble5;
    if (!known_wifi_mode || (!wifi && !ble)) return false;
    if (ble && group.wifi_mode != WifiMode::Beacon) return false;

    if (group.protocol == Protocol::Gb46750) {
        return wifi && group.wifi_mode == WifiMode::Beacon;
    }
    return group.protocol == Protocol::OpenDroneId;
}

}  // namespace

SystemConfig default_config() {
    return SystemConfig{};
}

ValidationResult validate(const SystemConfig &config) {
    if (!config.site.has_value()) return ValidationResult{ConfigError::MissingSite};
    if (!valid_site(*config.site)) return ValidationResult{ConfigError::InvalidSite};
    if (!valid_period(config.default_period)) return ValidationResult{ConfigError::InvalidPeriod};

    size_t total = 0;
    std::set<std::string> names;
    for (const auto &group : config.groups) {
        if (group.name.empty()) return ValidationResult{ConfigError::InvalidGroupName};
        if (group.count == 0) return ValidationResult{ConfigError::InvalidGroupCount};
        if (!names.insert(group.name).second) {
            return ValidationResult{ConfigError::DuplicateGroupName};
        }
        if (!supported(group)) return ValidationResult{ConfigError::UnsupportedCombination};
        if (!valid(group.trajectory)) return ValidationResult{ConfigError::InvalidTrajectory};
        if (group.default_period.has_value() && !valid_period(*group.default_period)) {
            return ValidationResult{ConfigError::InvalidPeriod};
        }
        for (const auto &period : group.period_overrides) {
            if (period.has_value() && !valid_period(*period)) {
                return ValidationResult{ConfigError::InvalidPeriod};
            }
        }
        total += group.count;
        if (total > kMaxAircraft) return ValidationResult{ConfigError::TooManyAircraft};
    }

    if (total == 0) return ValidationResult{ConfigError::NoAircraft};
    return ValidationResult{};
}

ExpandResult validate_and_expand(const SystemConfig &config, const DeviceId &device) {
    const auto validation = validate(config);
    if (!validation.ok()) return ExpandResult{validation.error()};

    size_t total = 0;
    for (const auto &group : config.groups) total += group.count;

    std::vector<AircraftConfig> aircraft;
    aircraft.reserve(total);
    for (size_t group_index = 0; group_index < config.groups.size(); ++group_index) {
        const auto &group = config.groups[group_index];
        for (uint16_t instance_index = 0; instance_index < group.count; ++instance_index) {
            auto identity = derive_test_id(device, group.name, instance_index, group.protocol);
            if (identity.value.empty()) return ExpandResult{ConfigError::IdentityDerivationFailed};
            aircraft.push_back(AircraftConfig{
                std::move(identity),
                static_cast<uint16_t>(group_index),
                instance_index,
                group.protocol,
                group.transport,
                group.wifi_mode,
            });
        }
    }
    return ExpandResult{std::move(aircraft)};
}

std::chrono::milliseconds effective_period(const SystemConfig &system, const GroupConfig &group,
                                           MessageKind kind) {
    const size_t index = static_cast<size_t>(kind);
    if (index < group.period_overrides.size() && group.period_overrides[index].has_value()) {
        return *group.period_overrides[index];
    }
    if (group.default_period.has_value()) return *group.default_period;
    return system.default_period;
}

}  // namespace rid

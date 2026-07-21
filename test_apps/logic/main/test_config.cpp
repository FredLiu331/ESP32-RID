#include <chrono>
#include <set>

#include "rid/config.hpp"
#include "unity.h"

using namespace std::chrono_literals;

namespace {

constexpr rid::DeviceId kDevice{{1, 2, 3, 4, 5, 6}};

rid::SystemConfig base_config() {
    auto config = rid::default_config();
    config.site = rid::GeoPoint{31.2304, 121.4737};
    return config;
}

}  // namespace

TEST_CASE("groups expand from one to fifty unique aircraft", "[config]") {
    auto config = base_config();
    config.groups = {
        rid::GroupConfig{"ble", 10, rid::Protocol::OpenDroneId, rid::Transport::Ble5},
        rid::GroupConfig{"wifi", 40, rid::Protocol::Gb46750, rid::Transport::Wifi58},
    };

    const auto expanded = rid::validate_and_expand(config, kDevice);
    TEST_ASSERT_TRUE(expanded.ok());
    TEST_ASSERT_EQUAL_UINT32(50, expanded.value().size());

    std::set<std::string> identities;
    for (const auto &aircraft : expanded.value()) identities.insert(aircraft.identity.value);
    TEST_ASSERT_EQUAL_UINT32(50, identities.size());

    config.groups = {
        rid::GroupConfig{"single", 1, rid::Protocol::OpenDroneId, rid::Transport::Ble4},
    };
    TEST_ASSERT_EQUAL_UINT32(1, rid::validate_and_expand(config, kDevice).value().size());
}

TEST_CASE("fleet size and group names are validated", "[config]") {
    auto config = base_config();
    TEST_ASSERT_EQUAL(rid::ConfigError::NoAircraft, rid::validate(config).error());

    config.groups = {
        rid::GroupConfig{"first", 25, rid::Protocol::OpenDroneId, rid::Transport::Ble4},
        rid::GroupConfig{"second", 26, rid::Protocol::OpenDroneId, rid::Transport::Ble5},
    };
    TEST_ASSERT_EQUAL(rid::ConfigError::TooManyAircraft, rid::validate(config).error());

    config.groups[1].name = "first";
    config.groups[1].count = 25;
    TEST_ASSERT_EQUAL(rid::ConfigError::DuplicateGroupName, rid::validate(config).error());

    config.groups[1].name.clear();
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidGroupName, rid::validate(config).error());

    config.groups = {
        rid::GroupConfig{"empty", 0, rid::Protocol::OpenDroneId, rid::Transport::Ble4},
    };
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidGroupCount, rid::validate(config).error());
}

TEST_CASE("site and all period levels are validated", "[config]") {
    auto config = rid::default_config();
    config.groups = {
        rid::GroupConfig{"ble", 1, rid::Protocol::OpenDroneId, rid::Transport::Ble5},
    };
    TEST_ASSERT_EQUAL(rid::ConfigError::MissingSite, rid::validate(config).error());

    config.site = rid::GeoPoint{91.0, 121.4737};
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidSite, rid::validate(config).error());
    config.site = rid::GeoPoint{31.2304, 121.4737};

    config.default_period = 99ms;
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidPeriod, rid::validate(config).error());
    config.default_period = 60s + 1ms;
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidPeriod, rid::validate(config).error());
    config.default_period = 3s;

    config.groups[0].default_period = 2s;
    config.groups[0].period_overrides[static_cast<size_t>(rid::MessageKind::Location)] = 500ms;
    TEST_ASSERT_TRUE(rid::validate(config).ok());
    TEST_ASSERT_EQUAL_INT32(500, rid::effective_period(
                                     config, config.groups[0], rid::MessageKind::Location)
                                     .count());
    TEST_ASSERT_EQUAL_INT32(2000, rid::effective_period(
                                      config, config.groups[0], rid::MessageKind::BasicId)
                                      .count());
    config.groups[0].default_period.reset();
    TEST_ASSERT_EQUAL_INT32(3000, rid::effective_period(
                                      config, config.groups[0], rid::MessageKind::BasicId)
                                      .count());

    config.groups[0].period_overrides[static_cast<size_t>(rid::MessageKind::Location)] = 61s;
    TEST_ASSERT_EQUAL(rid::ConfigError::InvalidPeriod, rid::validate(config).error());
}

TEST_CASE("protocol transport and wifi mode combinations are enforced", "[config]") {
    auto config = base_config();
    config.groups = {
        rid::GroupConfig{"gb_ble", 1, rid::Protocol::Gb46750, rid::Transport::Ble4},
    };
    TEST_ASSERT_EQUAL(rid::ConfigError::UnsupportedCombination, rid::validate(config).error());

    config.groups[0] =
        rid::GroupConfig{"gb_nan", 1, rid::Protocol::Gb46750, rid::Transport::Wifi24};
    config.groups[0].wifi_mode = rid::WifiMode::Nan;
    TEST_ASSERT_EQUAL(rid::ConfigError::UnsupportedCombination, rid::validate(config).error());

    config.groups[0] =
        rid::GroupConfig{"odid_nan", 1, rid::Protocol::OpenDroneId, rid::Transport::Wifi58};
    config.groups[0].wifi_mode = rid::WifiMode::Nan;
    TEST_ASSERT_TRUE(rid::validate(config).ok());

    config.groups[0].transport = static_cast<rid::Transport>(0xff);
    TEST_ASSERT_EQUAL(rid::ConfigError::UnsupportedCombination, rid::validate(config).error());
    config.groups[0].transport = rid::Transport::Wifi24;
    config.groups[0].protocol = static_cast<rid::Protocol>(0xff);
    TEST_ASSERT_EQUAL(rid::ConfigError::UnsupportedCombination, rid::validate(config).error());

    const auto defaults = rid::GroupConfig{"default_wifi", 1, rid::Protocol::OpenDroneId,
                                           rid::Transport::Wifi24};
    TEST_ASSERT_EQUAL(rid::WifiMode::Beacon, defaults.wifi_mode);
}

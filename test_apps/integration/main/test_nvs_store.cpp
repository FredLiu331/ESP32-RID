#include <array>
#include <chrono>
#include <vector>

#include "esp_crc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "rid/nvs_store.hpp"
#include "unity.h"

using namespace std::chrono_literals;

namespace {

constexpr const char *kNamespace = "rid_nvs_test";

void initialize_clean_nvs() {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ESP_OK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    TEST_ESP_OK(result);
    nvs_handle_t handle = 0;
    result = nvs_open(kNamespace, NVS_READWRITE, &handle);
    TEST_ESP_OK(result);
    TEST_ESP_OK(nvs_erase_all(handle));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
}

rid::SystemConfig sample_config() {
    rid::SystemConfig config;
    config.site = rid::GeoPoint{31.2304, 121.4737};
    config.default_period = 1500ms;
    rid::GroupConfig odid{"odid_ble5", 10, rid::Protocol::OpenDroneId,
                          rid::Transport::Ble5};
    odid.trajectory = rid::circle_trajectory(80.0F, 12.5F, 100.0F);
    odid.default_period = 2s;
    odid.period_overrides[static_cast<size_t>(rid::MessageKind::Location)] = 500ms;
    rid::GroupConfig gb{"gb_wifi58", 20, rid::Protocol::Gb46750, rid::Transport::Wifi58};
    gb.trajectory = rid::waypoint_trajectory(
        {{0.0F, 0.0F, 80.0F}, {100.0F, 20.0F, 90.0F}, {20.0F, 120.0F, 85.0F}}, 8.0F);
    config.groups = {odid, gb};
    return config;
}

void assert_trajectory_equal(const rid::TrajectoryConfig &expected,
                             const rid::TrajectoryConfig &actual) {
    TEST_ASSERT_EQUAL(expected.type, actual.type);
    TEST_ASSERT_EQUAL_FLOAT(expected.altitude_m, actual.altitude_m);
    TEST_ASSERT_EQUAL_FLOAT(expected.speed_mps, actual.speed_mps);
    TEST_ASSERT_EQUAL_FLOAT(expected.heading_deg, actual.heading_deg);
    TEST_ASSERT_EQUAL_FLOAT(expected.primary_size_m, actual.primary_size_m);
    TEST_ASSERT_EQUAL_FLOAT(expected.secondary_size_m, actual.secondary_size_m);
    TEST_ASSERT_EQUAL_FLOAT(expected.vertical_speed_mps, actual.vertical_speed_mps);
    TEST_ASSERT_EQUAL_UINT32(expected.cruise_time_ms, actual.cruise_time_ms);
    TEST_ASSERT_EQUAL_UINT32(expected.waypoints.size(), actual.waypoints.size());
    for (size_t index = 0; index < expected.waypoints.size(); ++index) {
        TEST_ASSERT_EQUAL_FLOAT(expected.waypoints[index].east_m, actual.waypoints[index].east_m);
        TEST_ASSERT_EQUAL_FLOAT(expected.waypoints[index].north_m,
                                actual.waypoints[index].north_m);
        TEST_ASSERT_EQUAL_FLOAT(expected.waypoints[index].up_m, actual.waypoints[index].up_m);
    }
}

void assert_config_equal(const rid::SystemConfig &expected, const rid::SystemConfig &actual) {
    TEST_ASSERT_EQUAL(expected.site.has_value(), actual.site.has_value());
    TEST_ASSERT_EQUAL_DOUBLE(expected.site->latitude_deg, actual.site->latitude_deg);
    TEST_ASSERT_EQUAL_DOUBLE(expected.site->longitude_deg, actual.site->longitude_deg);
    TEST_ASSERT_EQUAL_INT64(expected.default_period.count(), actual.default_period.count());
    TEST_ASSERT_EQUAL_UINT32(expected.groups.size(), actual.groups.size());
    for (size_t index = 0; index < expected.groups.size(); ++index) {
        const auto &left = expected.groups[index];
        const auto &right = actual.groups[index];
        TEST_ASSERT_EQUAL_STRING(left.name.c_str(), right.name.c_str());
        TEST_ASSERT_EQUAL_UINT16(left.count, right.count);
        TEST_ASSERT_EQUAL(left.protocol, right.protocol);
        TEST_ASSERT_EQUAL(left.transport, right.transport);
        TEST_ASSERT_EQUAL(left.wifi_mode, right.wifi_mode);
        assert_trajectory_equal(left.trajectory, right.trajectory);
        TEST_ASSERT_EQUAL(left.default_period.has_value(), right.default_period.has_value());
        if (left.default_period.has_value()) {
            TEST_ASSERT_EQUAL_INT64(left.default_period->count(), right.default_period->count());
        }
        for (size_t period = 0; period < left.period_overrides.size(); ++period) {
            TEST_ASSERT_EQUAL(left.period_overrides[period].has_value(),
                              right.period_overrides[period].has_value());
            if (left.period_overrides[period].has_value()) {
                TEST_ASSERT_EQUAL_INT64(left.period_overrides[period]->count(),
                                        right.period_overrides[period]->count());
            }
        }
    }
}

void corrupt_blob(const char *key) {
    nvs_handle_t handle = 0;
    TEST_ESP_OK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    size_t size = 0;
    TEST_ESP_OK(nvs_get_blob(handle, key, nullptr, &size));
    std::vector<uint8_t> blob(size);
    TEST_ESP_OK(nvs_get_blob(handle, key, blob.data(), &size));
    blob.back() ^= 0x80;
    TEST_ESP_OK(nvs_set_blob(handle, key, blob.data(), blob.size()));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
}

void write_u16_le(uint8_t *destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8U);
}

void write_u32_le(uint8_t *destination, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) {
        destination[index] = static_cast<uint8_t>(value >> (index * 8U));
    }
}

}  // namespace

TEST_CASE("NVS store reports no configuration in an empty namespace", "[nvs]") {
    initialize_clean_nvs();
    const rid::NvsLoadResult loaded = rid::NvsConfigStore{kNamespace}.load();
    TEST_ASSERT_EQUAL(rid::NvsStoreError::NotFound, loaded.error);
    TEST_ASSERT_FALSE(loaded.value.has_value());
}

TEST_CASE("NVS configuration round trip preserves every field", "[nvs]") {
    initialize_clean_nvs();
    const auto expected = sample_config();
    rid::NvsConfigStore store{kNamespace};
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(expected, 42));

    const rid::NvsLoadResult loaded = rid::NvsConfigStore{kNamespace}.load();
    TEST_ASSERT_TRUE(loaded.ok());
    TEST_ASSERT_EQUAL_UINT32(42, loaded.value->generation);
    assert_config_equal(expected, loaded.value->config);

}

TEST_CASE("NVS dual slot falls back after interrupted or corrupted update", "[nvs]") {
    initialize_clean_nvs();
    rid::NvsConfigStore store{kNamespace};
    auto original = sample_config();
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(original, 7));
    auto updated = original;
    updated.groups[0].count = 11;
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(updated, 8));
    corrupt_blob("cfg_b");

    const auto loaded = store.load();
    TEST_ASSERT_TRUE(loaded.ok());
    TEST_ASSERT_EQUAL_UINT32(7, loaded.value->generation);
    assert_config_equal(original, loaded.value->config);

    nvs_handle_t handle = 0;
    TEST_ESP_OK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    const uint8_t partial[] = {0x43, 0x44, 0x49};
    TEST_ESP_OK(nvs_set_blob(handle, "cfg_b", partial, sizeof(partial)));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
    TEST_ASSERT_EQUAL_UINT32(7, store.load().value->generation);
}

TEST_CASE("NVS store rejects incompatible schema and invalid configuration", "[nvs]") {
    initialize_clean_nvs();
    std::array<uint8_t, rid::kStoredConfigHeaderSize> blob{};
    write_u32_le(blob.data(), rid::kStoredConfigMagic);
    write_u16_le(blob.data() + 4, rid::kStoredConfigSchemaVersion + 1);
    write_u16_le(blob.data() + 6, 0);
    write_u32_le(blob.data() + 8, 1);
    write_u32_le(blob.data() + 12, esp_crc32_le(0, blob.data(), 12));
    nvs_handle_t handle = 0;
    TEST_ESP_OK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    TEST_ESP_OK(nvs_set_blob(handle, "cfg_a", blob.data(), blob.size()));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);

    rid::NvsConfigStore store{kNamespace};
    TEST_ASSERT_EQUAL(rid::NvsStoreError::IncompatibleVersion, store.load().error);
    TEST_ASSERT_EQUAL(rid::NvsStoreError::InvalidConfig, store.save(rid::default_config(), 2));
}

TEST_CASE("NVS generation is monotonic across uint32 wrap", "[nvs]") {
    initialize_clean_nvs();
    rid::NvsConfigStore store{kNamespace};
    const auto config = sample_config();
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(config, UINT32_MAX));
    TEST_ASSERT_EQUAL(rid::NvsStoreError::InvalidGeneration, store.save(config, UINT32_MAX));
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(config, 0));
    TEST_ASSERT_EQUAL_UINT32(0, store.load().value->generation);
}

TEST_CASE("NVS store rejects ambiguous generation and preserves unknown schema", "[nvs]") {
    initialize_clean_nvs();
    rid::NvsConfigStore store{kNamespace};
    const auto config = sample_config();
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(config, 1));

    nvs_handle_t handle = 0;
    TEST_ESP_OK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    size_t size = 0;
    TEST_ESP_OK(nvs_get_blob(handle, "cfg_a", nullptr, &size));
    std::vector<uint8_t> blob(size);
    TEST_ESP_OK(nvs_get_blob(handle, "cfg_a", blob.data(), &size));
    TEST_ESP_OK(nvs_set_blob(handle, "cfg_b", blob.data(), blob.size()));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
    TEST_ASSERT_EQUAL(rid::NvsStoreError::Corrupt, store.load().error);
    TEST_ASSERT_EQUAL(rid::NvsStoreError::InvalidGeneration, store.save(config, 2));

    initialize_clean_nvs();
    TEST_ASSERT_EQUAL(rid::NvsStoreError::None, store.save(config, 4));
    TEST_ESP_OK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    std::array<uint8_t, rid::kStoredConfigHeaderSize> future{};
    write_u32_le(future.data(), rid::kStoredConfigMagic);
    write_u16_le(future.data() + 4, rid::kStoredConfigSchemaVersion + 1);
    write_u32_le(future.data() + 8, 5);
    write_u32_le(future.data() + 12, esp_crc32_le(0, future.data(), 12));
    TEST_ESP_OK(nvs_set_blob(handle, "cfg_b", future.data(), future.size()));
    TEST_ESP_OK(nvs_commit(handle));
    nvs_close(handle);
    TEST_ASSERT_TRUE(store.load().ok());
    TEST_ASSERT_EQUAL(rid::NvsStoreError::IncompatibleVersion, store.save(config, 6));
}

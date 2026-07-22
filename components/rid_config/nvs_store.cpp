#include "rid/nvs_store.hpp"

#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "esp_crc.h"
#include "nvs.h"

namespace rid {
namespace {

constexpr size_t kMaxPayloadSize = 32 * 1024;
constexpr size_t kMaxNameSize = 64;
constexpr size_t kMaxWaypoints = 64;
constexpr const char *kSlots[] = {"cfg_a", "cfg_b"};

struct SlotResult {
    NvsStoreError error{NvsStoreError::NotFound};
    std::optional<LoadedConfig> value;
    size_t slot{0};
};

class Writer {
public:
    template <typename T>
    void scalar(T value) {
        if constexpr (std::is_integral_v<T>) {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = static_cast<Unsigned>(value);
            for (size_t index = 0; index < sizeof(T); ++index) {
                data_.push_back(static_cast<uint8_t>(bits >> (index * 8U)));
            }
        } else if constexpr (std::is_same_v<T, float>) {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            scalar(bits);
        } else if constexpr (std::is_same_v<T, double>) {
            uint64_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            scalar(bits);
        }
    }

    void bytes(const uint8_t *data, size_t size) {
        data_.insert(data_.end(), data, data + size);
    }

    const std::vector<uint8_t> &data() const { return data_; }

private:
    std::vector<uint8_t> data_;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t> &data) : data_(data) {}

    template <typename T>
    bool scalar(T &value) {
        if (offset_ + sizeof(T) > data_.size()) return false;
        if constexpr (std::is_integral_v<T>) {
            using Unsigned = std::make_unsigned_t<T>;
            Unsigned bits = 0;
            for (size_t index = 0; index < sizeof(T); ++index) {
                bits |= static_cast<Unsigned>(data_[offset_++]) << (index * 8U);
            }
            value = static_cast<T>(bits);
        } else if constexpr (std::is_same_v<T, float>) {
            uint32_t bits = 0;
            if (!scalar(bits)) return false;
            std::memcpy(&value, &bits, sizeof(value));
        } else if constexpr (std::is_same_v<T, double>) {
            uint64_t bits = 0;
            if (!scalar(bits)) return false;
            std::memcpy(&value, &bits, sizeof(value));
        }
        return true;
    }

    bool string(std::string &value, size_t size) {
        if (offset_ + size > data_.size()) return false;
        value.assign(reinterpret_cast<const char *>(data_.data() + offset_), size);
        offset_ += size;
        return true;
    }

    bool done() const { return offset_ == data_.size(); }

private:
    const std::vector<uint8_t> &data_;
    size_t offset_{0};
};

void write_optional_period(Writer &writer,
                           const std::optional<std::chrono::milliseconds> &period) {
    const int32_t value = period.has_value() ? static_cast<int32_t>(period->count()) : -1;
    writer.scalar(value);
}

bool read_optional_period(Reader &reader,
                          std::optional<std::chrono::milliseconds> &period) {
    int32_t value = 0;
    if (!reader.scalar(value) || value < -1) return false;
    if (value == -1) {
        period.reset();
    } else {
        period = std::chrono::milliseconds{value};
    }
    return true;
}

NvsStoreError encode_config(const SystemConfig &config, std::vector<uint8_t> &payload) {
    if (!validate(config).ok()) return NvsStoreError::InvalidConfig;
    if (config.groups.size() > std::numeric_limits<uint16_t>::max()) {
        return NvsStoreError::PayloadTooLarge;
    }

    Writer writer;
    writer.scalar<uint8_t>(config.site.has_value() ? 1 : 0);
    if (config.site.has_value()) {
        writer.scalar(config.site->latitude_deg);
        writer.scalar(config.site->longitude_deg);
    }
    writer.scalar<int32_t>(static_cast<int32_t>(config.default_period.count()));
    writer.scalar<uint16_t>(static_cast<uint16_t>(config.groups.size()));
    for (const auto &group : config.groups) {
        if (group.name.size() > kMaxNameSize || group.trajectory.waypoints.size() > kMaxWaypoints) {
            return NvsStoreError::PayloadTooLarge;
        }
        writer.scalar<uint16_t>(static_cast<uint16_t>(group.name.size()));
        writer.bytes(reinterpret_cast<const uint8_t *>(group.name.data()), group.name.size());
        writer.scalar(group.count);
        writer.scalar<uint8_t>(static_cast<uint8_t>(group.protocol));
        writer.scalar<uint8_t>(static_cast<uint8_t>(group.transport));
        writer.scalar<uint8_t>(static_cast<uint8_t>(group.wifi_mode));
        writer.scalar<uint8_t>(static_cast<uint8_t>(group.trajectory.type));
        writer.scalar(group.trajectory.altitude_m);
        writer.scalar(group.trajectory.speed_mps);
        writer.scalar(group.trajectory.heading_deg);
        writer.scalar(group.trajectory.primary_size_m);
        writer.scalar(group.trajectory.secondary_size_m);
        writer.scalar(group.trajectory.vertical_speed_mps);
        writer.scalar(group.trajectory.cruise_time_ms);
        writer.scalar<uint16_t>(static_cast<uint16_t>(group.trajectory.waypoints.size()));
        for (const auto &point : group.trajectory.waypoints) {
            writer.scalar(point.east_m);
            writer.scalar(point.north_m);
            writer.scalar(point.up_m);
        }
        write_optional_period(writer, group.default_period);
        for (const auto &period : group.period_overrides) write_optional_period(writer, period);
    }
    if (writer.data().size() > kMaxPayloadSize) return NvsStoreError::PayloadTooLarge;
    payload = writer.data();
    return NvsStoreError::None;
}

NvsStoreError decode_config(const std::vector<uint8_t> &payload, SystemConfig &config) {
    Reader reader(payload);
    uint8_t has_site = 0;
    int32_t default_period = 0;
    uint16_t group_count = 0;
    if (!reader.scalar(has_site) || has_site > 1) return NvsStoreError::Corrupt;
    if (has_site != 0) {
        GeoPoint site{};
        if (!reader.scalar(site.latitude_deg) || !reader.scalar(site.longitude_deg)) {
            return NvsStoreError::Corrupt;
        }
        config.site = site;
    }
    if (!reader.scalar(default_period) || !reader.scalar(group_count) ||
        group_count > kMaxAircraft) {
        return NvsStoreError::Corrupt;
    }
    config.default_period = std::chrono::milliseconds{default_period};
    config.groups.reserve(group_count);
    for (uint16_t index = 0; index < group_count; ++index) {
        GroupConfig group;
        uint16_t name_size = 0;
        uint8_t protocol = 0, transport = 0, wifi_mode = 0, trajectory_type = 0;
        uint16_t waypoint_count = 0;
        if (!reader.scalar(name_size) || name_size > kMaxNameSize ||
            !reader.string(group.name, name_size) || !reader.scalar(group.count) ||
            !reader.scalar(protocol) || !reader.scalar(transport) ||
            !reader.scalar(wifi_mode) || !reader.scalar(trajectory_type) ||
            !reader.scalar(group.trajectory.altitude_m) ||
            !reader.scalar(group.trajectory.speed_mps) ||
            !reader.scalar(group.trajectory.heading_deg) ||
            !reader.scalar(group.trajectory.primary_size_m) ||
            !reader.scalar(group.trajectory.secondary_size_m) ||
            !reader.scalar(group.trajectory.vertical_speed_mps) ||
            !reader.scalar(group.trajectory.cruise_time_ms) ||
            !reader.scalar(waypoint_count) || waypoint_count > kMaxWaypoints) {
            return NvsStoreError::Corrupt;
        }
        group.protocol = static_cast<Protocol>(protocol);
        group.transport = static_cast<Transport>(transport);
        group.wifi_mode = static_cast<WifiMode>(wifi_mode);
        group.trajectory.type = static_cast<TrajectoryType>(trajectory_type);
        group.trajectory.waypoints.reserve(waypoint_count);
        for (uint16_t waypoint = 0; waypoint < waypoint_count; ++waypoint) {
            EnuPoint point{};
            if (!reader.scalar(point.east_m) || !reader.scalar(point.north_m) ||
                !reader.scalar(point.up_m)) {
                return NvsStoreError::Corrupt;
            }
            group.trajectory.waypoints.push_back(point);
        }
        if (!read_optional_period(reader, group.default_period)) return NvsStoreError::Corrupt;
        for (auto &period : group.period_overrides) {
            if (!read_optional_period(reader, period)) return NvsStoreError::Corrupt;
        }
        config.groups.push_back(std::move(group));
    }
    if (!reader.done() || !validate(config).ok()) return NvsStoreError::Corrupt;
    return NvsStoreError::None;
}

uint32_t calculate_crc(const uint8_t *header, const uint8_t *payload, size_t payload_size) {
    uint32_t crc = esp_crc32_le(0, header, 12);
    return esp_crc32_le(crc, payload, payload_size);
}

uint16_t read_u16_le(const uint8_t *data) {
    return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1]) << 8U;
}

uint32_t read_u32_le(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8U |
           static_cast<uint32_t>(data[2]) << 16U | static_cast<uint32_t>(data[3]) << 24U;
}

void append_header(Writer &writer, uint16_t payload_size, uint32_t generation,
                   uint32_t crc) {
    writer.scalar(kStoredConfigMagic);
    writer.scalar(kStoredConfigSchemaVersion);
    writer.scalar(payload_size);
    writer.scalar(generation);
    writer.scalar(crc);
}

SlotResult read_slot(nvs_handle_t handle, size_t slot) {
    size_t size = 0;
    esp_err_t result = nvs_get_blob(handle, kSlots[slot], nullptr, &size);
    if (result == ESP_ERR_NVS_NOT_FOUND) return {NvsStoreError::NotFound, std::nullopt, slot};
    if (result != ESP_OK) return {NvsStoreError::StorageError, std::nullopt, slot};
    if (size < kStoredConfigHeaderSize || size > kStoredConfigHeaderSize + kMaxPayloadSize) {
        return {NvsStoreError::Corrupt, std::nullopt, slot};
    }
    std::vector<uint8_t> blob(size);
    result = nvs_get_blob(handle, kSlots[slot], blob.data(), &size);
    if (result != ESP_OK) return {NvsStoreError::StorageError, std::nullopt, slot};

    const uint32_t magic = read_u32_le(blob.data());
    const uint16_t version = read_u16_le(blob.data() + 4);
    const uint16_t payload_size = read_u16_le(blob.data() + 6);
    const uint32_t generation = read_u32_le(blob.data() + 8);
    const uint32_t stored_crc = read_u32_le(blob.data() + 12);
    if (magic != kStoredConfigMagic || payload_size != size - kStoredConfigHeaderSize) {
        return {NvsStoreError::Corrupt, std::nullopt, slot};
    }
    if (stored_crc != calculate_crc(blob.data(), blob.data() + kStoredConfigHeaderSize,
                                    payload_size)) {
        return {NvsStoreError::Corrupt, std::nullopt, slot};
    }
    if (version != kStoredConfigSchemaVersion) {
        return {NvsStoreError::IncompatibleVersion, std::nullopt, slot};
    }
    std::vector<uint8_t> payload(blob.begin() + kStoredConfigHeaderSize, blob.end());
    SystemConfig config;
    const NvsStoreError decode_error = decode_config(payload, config);
    if (decode_error != NvsStoreError::None) return {decode_error, std::nullopt, slot};
    return {NvsStoreError::None, LoadedConfig{std::move(config), generation}, slot};
}

bool newer(uint32_t left, uint32_t right) {
    const uint32_t distance = left - right;
    return distance != 0 && distance < 0x80000000U;
}

SlotResult choose_slot(const SlotResult &left, const SlotResult &right) {
    if (left.value.has_value() && right.value.has_value()) {
        const uint32_t distance = right.value->generation - left.value->generation;
        if (distance == 0 || distance == 0x80000000U) {
            return {NvsStoreError::Corrupt, std::nullopt, 0};
        }
        return newer(right.value->generation, left.value->generation) ? right : left;
    }
    if (left.value.has_value()) return left;
    if (right.value.has_value()) return right;
    if (left.error == NvsStoreError::StorageError || right.error == NvsStoreError::StorageError) {
        return {NvsStoreError::StorageError, std::nullopt, 0};
    }
    if (left.error == NvsStoreError::IncompatibleVersion ||
        right.error == NvsStoreError::IncompatibleVersion) {
        return {NvsStoreError::IncompatibleVersion, std::nullopt, 0};
    }
    if (left.error == NvsStoreError::Corrupt || right.error == NvsStoreError::Corrupt) {
        return {NvsStoreError::Corrupt, std::nullopt, 0};
    }
    return {NvsStoreError::NotFound, std::nullopt, 0};
}

}  // namespace

NvsConfigStore::NvsConfigStore(std::string namespace_name)
    : namespace_name_(std::move(namespace_name)) {}

NvsLoadResult NvsConfigStore::load() const {
    nvs_handle_t handle = 0;
    const esp_err_t open_result = nvs_open(namespace_name_.c_str(), NVS_READONLY, &handle);
    if (open_result == ESP_ERR_NVS_NOT_FOUND) return {};
    if (open_result != ESP_OK) return {NvsStoreError::StorageError, std::nullopt};
    const SlotResult selected = choose_slot(read_slot(handle, 0), read_slot(handle, 1));
    nvs_close(handle);
    return {selected.error, selected.value};
}

NvsStoreError NvsConfigStore::save(const SystemConfig &config, uint32_t generation) {
    std::vector<uint8_t> payload;
    const NvsStoreError encode_error = encode_config(config, payload);
    if (encode_error != NvsStoreError::None) return encode_error;

    nvs_handle_t handle = 0;
    if (nvs_open(namespace_name_.c_str(), NVS_READWRITE, &handle) != ESP_OK) {
        return NvsStoreError::StorageError;
    }
    const SlotResult left = read_slot(handle, 0);
    const SlotResult right = read_slot(handle, 1);
    if (left.error == NvsStoreError::StorageError || right.error == NvsStoreError::StorageError) {
        nvs_close(handle);
        return NvsStoreError::StorageError;
    }
    if (left.error == NvsStoreError::IncompatibleVersion ||
        right.error == NvsStoreError::IncompatibleVersion) {
        nvs_close(handle);
        return NvsStoreError::IncompatibleVersion;
    }
    const SlotResult current = choose_slot(left, right);
    if (left.value.has_value() && right.value.has_value() && !current.value.has_value()) {
        nvs_close(handle);
        return NvsStoreError::InvalidGeneration;
    }
    if (current.value.has_value() && !newer(generation, current.value->generation)) {
        nvs_close(handle);
        return NvsStoreError::InvalidGeneration;
    }
    const size_t target = current.value.has_value() ? current.slot ^ 1U : 0;

    Writer header_without_crc;
    header_without_crc.scalar(kStoredConfigMagic);
    header_without_crc.scalar(kStoredConfigSchemaVersion);
    header_without_crc.scalar<uint16_t>(static_cast<uint16_t>(payload.size()));
    header_without_crc.scalar(generation);
    const uint32_t crc = calculate_crc(header_without_crc.data().data(), payload.data(),
                                       payload.size());
    Writer blob;
    append_header(blob, static_cast<uint16_t>(payload.size()), generation, crc);
    blob.bytes(payload.data(), payload.size());

    esp_err_t result = nvs_set_blob(handle, kSlots[target], blob.data().data(), blob.data().size());
    if (result == ESP_OK) result = nvs_commit(handle);
    if (result == ESP_OK) {
        const SlotResult verified = read_slot(handle, target);
        if (!verified.value.has_value() || verified.value->generation != generation) {
            result = ESP_FAIL;
        }
    }
    nvs_close(handle);
    return result == ESP_OK ? NvsStoreError::None : NvsStoreError::StorageError;
}

}  // namespace rid

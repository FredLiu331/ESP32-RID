#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace rid {

enum class Protocol : uint8_t { Gb46750, OpenDroneId };
enum class Transport : uint8_t { Ble4, Ble5, Wifi24, Wifi58 };
enum class WifiMode : uint8_t { Beacon, Nan };
enum class MessageKind : uint8_t {
    BasicId,
    Location,
    Authentication,
    SelfId,
    System,
    OperatorId,
};

struct DeviceId {
    std::array<uint8_t, 6> bytes;
};

struct TestIdentity {
    std::string value;
};

struct ByteView {
    const uint8_t *data;
    size_t size;
};

struct GeoPoint {
    double latitude_deg;
    double longitude_deg;
};

struct FlightState {
    double latitude_deg;
    double longitude_deg;
    float altitude_msl_m;
    float height_agl_m;
    float horizontal_speed_mps;
    float vertical_speed_mps;
    float heading_deg;
    bool airborne;
    uint32_t relative_time_ms;
};

bool valid(const FlightState &state);
bool valid_period_ms(int64_t period_ms);

}  // namespace rid

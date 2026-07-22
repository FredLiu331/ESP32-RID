#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rid/model.hpp"

namespace rid {

struct GbOptions {
    std::string registration_mark{"TEST0000"};
    uint8_t operation_category{0};
    uint8_t ua_classification{0};
    uint8_t gcs_position_type{0};
    std::optional<GeoPoint> gcs_position;
    std::optional<float> gcs_altitude_m;
    std::optional<float> barometric_altitude_m;
    uint8_t horizontal_accuracy{0};
    uint8_t vertical_accuracy{0};
    uint8_t speed_accuracy{0};
    uint64_t timestamp_ms{0};
    uint8_t timestamp_accuracy{0};
};

enum class GbEncodeError : uint8_t {
    None,
    InvalidIdentity,
    InvalidFlightState,
    InvalidOptions,
    UnsupportedMessage,
};

class GbEncodeResult {
public:
    explicit GbEncodeResult(GbEncodeError error) : error_(error) {}
    explicit GbEncodeResult(std::vector<uint8_t> bytes)
        : error_(GbEncodeError::None), bytes_(std::move(bytes)) {}

    bool ok() const { return error_ == GbEncodeError::None; }
    GbEncodeError error() const { return error_; }
    const std::vector<uint8_t> &value() const { return bytes_; }

private:
    GbEncodeError error_;
    std::vector<uint8_t> bytes_;
};

class GbEncoder {
public:
    GbEncodeResult encode(MessageKind kind, const FlightState &state,
                          const TestIdentity &identity, const GbOptions &options) const;
};

}  // namespace rid

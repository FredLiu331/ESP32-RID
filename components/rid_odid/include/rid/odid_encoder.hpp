#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rid/model.hpp"

namespace rid {

enum class OdidIdType : uint8_t {
    SerialNumber = 1,
    CaaRegistration = 2,
};

struct OdidOptions {
    OdidIdType id_type{OdidIdType::SerialNumber};
    std::optional<float> barometric_altitude_m;
    float horizontal_accuracy_m{3.0F};
    float vertical_accuracy_m{1.0F};
    float barometric_accuracy_m{3.0F};
    float speed_accuracy_mps{1.0F};
    float timestamp_accuracy_s{0.2F};

    uint8_t authentication_type{0};
    uint32_t authentication_timestamp{0};
    std::vector<uint8_t> authentication_data;
    std::string self_description{"RID test flight"};

    std::optional<GeoPoint> operator_position;
    uint16_t area_count{1};
    uint16_t area_radius_m{0};
    float area_ceiling_m{-1000.0F};
    float area_floor_m{-1000.0F};
    bool eu_classification{false};
    uint8_t eu_category{0};
    uint8_t eu_class{0};
    float operator_altitude_m{-1000.0F};
    uint32_t system_timestamp{0};
    std::string operator_id{"TESTOPERATOR"};
};

enum class EncodeError : uint8_t {
    None,
    InvalidIdentity,
    InvalidFlightState,
    InvalidOptions,
    AuthenticationTooLong,
    TextTooLong,
    UnsupportedMessage,
    CoreEncodingFailed,
};

class EncodeResult {
public:
    explicit EncodeResult(EncodeError error) : error_(error) {}
    explicit EncodeResult(std::vector<uint8_t> bytes)
        : error_(EncodeError::None), bytes_(std::move(bytes)) {}

    bool ok() const { return error_ == EncodeError::None; }
    EncodeError error() const { return error_; }
    const std::vector<uint8_t> &value() const { return bytes_; }

private:
    EncodeError error_;
    std::vector<uint8_t> bytes_;
};

class OdidEncoder {
public:
    EncodeResult encode(MessageKind kind, const FlightState &state,
                        const TestIdentity &identity, const OdidOptions &options) const;
    EncodeResult encode_pack(const FlightState &state, const TestIdentity &identity,
                             const OdidOptions &options) const;
};

}  // namespace rid

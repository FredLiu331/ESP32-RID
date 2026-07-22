#include "rid/gb_encoder.hpp"

#include <cmath>
#include <initializer_list>

#include "gb_wire.hpp"

namespace rid {
namespace {

constexpr size_t kProductIdSize = 20;
constexpr size_t kRegistrationMarkSize = 8;
constexpr uint64_t kMaxTimestamp = 0xffffffffffffULL;
constexpr float kMinAltitudeM = -1000.0F;
constexpr float kMaxAltitudeM = 31767.5F;
constexpr float kMinRelativeAltitudeM = -9000.0F;
constexpr float kMaxRelativeAltitudeM = 23767.5F;
constexpr float kMaxGroundSpeedMps = 6553.4F;

bool valid_point(const GeoPoint &point) {
    return std::isfinite(point.latitude_deg) && point.latitude_deg >= -90.0 &&
           point.latitude_deg <= 90.0 && std::isfinite(point.longitude_deg) &&
           point.longitude_deg >= -180.0 && point.longitude_deg <= 180.0;
}

bool valid_altitude(float altitude_m) {
    return std::isfinite(altitude_m) && altitude_m > kMinAltitudeM &&
           altitude_m <= kMaxAltitudeM;
}

GbEncodeError validate_inputs(const FlightState &state, const TestIdentity &identity,
                              const GbOptions &options) {
    if (identity.value.size() != kProductIdSize) return GbEncodeError::InvalidIdentity;
    if (!valid(state)) return GbEncodeError::InvalidFlightState;
    if (options.registration_mark.size() > kRegistrationMarkSize ||
        (options.gcs_position && !valid_point(*options.gcs_position)) ||
        (options.gcs_altitude_m && !valid_altitude(*options.gcs_altitude_m)) ||
        (options.barometric_altitude_m &&
         !valid_altitude(*options.barometric_altitude_m)) ||
        options.timestamp_ms > kMaxTimestamp) {
        return GbEncodeError::InvalidOptions;
    }
    return GbEncodeError::None;
}

std::vector<uint8_t> payload(std::initializer_list<uint8_t> identifiers,
                             std::vector<uint8_t> content) {
    std::vector<uint8_t> out;
    out.reserve(3 + identifiers.size() + content.size());
    out.push_back(0xff);
    out.push_back(0x20);
    out.push_back(static_cast<uint8_t>(content.size()));
    out.insert(out.end(), identifiers.begin(), identifiers.end());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

void put_fixed_text(std::vector<uint8_t> &out, const std::string &text, size_t size) {
    out.insert(out.end(), text.begin(), text.end());
    out.insert(out.end(), size - text.size(), 0);
}

void put_coordinate(std::vector<uint8_t> &out, const GeoPoint &point) {
    gb_wire::put_i32_le(out,
                        static_cast<int32_t>(std::llround(point.longitude_deg * 1e7)));
    gb_wire::put_i32_le(out,
                        static_cast<int32_t>(std::llround(point.latitude_deg * 1e7)));
}

uint16_t encode_altitude(float altitude_m) {
    return static_cast<uint16_t>(std::lround((altitude_m + 1000.0F) * 2.0F));
}

uint16_t encode_relative_altitude(float altitude_m) {
    return static_cast<uint16_t>(std::lround((altitude_m + 9000.0F) * 2.0F));
}

GbEncodeResult encode_basic_id(const TestIdentity &identity) {
    std::vector<uint8_t> content;
    content.reserve(kProductIdSize);
    put_fixed_text(content, identity.value, kProductIdSize);
    return GbEncodeResult{payload({0x80}, std::move(content))};
}

GbEncodeResult encode_operator_id(const GbOptions &options) {
    std::vector<uint8_t> content;
    content.reserve(kRegistrationMarkSize);
    put_fixed_text(content, options.registration_mark, kRegistrationMarkSize);
    return GbEncodeResult{payload({0x40}, std::move(content))};
}

GbEncodeResult encode_system(const FlightState &state, const GbOptions &options) {
    const GeoPoint position =
        options.gcs_position.value_or(GeoPoint{state.latitude_deg, state.longitude_deg});
    const float altitude = options.gcs_altitude_m.value_or(state.altitude_msl_m);
    if (!valid_altitude(altitude)) return GbEncodeResult{GbEncodeError::InvalidOptions};

    std::vector<uint8_t> content;
    content.reserve(13);
    content.push_back(options.operation_category);
    content.push_back(options.ua_classification);
    content.push_back(options.gcs_position_type);
    put_coordinate(content, position);
    gb_wire::put_u16_le(content, encode_altitude(altitude));
    return GbEncodeResult{payload({0x3e}, std::move(content))};
}

GbEncodeResult encode_location(const FlightState &state, const GbOptions &options) {
    const float barometric_altitude =
        options.barometric_altitude_m.value_or(state.altitude_msl_m);
    if (!valid_altitude(state.altitude_msl_m) ||
        state.height_agl_m <= kMinRelativeAltitudeM ||
        state.height_agl_m > kMaxRelativeAltitudeM ||
        state.horizontal_speed_mps > kMaxGroundSpeedMps ||
        state.vertical_speed_mps < -63.0F || state.vertical_speed_mps > 63.5F ||
        !valid_altitude(barometric_altitude)) {
        return GbEncodeResult{GbEncodeError::InvalidFlightState};
    }

    std::vector<uint8_t> content;
    content.reserve(31);
    put_coordinate(content, GeoPoint{state.latitude_deg, state.longitude_deg});
    gb_wire::put_u16_le(content, static_cast<uint16_t>(std::lround(state.heading_deg * 10.0F)));
    gb_wire::put_u16_le(
        content, static_cast<uint16_t>(std::lround(state.horizontal_speed_mps * 10.0F)));
    gb_wire::put_u16_le(content, encode_relative_altitude(state.height_agl_m));
    const uint8_t vertical_magnitude =
        static_cast<uint8_t>(std::lround(std::fabs(state.vertical_speed_mps) * 2.0F));
    content.push_back(state.vertical_speed_mps < 0.0F
                          ? static_cast<uint8_t>(vertical_magnitude | 0x80)
                          : vertical_magnitude);
    gb_wire::put_u16_le(content, encode_altitude(state.altitude_msl_m));
    gb_wire::put_u16_le(content, encode_altitude(barometric_altitude));
    content.push_back(state.airborne ? 1 : 0);
    content.push_back(0);
    content.push_back(options.horizontal_accuracy);
    content.push_back(options.vertical_accuracy);
    content.push_back(options.speed_accuracy);
    gb_wire::put_u48_le(content, options.timestamp_ms);
    content.push_back(options.timestamp_accuracy);
    return GbEncodeResult{payload({0x01, 0xff, 0xfe}, std::move(content))};
}

}  // namespace

GbEncodeResult GbEncoder::encode(MessageKind kind, const FlightState &state,
                                 const TestIdentity &identity,
                                 const GbOptions &options) const {
    const GbEncodeError validation = validate_inputs(state, identity, options);
    if (validation != GbEncodeError::None) return GbEncodeResult{validation};

    switch (kind) {
        case MessageKind::BasicId:
            return encode_basic_id(identity);
        case MessageKind::Location:
            return encode_location(state, options);
        case MessageKind::System:
            return encode_system(state, options);
        case MessageKind::OperatorId:
            return encode_operator_id(options);
        case MessageKind::Authentication:
        case MessageKind::SelfId:
            return GbEncodeResult{GbEncodeError::UnsupportedMessage};
    }
    return GbEncodeResult{GbEncodeError::UnsupportedMessage};
}

}  // namespace rid

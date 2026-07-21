#include "rid/odid_encoder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

extern "C" {
#include "opendroneid.h"
}

namespace rid {
namespace {

constexpr size_t kMessageSize = ODID_MESSAGE_SIZE;
constexpr size_t kMaxAuthenticationBytes = ODID_AUTH_PAGE_ZERO_DATA_SIZE;

template <size_t N>
void copy_text(char (&destination)[N], const std::string &source) {
    const size_t count = std::min(source.size(), N - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

template <typename Encoded>
EncodeResult encoded_result(const Encoded &encoded, int status) {
    if (status != ODID_SUCCESS) return EncodeResult{EncodeError::CoreEncodingFailed};
    const auto *begin = reinterpret_cast<const uint8_t *>(&encoded);
    return EncodeResult{std::vector<uint8_t>(begin, begin + kMessageSize)};
}

EncodeError validate_inputs(const FlightState &state, const TestIdentity &identity,
                            const OdidOptions &options) {
    if (identity.value.size() != ODID_ID_SIZE) return EncodeError::InvalidIdentity;
    if (!valid(state)) return EncodeError::InvalidFlightState;
    const bool valid_operator_position =
        !options.operator_position ||
        (std::isfinite(options.operator_position->latitude_deg) &&
         options.operator_position->latitude_deg >= -90.0 &&
         options.operator_position->latitude_deg <= 90.0 &&
         std::isfinite(options.operator_position->longitude_deg) &&
         options.operator_position->longitude_deg >= -180.0 &&
         options.operator_position->longitude_deg <= 180.0);
    if ((options.barometric_altitude_m &&
         !std::isfinite(*options.barometric_altitude_m)) ||
        !std::isfinite(options.horizontal_accuracy_m) ||
        !std::isfinite(options.vertical_accuracy_m) ||
        !std::isfinite(options.barometric_accuracy_m) ||
        !std::isfinite(options.speed_accuracy_mps) ||
        !std::isfinite(options.timestamp_accuracy_s) ||
        !std::isfinite(options.area_ceiling_m) || !std::isfinite(options.area_floor_m) ||
        !std::isfinite(options.operator_altitude_m) || !valid_operator_position) {
        return EncodeError::InvalidOptions;
    }
    if (options.authentication_data.size() > kMaxAuthenticationBytes) {
        return EncodeError::AuthenticationTooLong;
    }
    if (options.self_description.size() > ODID_STR_SIZE ||
        options.operator_id.size() > ODID_ID_SIZE) {
        return EncodeError::TextTooLong;
    }
    return EncodeError::None;
}

EncodeResult encode_basic_id(const TestIdentity &identity, const OdidOptions &options) {
    ODID_BasicID_data data;
    ODID_BasicID_encoded encoded{};
    odid_initBasicIDData(&data);
    data.IDType = static_cast<ODID_idtype_t>(options.id_type);
    data.UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
    copy_text(data.UASID, identity.value);
    return encoded_result(encoded, encodeBasicIDMessage(&encoded, &data));
}

EncodeResult encode_location(const FlightState &state, const OdidOptions &options) {
    ODID_Location_data data;
    ODID_Location_encoded encoded{};
    odid_initLocationData(&data);
    data.Status = state.airborne ? ODID_STATUS_AIRBORNE : ODID_STATUS_GROUND;
    data.Direction = state.heading_deg;
    data.SpeedHorizontal = state.horizontal_speed_mps;
    data.SpeedVertical = state.vertical_speed_mps;
    data.Latitude = state.latitude_deg;
    data.Longitude = state.longitude_deg;
    data.AltitudeBaro = options.barometric_altitude_m.value_or(state.altitude_msl_m);
    data.AltitudeGeo = state.altitude_msl_m;
    data.HeightType = ODID_HEIGHT_REF_OVER_GROUND;
    data.Height = state.height_agl_m;
    data.HorizAccuracy = createEnumHorizontalAccuracy(options.horizontal_accuracy_m);
    data.VertAccuracy = createEnumVerticalAccuracy(options.vertical_accuracy_m);
    data.BaroAccuracy = createEnumVerticalAccuracy(options.barometric_accuracy_m);
    data.SpeedAccuracy = createEnumSpeedAccuracy(options.speed_accuracy_mps);
    data.TSAccuracy = createEnumTimestampAccuracy(options.timestamp_accuracy_s);
    data.TimeStamp = static_cast<float>(state.relative_time_ms % 3600000U) / 1000.0F;
    return encoded_result(encoded, encodeLocationMessage(&encoded, &data));
}

EncodeResult encode_authentication(const OdidOptions &options) {
    ODID_Auth_data data;
    ODID_Auth_encoded encoded{};
    odid_initAuthData(&data);
    data.AuthType = static_cast<ODID_authtype_t>(options.authentication_type);
    data.DataPage = 0;
    data.LastPageIndex = 0;
    data.Length = static_cast<uint8_t>(options.authentication_data.size());
    data.Timestamp = options.authentication_timestamp;
    std::copy(options.authentication_data.begin(), options.authentication_data.end(),
              data.AuthData);
    return encoded_result(encoded, encodeAuthMessage(&encoded, &data));
}

EncodeResult encode_self_id(const OdidOptions &options) {
    ODID_SelfID_data data;
    ODID_SelfID_encoded encoded{};
    odid_initSelfIDData(&data);
    data.DescType = ODID_DESC_TYPE_TEXT;
    copy_text(data.Desc, options.self_description);
    return encoded_result(encoded, encodeSelfIDMessage(&encoded, &data));
}

EncodeResult encode_system(const FlightState &state, const OdidOptions &options) {
    ODID_System_data data;
    ODID_System_encoded encoded{};
    odid_initSystemData(&data);
    const GeoPoint operator_position =
        options.operator_position.value_or(GeoPoint{state.latitude_deg, state.longitude_deg});
    data.OperatorLocationType = ODID_OPERATOR_LOCATION_TYPE_TAKEOFF;
    data.ClassificationType = options.eu_classification ? ODID_CLASSIFICATION_TYPE_EU
                                                       : ODID_CLASSIFICATION_TYPE_UNDECLARED;
    data.OperatorLatitude = operator_position.latitude_deg;
    data.OperatorLongitude = operator_position.longitude_deg;
    data.AreaCount = options.area_count;
    data.AreaRadius = options.area_radius_m;
    data.AreaCeiling = options.area_ceiling_m;
    data.AreaFloor = options.area_floor_m;
    data.CategoryEU = static_cast<ODID_category_EU_t>(options.eu_category);
    data.ClassEU = static_cast<ODID_class_EU_t>(options.eu_class);
    data.OperatorAltitudeGeo = options.operator_altitude_m;
    data.Timestamp = options.system_timestamp;
    return encoded_result(encoded, encodeSystemMessage(&encoded, &data));
}

EncodeResult encode_operator_id(const OdidOptions &options) {
    ODID_OperatorID_data data;
    ODID_OperatorID_encoded encoded{};
    odid_initOperatorIDData(&data);
    data.OperatorIdType = ODID_OPERATOR_ID;
    copy_text(data.OperatorId, options.operator_id);
    return encoded_result(encoded, encodeOperatorIDMessage(&encoded, &data));
}

}  // namespace

EncodeResult OdidEncoder::encode(MessageKind kind, const FlightState &state,
                                 const TestIdentity &identity,
                                 const OdidOptions &options) const {
    const EncodeError validation = validate_inputs(state, identity, options);
    if (validation != EncodeError::None) return EncodeResult{validation};

    switch (kind) {
        case MessageKind::BasicId:
            return encode_basic_id(identity, options);
        case MessageKind::Location:
            return encode_location(state, options);
        case MessageKind::Authentication:
            return encode_authentication(options);
        case MessageKind::SelfId:
            return encode_self_id(options);
        case MessageKind::System:
            return encode_system(state, options);
        case MessageKind::OperatorId:
            return encode_operator_id(options);
    }
    return EncodeResult{EncodeError::UnsupportedMessage};
}

EncodeResult OdidEncoder::encode_pack(const FlightState &state, const TestIdentity &identity,
                                      const OdidOptions &options) const {
    ODID_MessagePack_data data;
    ODID_MessagePack_encoded encoded{};
    odid_initMessagePackData(&data);

    constexpr std::array<MessageKind, 6> kinds{
        MessageKind::BasicId,       MessageKind::Location, MessageKind::Authentication,
        MessageKind::SelfId,        MessageKind::System,   MessageKind::OperatorId,
    };
    for (const auto kind : kinds) {
        const auto message = encode(kind, state, identity, options);
        if (!message.ok()) return EncodeResult{message.error()};
        std::memcpy(data.Messages[data.MsgPackSize].rawData, message.value().data(), kMessageSize);
        ++data.MsgPackSize;
    }
    if (encodeMessagePack(&encoded, &data) != ODID_SUCCESS) {
        return EncodeResult{EncodeError::CoreEncodingFailed};
    }
    const auto *begin = reinterpret_cast<const uint8_t *>(&encoded);
    const size_t packed_size = 3 + data.MsgPackSize * kMessageSize;
    return EncodeResult{std::vector<uint8_t>(begin, begin + packed_size)};
}

}  // namespace rid

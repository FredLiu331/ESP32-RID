#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "rid/odid_encoder.hpp"
#include "unity.h"

namespace {

constexpr std::array<uint8_t, 25> kBasicId{
    0x02, 0x22, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31,
    0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x00, 0x00, 0x00,
};
constexpr std::array<uint8_t, 25> kLocation{
    0x12, 0x26, 0x24, 0x16, 0x0b, 0x42, 0xbf, 0x24, 0x1b, 0x6e, 0xd1, 0xb4, 0xb6,
    0x98, 0x08, 0xac, 0x08, 0x70, 0x08, 0x6b, 0x53, 0x15, 0x0e, 0x02, 0x00,
};
constexpr std::array<uint8_t, 25> kAuth{
    0x22, 0x10, 0x00, 0x11, 0x00, 0x3f, 0xab, 0x01, 0x31, 0x32, 0x33, 0x34, 0x35,
    0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
};
constexpr std::array<uint8_t, 25> kSelfId{
    0x32, 0x00, 0x44, 0x72, 0x6f, 0x6e, 0x65, 0x73, 0x52, 0x55, 0x53, 0x3a, 0x20,
    0x52, 0x65, 0x61, 0x6c, 0x20, 0x45, 0x73, 0x74, 0x61, 0x74, 0x65, 0x00,
};
constexpr std::array<uint8_t, 25> kSystem{
    0x42, 0x04, 0xa6, 0xbf, 0x24, 0x1b, 0xd2, 0xd1, 0xb4, 0xb6, 0x23, 0x00, 0x07,
    0x32, 0x09, 0x23, 0x08, 0x24, 0xf9, 0x07, 0x00, 0x3f, 0xab, 0x01, 0x00,
};
constexpr std::array<uint8_t, 25> kOperatorId{
    0x52, 0x00, 0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x00, 0x00, 0x00,
};

rid::FlightState official_state() {
    return rid::FlightState{
        45.539309, -122.966389, 110.0F, 80.0F, 5.4F, 5.25F, 215.7F, true, 360520,
    };
}

rid::OdidOptions official_options() {
    rid::OdidOptions options;
    options.id_type = rid::OdidIdType::CaaRegistration;
    options.barometric_altitude_m = 100.0F;
    options.horizontal_accuracy_m = 2.5F;
    options.vertical_accuracy_m = 0.5F;
    options.barometric_accuracy_m = 1.5F;
    options.speed_accuracy_mps = 0.5F;
    options.timestamp_accuracy_s = 0.2F;
    options.authentication_type = 1;
    options.authentication_timestamp = 28000000;
    options.authentication_data = {'1', '2', '3', '4', '5', '6', '7', '8', '9',
                                   '0', '1', '2', '3', '4', '5', '6', '7'};
    options.self_description = "DronesRUS: Real Estate";
    options.operator_position = rid::GeoPoint{45.539319, -122.966379};
    options.area_count = 35;
    options.area_radius_m = 75;
    options.area_ceiling_m = 176.9F;
    options.area_floor_m = 41.7F;
    options.eu_classification = true;
    options.eu_category = 2;
    options.eu_class = 4;
    options.operator_altitude_m = 20.5F;
    options.system_timestamp = 28000000;
    options.operator_id = "98765432100123456789";
    return options;
}

void assert_message(const rid::EncodeResult &result, const std::array<uint8_t, 25> &expected) {
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(expected.size(), result.value().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), result.value().data(), expected.size());
}

}  // namespace

TEST_CASE("upstream test_inout ASTM F3411-22a message vectors", "[odid]") {
    const rid::OdidEncoder encoder;
    const auto state = official_state();
    const rid::TestIdentity identity{"12345678901234567890"};
    const auto options = official_options();

    assert_message(encoder.encode(rid::MessageKind::BasicId, state, identity, options), kBasicId);
    assert_message(encoder.encode(rid::MessageKind::Location, state, identity, options), kLocation);
    assert_message(encoder.encode(rid::MessageKind::Authentication, state, identity, options), kAuth);
    assert_message(encoder.encode(rid::MessageKind::SelfId, state, identity, options), kSelfId);
    assert_message(encoder.encode(rid::MessageKind::System, state, identity, options), kSystem);
    assert_message(encoder.encode(rid::MessageKind::OperatorId, state, identity, options),
                   kOperatorId);
}

TEST_CASE("upstream test_inout ASTM F3411-22a six message pack vector", "[odid]") {
    const rid::OdidEncoder encoder;
    const auto packed = encoder.encode_pack(official_state(), {"12345678901234567890"},
                                            official_options());
    TEST_ASSERT_TRUE(packed.ok());

    std::vector<uint8_t> expected{0xf2, 0x19, 0x06};
    for (const auto *message : {&kBasicId, &kLocation, &kAuth, &kSelfId, &kSystem, &kOperatorId}) {
        expected.insert(expected.end(), message->begin(), message->end());
    }
    TEST_ASSERT_EQUAL_UINT32(expected.size(), packed.value().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), packed.value().data(), expected.size());
}

TEST_CASE("OpenDroneID adapter rejects invalid project inputs", "[odid]") {
    rid::OdidEncoder encoder;
    auto state = official_state();
    auto options = official_options();
    TEST_ASSERT_EQUAL(rid::EncodeError::InvalidIdentity,
                      encoder.encode(rid::MessageKind::BasicId, state, {"too-short"}, options)
                          .error());
    state.latitude_deg = 91.0;
    TEST_ASSERT_EQUAL(rid::EncodeError::InvalidFlightState,
                      encoder.encode(rid::MessageKind::Location, state,
                                     {"12345678901234567890"}, options)
                          .error());
    options.authentication_data.push_back(0xff);
    TEST_ASSERT_EQUAL(rid::EncodeError::AuthenticationTooLong,
                      encoder.encode(rid::MessageKind::Authentication, official_state(),
                                     {"12345678901234567890"}, options)
                          .error());

    options = official_options();
    options.barometric_altitude_m = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_EQUAL(rid::EncodeError::InvalidOptions,
                      encoder.encode(rid::MessageKind::Location, official_state(),
                                     {"12345678901234567890"}, options)
                          .error());

    options = official_options();
    options.operator_position =
        rid::GeoPoint{std::numeric_limits<double>::quiet_NaN(), 0.0};
    TEST_ASSERT_EQUAL(rid::EncodeError::InvalidOptions,
                      encoder.encode(rid::MessageKind::System, official_state(),
                                     {"12345678901234567890"}, options)
                          .error());
}

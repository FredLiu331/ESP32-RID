#include <array>
#include <cstdint>
#include <limits>

#include "rid/gb_encoder.hpp"
#include "unity.h"

namespace {

constexpr const char *kIdentity = "TEST307A069CCC8AFFEF";

rid::FlightState test_state() {
    return rid::FlightState{
        31.2304, 121.4737, 123.5F, 50.0F, 12.3F, -2.5F, 87.6F, true, 1000,
    };
}

rid::GbOptions test_options() {
    rid::GbOptions options;
    options.registration_mark = "TEST0001";
    options.operation_category = 2;
    options.ua_classification = 3;
    options.gcs_position_type = 1;
    options.gcs_position = rid::GeoPoint{31.2, 121.4};
    options.gcs_altitude_m = 100.0F;
    options.barometric_altitude_m = 120.0F;
    options.horizontal_accuracy = 3;
    options.vertical_accuracy = 2;
    options.speed_accuracy = 1;
    options.timestamp_ms = 0x010203040506ULL;
    options.timestamp_accuracy = 4;
    return options;
}

template <size_t N>
void assert_bytes(const rid::GbEncodeResult &result,
                  const std::array<uint8_t, N> &expected) {
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT32(expected.size(), result.value().size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), result.value().data(), expected.size());
}

}  // namespace

TEST_CASE("GB 46750 identity and registration vectors", "[gb46750]") {
    const rid::GbEncoder encoder;
    const auto state = test_state();
    const auto options = test_options();
    constexpr std::array<uint8_t, 24> basic{
        0xff, 0x20, 0x14, 0x80, 'T',  'E',  'S',  'T',  '3',  '0',  '7',  'A',
        '0',  '6',  '9',  'C',  'C',  'C',  '8',  'A',  'F',  'F',  'E',  'F',
    };
    constexpr std::array<uint8_t, 12> registration{
        0xff, 0x20, 0x08, 0x40, 'T', 'E', 'S', 'T', '0', '0', '0', '1',
    };

    assert_bytes(encoder.encode(rid::MessageKind::BasicId, state, {kIdentity}, options), basic);
    assert_bytes(encoder.encode(rid::MessageKind::OperatorId, state, {kIdentity}, options),
                 registration);

    auto empty_registration = options;
    empty_registration.registration_mark.clear();
    constexpr std::array<uint8_t, 12> padded{
        0xff, 0x20, 0x08, 0x40, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    assert_bytes(encoder.encode(rid::MessageKind::OperatorId, state, {kIdentity},
                                empty_registration),
                 padded);
}

TEST_CASE("GB 46750 system normal and boundary vectors", "[gb46750]") {
    const rid::GbEncoder encoder;
    auto options = test_options();
    constexpr std::array<uint8_t, 17> normal{
        0xff, 0x20, 0x0d, 0x3e, 0x02, 0x03, 0x01, 0x80, 0x2b,
        0x5c, 0x48, 0x00, 0xbe, 0x98, 0x12, 0x98, 0x08,
    };
    assert_bytes(encoder.encode(rid::MessageKind::System, test_state(), {kIdentity}, options),
                 normal);

    options.operation_category = 0;
    options.ua_classification = 0;
    options.gcs_position_type = 0;
    options.gcs_position = rid::GeoPoint{-90.0, -180.0};
    options.gcs_altitude_m = -999.5F;
    constexpr std::array<uint8_t, 17> boundary{
        0xff, 0x20, 0x0d, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x2e,
        0xb6, 0x94, 0x00, 0x17, 0x5b, 0xca, 0x01, 0x00,
    };
    assert_bytes(encoder.encode(rid::MessageKind::System, test_state(), {kIdentity}, options),
                 boundary);
}

TEST_CASE("GB 46750 location normal vector", "[gb46750]") {
    constexpr std::array<uint8_t, 37> expected{
        0xff, 0x20, 0x1f, 0x01, 0xff, 0xfe, 0x68, 0x6a, 0x67, 0x48,
        0x80, 0x61, 0x9d, 0x12, 0x6c, 0x03, 0x7b, 0x00, 0xb4, 0x46,
        0x85, 0xc7, 0x08, 0xc0, 0x08, 0x01, 0x00, 0x03, 0x02, 0x01,
        0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x04,
    };
    assert_bytes(rid::GbEncoder{}.encode(rid::MessageKind::Location, test_state(), {kIdentity},
                                         test_options()),
                 expected);
}

TEST_CASE("GB 46750 location boundary vector and invalid inputs", "[gb46750]") {
    rid::GbEncoder encoder;
    auto state = test_state();
    auto options = test_options();
    state.latitude_deg = -90.0;
    state.longitude_deg = -180.0;
    state.altitude_msl_m = 31767.5F;
    state.height_agl_m = -8999.5F;
    state.horizontal_speed_mps = 6553.4F;
    state.vertical_speed_mps = 63.5F;
    state.heading_deg = 359.9F;
    state.airborne = false;
    options.barometric_altitude_m = -999.5F;
    options.horizontal_accuracy = 0;
    options.vertical_accuracy = 0;
    options.speed_accuracy = 0;
    options.timestamp_ms = 0xffffffffffffULL;
    options.timestamp_accuracy = 0;
    constexpr std::array<uint8_t, 37> boundary{
        0xff, 0x20, 0x1f, 0x01, 0xff, 0xfe, 0x00, 0x2e, 0xb6, 0x94,
        0x00, 0x17, 0x5b, 0xca, 0x0f, 0x0e, 0xfe, 0xff, 0x01, 0x00,
        0x7f, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00,
    };
    assert_bytes(encoder.encode(rid::MessageKind::Location, state, {kIdentity}, options),
                 boundary);

    TEST_ASSERT_EQUAL(rid::GbEncodeError::InvalidIdentity,
                      encoder.encode(rid::MessageKind::BasicId, test_state(), {"short"}, options)
                          .error());
    TEST_ASSERT_EQUAL(rid::GbEncodeError::UnsupportedMessage,
                      encoder.encode(rid::MessageKind::Authentication, test_state(),
                                     {kIdentity}, options)
                          .error());
    options.timestamp_ms = 0x1000000000000ULL;
    TEST_ASSERT_EQUAL(rid::GbEncodeError::InvalidOptions,
                      encoder.encode(rid::MessageKind::Location, test_state(), {kIdentity}, options)
                          .error());
    options = test_options();
    options.barometric_altitude_m = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_EQUAL(rid::GbEncodeError::InvalidOptions,
                      encoder.encode(rid::MessageKind::Location, test_state(), {kIdentity}, options)
                          .error());
}

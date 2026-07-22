#include <limits>

#include "rid/identity.hpp"
#include "rid/model.hpp"
#include "unity.h"

namespace {

rid::FlightState valid_state() {
    return rid::FlightState{
        31.2304,
        121.4737,
        120.0F,
        30.0F,
        12.5F,
        -0.5F,
        270.0F,
        true,
        1000,
    };
}

}  // namespace

TEST_CASE("test IDs are stable unique and protocol scoped", "[identity]") {
    const rid::DeviceId device{{0x10, 0x20, 0x30, 0x40, 0x50, 0x60}};
    const auto a = rid::derive_test_id(device, "odid_ble", 0, rid::Protocol::OpenDroneId);
    const auto b = rid::derive_test_id(device, "odid_ble", 1, rid::Protocol::OpenDroneId);
    const auto gb = rid::derive_test_id(device, "odid_ble", 0, rid::Protocol::Gb46750);

    TEST_ASSERT_EQUAL_STRING("TEST307A069CCC8AFFEF", a.value.c_str());
    TEST_ASSERT_EQUAL_STRING(
        a.value.c_str(),
        rid::derive_test_id(device, "odid_ble", 0, rid::Protocol::OpenDroneId).value.c_str());
    TEST_ASSERT_NOT_EQUAL(0, a.value.compare(b.value));
    TEST_ASSERT_NOT_EQUAL(0, a.value.compare(gb.value));
    TEST_ASSERT_EQUAL_UINT32(20, a.value.size());
    TEST_ASSERT_TRUE(a.value.rfind("TEST", 0) == 0);
}

TEST_CASE("flight state rejects coordinate and numeric domain errors", "[model]") {
    auto state = valid_state();
    TEST_ASSERT_TRUE(rid::valid(state));

    state.latitude_deg = 90.000001;
    TEST_ASSERT_FALSE(rid::valid(state));
    state = valid_state();
    state.longitude_deg = -180.000001;
    TEST_ASSERT_FALSE(rid::valid(state));
    state = valid_state();
    state.horizontal_speed_mps = -0.01F;
    TEST_ASSERT_FALSE(rid::valid(state));
    state = valid_state();
    state.heading_deg = 360.0F;
    TEST_ASSERT_FALSE(rid::valid(state));
}

TEST_CASE("flight state rejects non finite values and negative periods", "[model]") {
    auto state = valid_state();
    state.latitude_deg = std::numeric_limits<double>::quiet_NaN();
    TEST_ASSERT_FALSE(rid::valid(state));
    state = valid_state();
    state.altitude_msl_m = std::numeric_limits<float>::infinity();
    TEST_ASSERT_FALSE(rid::valid(state));
    state = valid_state();
    state.vertical_speed_mps = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_FALSE(rid::valid(state));

    TEST_ASSERT_FALSE(rid::valid_period_ms(-1));
    TEST_ASSERT_FALSE(rid::valid_period_ms(0));
    TEST_ASSERT_TRUE(rid::valid_period_ms(1));
}

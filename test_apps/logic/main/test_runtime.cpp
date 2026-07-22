#include <vector>
#include <memory>

#include "rid/ble_transport.hpp"
#include "rid/runtime.hpp"
#include "unity.h"

namespace {
class CapturingSink final : public rid::RuntimeSink {
public:
    bool submit(const rid::ScheduledPayload &payload) override {
        payloads.push_back(payload);
        return accept;
    }
    bool accept{true};
    std::vector<rid::ScheduledPayload> payloads;
};

rid::SystemConfig config_for(rid::Transport transport, rid::Protocol protocol, uint16_t count) {
    rid::SystemConfig config;
    config.site = rid::GeoPoint{31.2304, 121.4737};
    config.default_period = std::chrono::milliseconds{1000};
    config.groups = {rid::GroupConfig{"fleet", count, protocol, transport}};
    config.groups[0].trajectory = rid::circle_trajectory(20.0F, 4.0F, 60.0F);
    return config;
}
}

TEST_CASE("runtime atomically applies and expands one to fifty aircraft", "[runtime]") {
    CapturingSink sink;
    auto runtime = std::make_unique<rid::Runtime>(rid::DeviceId{{1, 2, 3, 4, 5, 6}}, sink);
    TEST_ASSERT_EQUAL(rid::RuntimeError::None,
                      runtime->apply(config_for(rid::Transport::Wifi24,
                                               rid::Protocol::OpenDroneId, 50), 100));
    TEST_ASSERT_TRUE(runtime->running());
    TEST_ASSERT_EQUAL_UINT32(50, runtime->aircraft_count());

    auto invalid = config_for(rid::Transport::Wifi24, rid::Protocol::OpenDroneId, 51);
    TEST_ASSERT_EQUAL(rid::RuntimeError::InvalidConfig, runtime->apply(invalid, 200));
    TEST_ASSERT_EQUAL_UINT32(50, runtime->aircraft_count());
}

TEST_CASE("runtime emits complete OpenDroneID Beacon on its configured band", "[runtime]") {
    CapturingSink sink;
    auto runtime = std::make_unique<rid::Runtime>(rid::DeviceId{{1, 2, 3, 4, 5, 6}}, sink);
    TEST_ASSERT_EQUAL(rid::RuntimeError::None,
                      runtime->apply(config_for(rid::Transport::Wifi58,
                                               rid::Protocol::OpenDroneId, 1), 1000));
    TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(1000));
    TEST_ASSERT_EQUAL_UINT32(1, sink.payloads.size());
    TEST_ASSERT_EQUAL(rid::Transport::Wifi58, sink.payloads[0].transport);
    TEST_ASSERT_GREATER_THAN(158, sink.payloads[0].size);
    TEST_ASSERT_EQUAL_HEX8(0x80, sink.payloads[0].bytes[0]);
}

TEST_CASE("runtime staggers six single messages for BLE4", "[runtime]") {
    CapturingSink sink;
    auto runtime = std::make_unique<rid::Runtime>(rid::DeviceId{{1, 2, 3, 4, 5, 6}}, sink);
    TEST_ASSERT_EQUAL(rid::RuntimeError::None,
                      runtime->apply(config_for(rid::Transport::Ble4,
                                               rid::Protocol::OpenDroneId, 1), 0));
    TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(0));
    TEST_ASSERT_EQUAL_UINT32(1, sink.payloads.size());
    for (uint64_t now = 10; now < 1000; now += 10) {
        TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(now));
    }
    TEST_ASSERT_EQUAL_UINT32(6, sink.payloads.size());
    for (const auto &payload : sink.payloads) {
        TEST_ASSERT_EQUAL(rid::Transport::Ble4, payload.transport);
        TEST_ASSERT_EQUAL_UINT32(30, payload.size);
    }
    TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(1000));
    TEST_ASSERT_EQUAL_UINT32(7, sink.payloads.size());
}

TEST_CASE("runtime emits one complete OpenDroneID message pack for BLE5", "[runtime]") {
    CapturingSink sink;
    auto runtime = std::make_unique<rid::Runtime>(rid::DeviceId{{1, 2, 3, 4, 5, 6}}, sink);
    TEST_ASSERT_EQUAL(rid::RuntimeError::None,
                      runtime->apply(config_for(rid::Transport::Ble5,
                                               rid::Protocol::OpenDroneId, 1), 1000));

    TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(1000));
    TEST_ASSERT_EQUAL_UINT32(1, sink.payloads.size());
    TEST_ASSERT_EQUAL(rid::Transport::Ble5, sink.payloads[0].transport);
    TEST_ASSERT_EQUAL_UINT32(rid::kBleExtendedMaxPayloadSize, sink.payloads[0].size);
    TEST_ASSERT_EQUAL_HEX8(0x16, sink.payloads[0].bytes[1]);
    TEST_ASSERT_EQUAL_HEX8(0xf0, sink.payloads[0].bytes[5] & 0xf0U);
}

TEST_CASE("runtime staggers fifty BLE5 aircraft across one period", "[runtime]") {
    CapturingSink sink;
    auto runtime = std::make_unique<rid::Runtime>(rid::DeviceId{{1, 2, 3, 4, 5, 6}}, sink);
    TEST_ASSERT_EQUAL(rid::RuntimeError::None,
                      runtime->apply(config_for(rid::Transport::Ble5,
                                               rid::Protocol::OpenDroneId, 50), 0));

    TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(0));
    TEST_ASSERT_EQUAL_UINT32(1, sink.payloads.size());
    for (uint64_t now = 20; now < 1000; now += 20) {
        TEST_ASSERT_EQUAL(rid::RuntimeError::None, runtime->tick(now));
    }
    TEST_ASSERT_EQUAL_UINT32(50, sink.payloads.size());
}

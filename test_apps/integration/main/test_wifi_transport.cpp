#include <array>
#include <vector>

#include "nvs_flash.h"
#include "rid/radio_coordinator.hpp"
#include "rid/wifi_transport.hpp"
#include "unity.h"

namespace {

class FakeWifiBackend final : public rid::WifiBackend {
public:
    esp_err_t set_country_cn() override {
        ++country_calls;
        return country_result;
    }
    esp_err_t set_channel(uint8_t channel) override {
        channel_calls.push_back(channel);
        return channel_result;
    }
    esp_err_t transmit(rid::ByteView frame) override {
        transmitted.emplace_back(frame.data, frame.data + frame.size);
        return transmit_result;
    }

    esp_err_t country_result{ESP_OK};
    esp_err_t channel_result{ESP_OK};
    esp_err_t transmit_result{ESP_OK};
    size_t country_calls{0};
    std::vector<uint8_t> channel_calls;
    std::vector<std::vector<uint8_t>> transmitted;
};

rid::ScheduledPayload payload(rid::Transport transport, uint8_t marker, uint64_t deadline = 0) {
    rid::ScheduledPayload result;
    result.transport = transport;
    result.deadline_ms = deadline;
    result.size = 1;
    result.bytes[0] = marker;
    return result;
}

constexpr std::array<uint8_t, 6> kMac{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

}  // namespace

TEST_CASE("GB Beacon bytes contain complete management frame and Vendor IE", "[wifi]") {
    const uint8_t ssid[] = {'R', 'I', 'D'};
    const uint8_t gb[] = {0xff, 0x20, 0x01, 0x80};
    rid::WifiFrame frame;
    TEST_ASSERT_EQUAL(rid::WifiFrameError::None,
                      rid::build_gb_beacon(frame, kMac.data(), {ssid, sizeof(ssid)}, 0x37,
                                           {gb, sizeof(gb)}));
    const uint8_t expected[] = {
        0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 0x02, 0x11, 0x22, 0x33, 0x44, 0x55,
        0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x64, 0x00, 0x01, 0x04,
        0x00, 0x03, 'R', 'I', 'D', 0x01, 0x01, 0x8c,
        0xdd, 0x09, 0xfa, 0x0b, 0xbc, 0x0d, 0x37, 0xff, 0x20, 0x01, 0x80,
    };
    TEST_ASSERT_EQUAL_UINT32(sizeof(expected), frame.size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.bytes.data(), sizeof(expected));
}

TEST_CASE("GB Beacon rejects null and oversized inputs", "[wifi]") {
    rid::WifiFrame frame;
    std::array<uint8_t, 250> too_large{};
    TEST_ASSERT_EQUAL(rid::WifiFrameError::InvalidArgument,
                      rid::build_gb_beacon(frame, nullptr, {nullptr, 0}, 0, {nullptr, 0}));
    TEST_ASSERT_EQUAL(rid::WifiFrameError::InvalidArgument,
                      rid::build_gb_beacon(frame, kMac.data(), {nullptr, 1}, 0, {nullptr, 0}));
    TEST_ASSERT_EQUAL(rid::WifiFrameError::InvalidArgument,
                      rid::build_gb_beacon(frame, kMac.data(), {nullptr, 0}, 0, {nullptr, 0}));
    TEST_ASSERT_EQUAL(rid::WifiFrameError::PayloadTooLarge,
                      rid::build_gb_beacon(frame, kMac.data(), {nullptr, 0}, 0,
                                           {too_large.data(), too_large.size()}));
    TEST_ASSERT_EQUAL_UINT32(0, frame.size);
}

TEST_CASE("external OpenDroneID Beacon validator rejects malformed IE boundaries", "[wifi]") {
    rid::WifiFrame frame;
    const uint8_t payload[] = {1};
    TEST_ASSERT_EQUAL(rid::WifiFrameError::None,
                      rid::build_gb_beacon(frame, kMac.data(), {nullptr, 0}, 1,
                                           {payload, sizeof(payload)}));
    TEST_ASSERT_EQUAL(rid::WifiFrameError::None,
                      rid::validate_opendroneid_beacon(frame.view()));
    frame.bytes[36] = 0xd0;
    TEST_ASSERT_EQUAL(rid::WifiFrameError::InvalidFrame,
                      rid::validate_opendroneid_beacon(frame.view()));
    frame.bytes[36] = 0;
    TEST_ASSERT_EQUAL(rid::WifiFrameError::InvalidFrame,
                      rid::validate_opendroneid_beacon(
                          {frame.bytes.data(), static_cast<size_t>(frame.size - 1)}));
}

TEST_CASE("empty coordinator does not select a channel", "[wifi]") {
    FakeWifiBackend backend;
    rid::RadioCoordinator coordinator(backend, 100, 20);
    TEST_ASSERT_EQUAL(ESP_OK, coordinator.poll(0));
    TEST_ASSERT_EQUAL_UINT32(0, backend.country_calls);
    TEST_ASSERT_EQUAL_UINT32(0, backend.channel_calls.size());
}

TEST_CASE("single-band coordinator stays on channels 6 and 149", "[wifi]") {
    {
        FakeWifiBackend wifi24;
        rid::RadioCoordinator radio24(wifi24, 100, 20);
        radio24.submit(payload(rid::Transport::Wifi24, 1));
        radio24.submit(payload(rid::Transport::Wifi24, 2));
        radio24.poll(0);
        radio24.poll(100);
        TEST_ASSERT_EQUAL_UINT32(1, wifi24.channel_calls.size());
        TEST_ASSERT_EQUAL_UINT8(6, wifi24.channel_calls[0]);
    }
    {
        FakeWifiBackend wifi58;
        rid::RadioCoordinator radio58(wifi58, 100, 20);
        radio58.submit(payload(rid::Transport::Wifi58, 3));
        radio58.poll(0);
        TEST_ASSERT_EQUAL_UINT8(149, wifi58.channel_calls[0]);
    }
}

TEST_CASE("dual-band coordinator rotates after deterministic dwell", "[wifi]") {
    FakeWifiBackend backend;
    rid::RadioCoordinator coordinator(backend, 100, 20);
    coordinator.submit(payload(rid::Transport::Wifi24, 1));
    coordinator.submit(payload(rid::Transport::Wifi24, 2));
    coordinator.submit(payload(rid::Transport::Wifi58, 3));
    coordinator.submit(payload(rid::Transport::Wifi58, 4));
    coordinator.poll(0);
    coordinator.poll(99);
    coordinator.poll(100);
    coordinator.submit(payload(rid::Transport::Wifi24, 5));
    coordinator.poll(200);
    const uint8_t expected[] = {6, 149, 6};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, backend.channel_calls.data(), 3);
}

TEST_CASE("failed channel switch keeps current channel and retries on deadline", "[wifi]") {
    FakeWifiBackend backend;
    rid::RadioCoordinator coordinator(backend, 100, 20);
    coordinator.submit(payload(rid::Transport::Wifi24, 1));
    coordinator.submit(payload(rid::Transport::Wifi58, 2));
    coordinator.poll(0);
    backend.channel_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, coordinator.poll(100));
    TEST_ASSERT_EQUAL_UINT8(6, coordinator.channel());
    TEST_ASSERT_EQUAL_UINT32(1, coordinator.depth(rid::Transport::Wifi58));
    TEST_ASSERT_EQUAL(ESP_OK, coordinator.poll(119));
    TEST_ASSERT_EQUAL_UINT32(2, backend.channel_calls.size());
    backend.channel_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, coordinator.poll(120));
    TEST_ASSERT_EQUAL_UINT8(149, coordinator.channel());
}

TEST_CASE("failed transmit keeps earliest-deadline payload and records radio error", "[wifi]") {
    FakeWifiBackend backend;
    rid::RadioCoordinator coordinator(backend, 100, 20);
    coordinator.submit(payload(rid::Transport::Wifi24, 2, 20));
    coordinator.submit(payload(rid::Transport::Wifi24, 1, 10));
    backend.transmit_result = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, coordinator.poll(0));
    TEST_ASSERT_EQUAL_UINT32(2, coordinator.depth(rid::Transport::Wifi24));
    TEST_ASSERT_EQUAL_UINT8(1, backend.transmitted[0][0]);
    TEST_ASSERT_EQUAL_UINT64(1, coordinator.stats(rid::Transport::Wifi24).radio_errors);
    TEST_ASSERT_EQUAL_UINT64(0, coordinator.stats(rid::Transport::Wifi24).submitted);
    backend.transmit_result = ESP_OK;
    TEST_ASSERT_EQUAL(ESP_OK, coordinator.poll(1));
    TEST_ASSERT_EQUAL_UINT8(1, backend.transmitted[1][0]);
    TEST_ASSERT_EQUAL_UINT64(1, coordinator.stats(rid::Transport::Wifi24).submitted);
    TEST_ASSERT_EQUAL_UINT64(0, coordinator.stats(rid::Transport::Wifi24).completed);
}

TEST_CASE("ESP32-C5 submits GB Beacon on channels 6 and 149", "[wifi][hardware]") {
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        TEST_ESP_OK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    TEST_ESP_OK(result);
    const uint8_t ssid[] = {'R', 'I', 'D'};
    const uint8_t gb[] = {0xff, 0x20, 0x01, 0x80};
    rid::WifiFrame frame;
    TEST_ASSERT_EQUAL(rid::WifiFrameError::None,
                      rid::build_gb_beacon(frame, kMac.data(), {ssid, sizeof(ssid)}, 1,
                                           {gb, sizeof(gb)}));
    {
        rid::EspWifiBackend backend;
        TEST_ESP_OK(backend.set_country_cn());
        TEST_ESP_OK(backend.set_channel(6));
        TEST_ESP_OK(backend.transmit(frame.view()));
        TEST_ESP_OK(backend.set_channel(149));
        TEST_ESP_OK(backend.transmit(frame.view()));
    }
    TEST_ESP_OK(nvs_flash_deinit());
}

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "rid/model.hpp"

namespace rid {

constexpr size_t kMaxWifiFrameSize = 512;
constexpr size_t kMaxGbVendorPayloadSize = 249;

enum class WifiFrameError : uint8_t {
    None,
    InvalidArgument,
    PayloadTooLarge,
    InvalidFrame,
};

struct WifiFrame {
    std::array<uint8_t, kMaxWifiFrameSize> bytes{};
    uint16_t size{0};
    ByteView view() const { return {bytes.data(), size}; }
};

WifiFrameError build_gb_beacon(WifiFrame &destination, const uint8_t address[6],
                               ByteView ssid, uint8_t counter, ByteView gb_payload);

// Validates only the outer Beacon structure supplied by an external ODID implementation.
// This does not validate or construct OpenDroneID protocol elements.
WifiFrameError validate_opendroneid_beacon(ByteView frame);

class WifiBackend {
public:
    virtual esp_err_t set_country_cn() = 0;
    virtual esp_err_t set_channel(uint8_t primary) = 0;
    virtual esp_err_t transmit(ByteView frame) = 0;
    virtual ~WifiBackend() = default;
};

class EspWifiBackend final : public WifiBackend {
public:
    ~EspWifiBackend() override;
    esp_err_t set_country_cn() override;
    esp_err_t set_channel(uint8_t primary) override;
    esp_err_t transmit(ByteView frame) override;

private:
    void shutdown();

    bool initialized_{false};
    bool owns_event_loop_{false};
    bool owns_netif_{false};
};

}  // namespace rid

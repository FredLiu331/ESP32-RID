#include "rid/wifi_transport.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace rid {
namespace {

constexpr size_t kBeaconHeaderSize = 24;
constexpr size_t kBeaconFixedBodySize = 12;

void append(WifiFrame &frame, uint8_t value) {
    frame.bytes[frame.size++] = value;
}

void append(WifiFrame &frame, const uint8_t *data, size_t size) {
    std::memcpy(frame.bytes.data() + frame.size, data, size);
    frame.size += static_cast<uint16_t>(size);
}

bool valid_beacon_ies(ByteView frame) {
    size_t offset = kBeaconHeaderSize + kBeaconFixedBodySize;
    bool has_ssid = false;
    bool has_rates = false;
    bool has_vendor = false;
    while (offset < frame.size) {
        if (offset + 2 > frame.size) return false;
        const uint8_t id = frame.data[offset];
        const size_t length = frame.data[offset + 1];
        offset += 2;
        if (offset + length > frame.size) return false;
        has_ssid |= id == 0 && length <= 32;
        has_rates |= id == 1 && length > 0 && length <= 8;
        has_vendor |= id == 0xdd && length >= 4;
        offset += length;
    }
    return offset == frame.size && has_ssid && has_rates && has_vendor;
}

}  // namespace

EspWifiBackend::~EspWifiBackend() {
    shutdown();
}

void EspWifiBackend::shutdown() {
    if (initialized_) {
        esp_wifi_stop();
        esp_wifi_deinit();
        initialized_ = false;
    }
    if (owns_event_loop_) {
        esp_event_loop_delete_default();
        owns_event_loop_ = false;
    }
    if (owns_netif_) {
        esp_netif_deinit();
        owns_netif_ = false;
    }
}

WifiFrameError build_gb_beacon(WifiFrame &destination, const uint8_t address[6],
                               ByteView ssid, uint8_t counter, ByteView gb_payload) {
    destination.size = 0;
    if (address == nullptr || (ssid.size != 0 && ssid.data == nullptr) ||
        gb_payload.size == 0 || gb_payload.data == nullptr) {
        return WifiFrameError::InvalidArgument;
    }
    if (ssid.size > 32 || gb_payload.size > kMaxGbVendorPayloadSize) {
        return WifiFrameError::PayloadTooLarge;
    }
    const size_t size = kBeaconHeaderSize + kBeaconFixedBodySize + 2 + ssid.size + 3 +
                        2 + 5 + gb_payload.size;
    if (size > destination.bytes.size()) return WifiFrameError::PayloadTooLarge;

    const uint8_t header_prefix[] = {0x80, 0x00, 0x00, 0x00,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    append(destination, header_prefix, sizeof(header_prefix));
    append(destination, address, 6);
    append(destination, address, 6);
    const uint8_t sequence_and_fixed[] = {
        0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x64, 0x00, 0x01, 0x04,
    };
    append(destination, sequence_and_fixed, sizeof(sequence_and_fixed));
    append(destination, 0x00);
    append(destination, static_cast<uint8_t>(ssid.size));
    if (ssid.size != 0) append(destination, ssid.data, ssid.size);
    const uint8_t rates[] = {0x01, 0x01, 0x8c};
    append(destination, rates, sizeof(rates));
    append(destination, 0xdd);
    append(destination, static_cast<uint8_t>(5 + gb_payload.size));
    const uint8_t vendor[] = {0xfa, 0x0b, 0xbc, 0x0d, counter};
    append(destination, vendor, sizeof(vendor));
    if (gb_payload.size != 0) append(destination, gb_payload.data, gb_payload.size);
    return WifiFrameError::None;
}

WifiFrameError validate_opendroneid_beacon(ByteView frame) {
    if (frame.data == nullptr || frame.size < kBeaconHeaderSize + kBeaconFixedBodySize + 2 ||
        frame.size > kMaxWifiFrameSize || (frame.data[0] & 0xfcU) != 0x80U) {
        return WifiFrameError::InvalidFrame;
    }
    return valid_beacon_ies(frame) ? WifiFrameError::None : WifiFrameError::InvalidFrame;
}

esp_err_t EspWifiBackend::set_country_cn() {
    if (!initialized_) {
        esp_err_t result = esp_netif_init();
        if (result == ESP_OK) {
            owns_netif_ = true;
        } else if (result != ESP_ERR_INVALID_STATE) {
            return result;
        }
        result = esp_event_loop_create_default();
        if (result == ESP_OK) {
            owns_event_loop_ = true;
        } else if (result != ESP_ERR_INVALID_STATE) {
            shutdown();
            return result;
        }
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        result = esp_wifi_init(&config);
        if (result != ESP_OK) {
            shutdown();
            return result;
        }
        result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (result == ESP_OK) result = esp_wifi_set_mode(WIFI_MODE_STA);
        if (result == ESP_OK) result = esp_wifi_start();
        if (result != ESP_OK) {
            esp_wifi_deinit();
            shutdown();
            return result;
        }
        initialized_ = true;
    }
    wifi_country_t country{};
    std::memcpy(country.cc, "CN", 3);
    country.schan = 1;
    country.nchan = 13;
    country.max_tx_power = 8;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    country.wifi_5g_channel_mask = WIFI_CHANNEL_149;
#endif
    esp_err_t result = esp_wifi_set_country(&country);
    if (result == ESP_OK) result = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    if (result == ESP_OK) result = esp_wifi_set_max_tx_power(8);
    return result;
}

esp_err_t EspWifiBackend::set_channel(uint8_t primary) {
    if (primary != 6 && primary != 149) return ESP_ERR_INVALID_ARG;
    return esp_wifi_set_channel(primary, WIFI_SECOND_CHAN_NONE);
}

esp_err_t EspWifiBackend::transmit(ByteView frame) {
    if (frame.data == nullptr || frame.size == 0 || frame.size > kMaxWifiFrameSize) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_wifi_80211_tx(WIFI_IF_STA, frame.data, frame.size, true);
}

}  // namespace rid

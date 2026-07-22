#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "rid/ble_transport.hpp"
#include "rid/odid_encoder.hpp"
#include "unity.h"

namespace {

static_assert(rid::kBleExtendedMaxPayloadSize == 158,
              "BLE5 must fit a six-message OpenDroneID Service Data AD");

constexpr uint16_t kAcceptanceAircraftPerMode = 25;
constexpr uint16_t kAcceptanceAircraftCount = 2 * kAcceptanceAircraftPerMode;
constexpr uint64_t kAcceptanceDurationMs = 10ULL * 60ULL * 1000ULL;
constexpr uint64_t kAcceptanceDrainTimeoutMs = 30ULL * 1000ULL;
constexpr uint32_t kAcceptancePollPeriodMs = 10;
constexpr uint32_t kNimbleSyncTimeoutMs = 5000;

SemaphoreHandle_t g_nimble_sync;
uint8_t g_nimble_address_type;

uint64_t monotonic_ms() {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void nimble_on_sync() {
    if (ble_hs_id_infer_auto(0, &g_nimble_address_type) == 0) xSemaphoreGive(g_nimble_sync);
}

void nimble_host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool stop_nimble();

bool start_nimble() {
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_result = nvs_flash_erase();
        if (nvs_result == ESP_OK) nvs_result = nvs_flash_init();
    }
    if (nvs_result != ESP_OK) return false;
    g_nimble_sync = xSemaphoreCreateBinary();
    if (g_nimble_sync == nullptr) {
        nvs_flash_deinit();
        return false;
    }
    if (nimble_port_init() != ESP_OK) {
        vSemaphoreDelete(g_nimble_sync);
        g_nimble_sync = nullptr;
        nvs_flash_deinit();
        return false;
    }
    ble_hs_cfg.sync_cb = nimble_on_sync;
    nimble_port_freertos_init(nimble_host_task);
    if (xSemaphoreTake(g_nimble_sync, pdMS_TO_TICKS(kNimbleSyncTimeoutMs)) != pdTRUE) {
        stop_nimble();
        return false;
    }
    return true;
}

bool stop_nimble() {
    const bool stopped = nimble_port_stop() == 0;
    if (!stopped) return false;
    const bool deinitialized = nimble_port_deinit() == ESP_OK;
    if (g_nimble_sync != nullptr) {
        vSemaphoreDelete(g_nimble_sync);
        g_nimble_sync = nullptr;
    }
    const bool nvs_deinitialized = nvs_flash_deinit() == ESP_OK;
    return stopped && deinitialized && nvs_deinitialized;
}

rid::ScheduledPayload acceptance_payload(uint32_t sequence, uint16_t aircraft_index,
                                         rid::Transport transport,
                                         const rid::BleAdvertisingData &advertising_data,
                                         uint64_t now_ms) {
    rid::ScheduledPayload result;
    result.sequence = sequence;
    result.aircraft_index = aircraft_index;
    result.transport = transport;
    result.deadline_ms = now_ms;
    result.expires_at_ms = now_ms + 5000;
    rid::ScheduledPayload::try_copy_from(result, advertising_data.view());
    result.bytes[4] = static_cast<uint8_t>(sequence);
    return result;
}

rid::ScheduledPayload payload(uint16_t aircraft_index, rid::Transport transport,
                              uint64_t deadline_ms, size_t size = 3) {
    rid::ScheduledPayload result;
    result.sequence = aircraft_index;
    result.aircraft_index = aircraft_index;
    result.transport = transport;
    result.deadline_ms = deadline_ms;
    result.expires_at_ms = deadline_ms + 1000;
    result.size = static_cast<uint16_t>(size);
    result.bytes[0] = static_cast<uint8_t>(aircraft_index);
    return result;
}

class FakeBleGap final : public rid::BleGapBackend {
public:
    esp_err_t configure(rid::BleMode mode, rid::ByteView payload) override {
        ++configure_calls;
        last_mode = mode;
        last_payload.assign(payload.data, payload.data + payload.size);
        return configure_result;
    }

    esp_err_t start() override {
        ++start_calls;
        return start_result;
    }

    esp_err_t stop() override {
        ++stop_calls;
        return ESP_OK;
    }

    esp_err_t reset() override {
        ++reset_calls;
        return reset_calls <= reset_failures ? ESP_FAIL : ESP_OK;
    }

    void set_completion_callback(rid::BleCompletionCallback callback, void *context) override {
        callback_ = callback;
        callback_context_ = context;
    }

    void complete(esp_err_t status = ESP_OK, uint16_t event_count = 1) {
        callback_(callback_context_, rid::BleCompletionEvent{status, event_count});
    }

    bool callback_registered() const { return callback_ != nullptr || callback_context_ != nullptr; }

    esp_err_t configure_result{ESP_OK};
    esp_err_t start_result{ESP_OK};
    unsigned configure_calls{0};
    unsigned start_calls{0};
    unsigned stop_calls{0};
    unsigned reset_calls{0};
    unsigned reset_failures{0};
    rid::BleMode last_mode{rid::BleMode::Legacy};
    std::vector<uint8_t> last_payload;

private:
    rid::BleCompletionCallback callback_{nullptr};
    void *callback_context_{nullptr};
};

}  // namespace

TEST_CASE("OpenDroneID BLE Service Data AD is encoded byte for byte", "[ble]") {
    std::array<uint8_t, 25> message{};
    for (size_t index = 0; index < message.size(); ++index) {
        message[index] = static_cast<uint8_t>(0x20 + index);
    }

    rid::BleAdvertisingData advertising_data;
    TEST_ASSERT_EQUAL(rid::BleAdError::None,
                      rid::build_opendroneid_service_data(
                          advertising_data, {message.data(), message.size()}, 0x5a));
    const rid::ByteView encoded = advertising_data.view();
    TEST_ASSERT_EQUAL_UINT32(30, encoded.size);
    TEST_ASSERT_EQUAL_HEX8(29, encoded.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x16, encoded.data[1]);
    TEST_ASSERT_EQUAL_HEX8(0xfa, encoded.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xff, encoded.data[3]);
    TEST_ASSERT_EQUAL_HEX8(0x5a, encoded.data[4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(message.data(), encoded.data + 5, message.size());
}

TEST_CASE("OpenDroneID BLE Service Data AD accepts six-message pack and rejects invalid sizes",
          "[ble]") {
    std::array<uint8_t, 153> pack{};
    pack[0] = 0xf2;
    pack[1] = 25;
    pack[2] = 6;
    rid::BleAdvertisingData advertising_data;

    TEST_ASSERT_EQUAL(rid::BleAdError::None,
                      rid::build_opendroneid_service_data(
                          advertising_data, {pack.data(), pack.size()}, 7));
    TEST_ASSERT_EQUAL_UINT32(158, advertising_data.view().size);
    TEST_ASSERT_EQUAL_HEX8(157, advertising_data.view().data[0]);
    TEST_ASSERT_EQUAL_HEX8(7, advertising_data.view().data[4]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pack.data(), advertising_data.view().data + 5, pack.size());

    std::array<uint8_t, 26> malformed{};
    TEST_ASSERT_EQUAL(rid::BleAdError::InvalidPayload,
                      rid::build_opendroneid_service_data(
                          advertising_data, {malformed.data(), malformed.size()}, 0));
    TEST_ASSERT_EQUAL_UINT32(0, advertising_data.view().size);

    std::array<uint8_t, 28> malformed_pack{};
    malformed_pack[0] = 0xe2;
    malformed_pack[1] = 25;
    malformed_pack[2] = 1;
    TEST_ASSERT_EQUAL(rid::BleAdError::InvalidPayload,
                      rid::build_opendroneid_service_data(
                          advertising_data, {malformed_pack.data(), malformed_pack.size()}, 0));
    malformed_pack[0] = 0xf2;
    malformed_pack[1] = 24;
    TEST_ASSERT_EQUAL(rid::BleAdError::InvalidPayload,
                      rid::build_opendroneid_service_data(
                          advertising_data, {malformed_pack.data(), malformed_pack.size()}, 0));
    malformed_pack[1] = 25;
    malformed_pack[2] = 2;
    TEST_ASSERT_EQUAL(rid::BleAdError::InvalidPayload,
                      rid::build_opendroneid_service_data(
                          advertising_data, {malformed_pack.data(), malformed_pack.size()}, 0));
    std::array<uint8_t, 154> too_large{};
    TEST_ASSERT_EQUAL(rid::BleAdError::PayloadTooLarge,
                      rid::build_opendroneid_service_data(
                          advertising_data, {too_large.data(), too_large.size()}, 0));
}

TEST_CASE("BLE transport rotates aircraft by deadline", "[ble]") {
    FakeBleGap gap;
    rid::BleTransport transport(gap);
    TEST_ASSERT_EQUAL(rid::BleTransportError::None,
                      transport.submit(payload(1, rid::Transport::Ble4, 100)));
    TEST_ASSERT_EQUAL(rid::BleTransportError::None,
                      transport.submit(payload(2, rid::Transport::Ble5, 90)));

    TEST_ASSERT_EQUAL(rid::BleTransportError::None, transport.poll(90));
    TEST_ASSERT_EQUAL(rid::BleMode::Extended, gap.last_mode);
    TEST_ASSERT_EQUAL_UINT8(2, gap.last_payload[0]);

    gap.complete();
    TEST_ASSERT_EQUAL_UINT64(0, transport.stats(rid::Transport::Ble5).completed);
    TEST_ASSERT_EQUAL(rid::BleTransportError::None, transport.poll(100));
    TEST_ASSERT_EQUAL_UINT64(1, transport.stats(rid::Transport::Ble5).completed);
    TEST_ASSERT_EQUAL(rid::BleMode::Legacy, gap.last_mode);
    TEST_ASSERT_EQUAL_UINT8(1, gap.last_payload[0]);
}

TEST_CASE("BLE transport enforces mode payload limits before GAP", "[ble]") {
    FakeBleGap gap;
    rid::BleTransport transport(gap);

    TEST_ASSERT_EQUAL(rid::BleTransportError::None,
                      transport.submit(payload(1, rid::Transport::Ble4, 1,
                                               rid::kBleLegacyMaxPayloadSize)));
    TEST_ASSERT_EQUAL(rid::BleTransportError::PayloadTooLarge,
                      transport.submit(payload(2, rid::Transport::Ble4, 2,
                                               rid::kBleLegacyMaxPayloadSize + 1)));
    TEST_ASSERT_EQUAL(rid::BleTransportError::None,
                      transport.submit(payload(3, rid::Transport::Ble5, 3,
                                               rid::kBleExtendedMaxPayloadSize)));
    TEST_ASSERT_EQUAL(rid::BleTransportError::InvalidPayload,
                      transport.submit(payload(4, rid::Transport::Ble5, 4,
                                               rid::kBleExtendedMaxPayloadSize + 1)));
    TEST_ASSERT_EQUAL(rid::BleTransportError::InvalidTransport,
                      transport.submit(payload(5, rid::Transport::Wifi24, 5)));
    TEST_ASSERT_EQUAL_UINT32(0, gap.configure_calls);
}

TEST_CASE("BLE completion callback only queues lightweight event", "[ble]") {
    FakeBleGap gap;
    rid::BleTransport transport(gap);
    transport.submit(payload(1, rid::Transport::Ble4, 1));
    TEST_ASSERT_EQUAL(rid::BleTransportError::None, transport.poll(1));

    gap.complete(ESP_OK, 3);
    TEST_ASSERT_TRUE(transport.busy());
    TEST_ASSERT_EQUAL_UINT64(0, transport.stats(rid::Transport::Ble4).completed);

    TEST_ASSERT_EQUAL(rid::BleTransportError::None, transport.poll(2));
    TEST_ASSERT_FALSE(transport.busy());
    TEST_ASSERT_EQUAL_UINT64(1, transport.stats(rid::Transport::Ble4).completed);
    TEST_ASSERT_EQUAL_UINT32(1, gap.configure_calls);
}

TEST_CASE("BLE transport records GAP failures without becoming busy", "[ble]") {
    FakeBleGap gap;
    rid::BleTransport transport(gap);
    transport.submit(payload(1, rid::Transport::Ble4, 1));
    gap.configure_result = ESP_FAIL;

    TEST_ASSERT_EQUAL(rid::BleTransportError::BackendError, transport.poll(1));
    TEST_ASSERT_FALSE(transport.busy());
    TEST_ASSERT_EQUAL_UINT64(1, transport.stats(rid::Transport::Ble4).radio_errors);
    TEST_ASSERT_EQUAL_UINT64(0, transport.stats(rid::Transport::Ble4).submitted);
}

TEST_CASE("BLE transport shutdown unbinds callback and resets GAP instances", "[ble]") {
    FakeBleGap gap;
    {
        rid::BleTransport transport(gap);
        transport.submit(payload(1, rid::Transport::Ble4, 1));
        TEST_ASSERT_EQUAL(rid::BleTransportError::None, transport.poll(1));
        TEST_ASSERT_EQUAL(ESP_OK, transport.shutdown());
        TEST_ASSERT_EQUAL(ESP_OK, transport.shutdown());
    }

    TEST_ASSERT_FALSE(gap.callback_registered());
    TEST_ASSERT_EQUAL_UINT32(1, gap.reset_calls);
}

TEST_CASE("BLE transport retries a failed GAP reset before unbinding", "[ble]") {
    FakeBleGap gap;
    gap.reset_failures = 1;
    rid::BleTransport transport(gap);

    TEST_ASSERT_EQUAL(ESP_FAIL, transport.shutdown());
    TEST_ASSERT_FALSE(gap.callback_registered());
    TEST_ASSERT_EQUAL(rid::BleTransportError::BackendError,
                      transport.submit(payload(2, rid::Transport::Ble5, 2)));
    TEST_ASSERT_EQUAL(ESP_OK, transport.shutdown());
    TEST_ASSERT_FALSE(gap.callback_registered());
    TEST_ASSERT_EQUAL_UINT32(2, gap.reset_calls);
}

TEST_CASE("BLE4 and BLE5 rotate 50 aircraft for ten minutes on NimBLE",
          "[ble][acceptance]") {
    rid::OdidOptions options;
    options.operator_position = rid::GeoPoint{31.2304, 121.4737};
    const rid::FlightState state{
        31.2304, 121.4737, 120.0F, 50.0F, 8.0F, 0.0F, 90.0F, true, 1000,
    };
    const rid::OdidEncoder encoder;
    const auto basic_id = encoder.encode(rid::MessageKind::BasicId, state,
                                         {"RID-ACCEPT-000000001"}, options);
    const auto message_pack = encoder.encode_pack(state, {"RID-ACCEPT-000000001"}, options);
    TEST_ASSERT_TRUE(basic_id.ok());
    TEST_ASSERT_TRUE(message_pack.ok());

    rid::BleAdvertisingData legacy_ad;
    rid::BleAdvertisingData extended_ad;
    TEST_ASSERT_EQUAL(rid::BleAdError::None,
                      rid::build_opendroneid_service_data(
                          legacy_ad, {basic_id.value().data(), basic_id.value().size()}, 0));
    TEST_ASSERT_EQUAL(rid::BleAdError::None,
                      rid::build_opendroneid_service_data(
                          extended_ad, {message_pack.value().data(), message_pack.value().size()},
                          0));
    TEST_ASSERT_EQUAL_UINT32(30, legacy_ad.view().size);
    TEST_ASSERT_EQUAL_UINT32(158, extended_ad.view().size);
    TEST_ASSERT_TRUE(start_nimble());

    uint64_t submitted_ble4 = 0;
    uint64_t completed_ble4 = 0;
    uint64_t submitted_ble5 = 0;
    uint64_t completed_ble5 = 0;
    uint64_t radio_errors = 0;
    uint32_t heap_start = 0;
    uint32_t heap_end = 0;
    uint32_t min_heap = 0;
    bool transport_ok = true;
    {
        rid::NimbleBleGapBackend gap(g_nimble_address_type);
        rid::BleTransport transport(gap);
        uint32_t next_sequence = 0;
        const uint64_t started_at_ms = monotonic_ms();
        for (uint16_t aircraft = 0; aircraft < kAcceptanceAircraftCount; ++aircraft) {
            const rid::Transport mode = aircraft < kAcceptanceAircraftPerMode
                                            ? rid::Transport::Ble4
                                            : rid::Transport::Ble5;
            const auto &ad = mode == rid::Transport::Ble4 ? legacy_ad : extended_ad;
            transport_ok &= transport.submit(acceptance_payload(
                                next_sequence++, aircraft, mode, ad, started_at_ms)) ==
                            rid::BleTransportError::None;
        }
        uint64_t observed_completions = 0;
        while (monotonic_ms() - started_at_ms < kAcceptanceDurationMs) {
            const uint64_t now_ms = monotonic_ms();
            transport_ok &= transport.poll(now_ms) != rid::BleTransportError::BackendError;
            const uint64_t current_completions =
                transport.stats(rid::Transport::Ble4).completed +
                transport.stats(rid::Transport::Ble5).completed;
            if (heap_start == 0 && current_completions >= kAcceptanceAircraftCount) {
                heap_start = esp_get_free_heap_size();
            }
            while (observed_completions < current_completions) {
                const uint16_t aircraft =
                    static_cast<uint16_t>(next_sequence % kAcceptanceAircraftCount);
                const rid::Transport mode = aircraft < kAcceptanceAircraftPerMode
                                                ? rid::Transport::Ble4
                                                : rid::Transport::Ble5;
                const auto &ad = mode == rid::Transport::Ble4 ? legacy_ad : extended_ad;
                transport_ok &= transport.submit(
                                    acceptance_payload(next_sequence++, aircraft, mode, ad,
                                                       now_ms)) ==
                                rid::BleTransportError::None;
                ++observed_completions;
            }
            vTaskDelay(pdMS_TO_TICKS(kAcceptancePollPeriodMs));
        }

        const uint64_t drain_started_at_ms = monotonic_ms();
        while ((transport.busy() || transport.depth() > 0) &&
               monotonic_ms() - drain_started_at_ms < kAcceptanceDrainTimeoutMs) {
            transport_ok &= transport.poll(monotonic_ms()) !=
                            rid::BleTransportError::BackendError;
            vTaskDelay(pdMS_TO_TICKS(kAcceptancePollPeriodMs));
        }

        const auto ble4_stats = transport.stats(rid::Transport::Ble4);
        const auto ble5_stats = transport.stats(rid::Transport::Ble5);
        submitted_ble4 = ble4_stats.submitted;
        completed_ble4 = ble4_stats.completed;
        submitted_ble5 = ble5_stats.submitted;
        completed_ble5 = ble5_stats.completed;
        radio_errors = ble4_stats.radio_errors + ble5_stats.radio_errors;
        heap_end = esp_get_free_heap_size();
        min_heap = esp_get_minimum_free_heap_size();
        transport_ok &= transport.shutdown() == ESP_OK;
    }
    transport_ok &= stop_nimble();

    std::printf("BLE_ACCEPTANCE submitted=%llu completed=%llu radio_errors=%llu "
                "ble4_submitted=%llu ble4_completed=%llu ble5_submitted=%llu "
                "ble5_completed=%llu heap_start=%lu heap_end=%lu min_heap=%lu\n",
                static_cast<unsigned long long>(submitted_ble4 + submitted_ble5),
                static_cast<unsigned long long>(completed_ble4 + completed_ble5),
                static_cast<unsigned long long>(radio_errors),
                static_cast<unsigned long long>(submitted_ble4),
                static_cast<unsigned long long>(completed_ble4),
                static_cast<unsigned long long>(submitted_ble5),
                static_cast<unsigned long long>(completed_ble5),
                static_cast<unsigned long>(heap_start), static_cast<unsigned long>(heap_end),
                static_cast<unsigned long>(min_heap));

    TEST_ASSERT_TRUE(transport_ok);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(kAcceptanceAircraftPerMode, completed_ble4);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(kAcceptanceAircraftPerMode, completed_ble5);
    TEST_ASSERT_EQUAL_UINT64(submitted_ble4, completed_ble4);
    TEST_ASSERT_EQUAL_UINT64(submitted_ble5, completed_ble5);
    TEST_ASSERT_EQUAL_UINT64(0, radio_errors);
    TEST_ASSERT_GREATER_THAN_UINT32(0, heap_start);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(heap_start, heap_end);
}

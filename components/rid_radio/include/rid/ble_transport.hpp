#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"

#include "rid/scheduler.hpp"

struct ble_gap_event;

namespace rid {

// Limits apply to complete Advertising Data, including AD headers and service metadata.
constexpr size_t kBleLegacyMaxPayloadSize = 31;
constexpr size_t kBleExtendedMaxPayloadSize = 158;
constexpr size_t kMaxBlePendingPayloads = 16;
constexpr size_t kOpenDroneIdMaxEncodedSize = 153;

enum class BleMode : uint8_t { Legacy, Extended };

enum class BleTransportError : uint8_t {
    None,
    InvalidTransport,
    InvalidPayload,
    PayloadTooLarge,
    QueueFull,
    BackendError,
};

enum class BleAdError : uint8_t { None, InvalidPayload, PayloadTooLarge };

struct BleAdvertisingData {
    std::array<uint8_t, kBleExtendedMaxPayloadSize> bytes{};
    uint16_t size{0};

    ByteView view() const { return {bytes.data(), size}; }
};

// Frames only OpenDroneID encoded messages. GB 46750 BLE framing is intentionally separate.
BleAdError build_opendroneid_service_data(BleAdvertisingData &destination,
                                           ByteView encoded_messages, uint8_t counter);

struct BleCompletionEvent {
    esp_err_t status;
    uint16_t advertising_event_count;
};

using BleCompletionCallback = void (*)(void *context, BleCompletionEvent event);

class BleGapBackend {
public:
    virtual esp_err_t configure(BleMode mode, ByteView payload) = 0;
    virtual esp_err_t start() = 0;
    virtual esp_err_t stop() = 0;
    virtual esp_err_t reset() = 0;
    virtual void set_completion_callback(BleCompletionCallback callback, void *context) = 0;
    virtual ~BleGapBackend() = default;
};

class BleTransport {
public:
    explicit BleTransport(BleGapBackend &backend);
    ~BleTransport();

    // payload.bytes must already contain complete Advertising Data.
    BleTransportError submit(const ScheduledPayload &payload);
    BleTransportError poll(uint64_t now_ms);
    esp_err_t shutdown();
    bool busy() const { return busy_; }
    size_t depth() const { return pending_count_; }
    const TransportStats &stats(Transport transport) const;

private:
    static constexpr size_t kCompletionQueueCapacity = 8;

    static void completion_callback(void *context, BleCompletionEvent event);
    void enqueue_completion(BleCompletionEvent event);
    void drain_completions();
    size_t earliest_pending_index() const;
    void erase_pending(size_t index);

    BleGapBackend &backend_;
    std::array<ScheduledPayload, kMaxBlePendingPayloads> pending_{};
    size_t pending_count_{0};
    std::array<BleCompletionEvent, kCompletionQueueCapacity> completions_{};
    std::atomic<size_t> completion_write_{0};
    std::atomic<size_t> completion_read_{0};
    std::atomic<uint32_t> completion_overflows_{0};
    std::array<TransportStats, 2> stats_{};
    TransportStats invalid_stats_{};
    Transport active_transport_{Transport::Ble4};
    bool busy_{false};
    bool shutting_down_{false};
    bool shut_down_{false};
};

class NimbleBleGapBackend final : public BleGapBackend {
public:
    explicit NimbleBleGapBackend(uint8_t own_address_type, uint8_t legacy_instance = 0,
                                 uint8_t extended_instance = 1);

    esp_err_t configure(BleMode mode, ByteView payload) override;
    esp_err_t start() override;
    esp_err_t stop() override;
    esp_err_t reset() override;
    void set_completion_callback(BleCompletionCallback callback, void *context) override;

private:
    static int gap_event(ble_gap_event *event, void *argument);
    static size_t mode_index(BleMode mode);

    uint8_t own_address_type_;
    std::array<uint8_t, 2> instances_;
    std::array<bool, 2> configured_{};
    BleMode active_mode_{BleMode::Legacy};
    std::atomic<BleCompletionCallback> completion_callback_{nullptr};
    std::atomic<void *> completion_context_{nullptr};
    std::atomic<uint32_t> callbacks_in_flight_{0};
};

}  // namespace rid

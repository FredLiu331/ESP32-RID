#include "rid/ble_transport.hpp"

#include <cstring>

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rid {
namespace {

constexpr size_t stats_index(Transport transport) {
    return transport == Transport::Ble5 ? 1 : 0;
}

bool earlier(const ScheduledPayload &left, const ScheduledPayload &right) {
    if (left.deadline_ms != right.deadline_ms) return left.deadline_ms < right.deadline_ms;
    return left.sequence < right.sequence;
}

void increment(uint64_t &counter) {
    counter = saturating_increment(counter);
}

}  // namespace

BleAdError build_opendroneid_service_data(BleAdvertisingData &destination,
                                           ByteView encoded_messages, uint8_t counter) {
    destination.size = 0;
    if (encoded_messages.size > kOpenDroneIdMaxEncodedSize) {
        return BleAdError::PayloadTooLarge;
    }
    const bool single_message = encoded_messages.size == 25;
    const bool pack_length = encoded_messages.size >= 28 &&
                             (encoded_messages.size - 3) % 25 == 0;
    const bool message_pack =
        pack_length && encoded_messages.data != nullptr &&
        (encoded_messages.data[0] & 0xf0U) == 0xf0U && encoded_messages.data[1] == 25 &&
        encoded_messages.data[2] == (encoded_messages.size - 3) / 25;
    if (encoded_messages.data == nullptr || (!single_message && !message_pack)) {
        return BleAdError::InvalidPayload;
    }

    destination.size = static_cast<uint16_t>(encoded_messages.size + 5);
    destination.bytes[0] = static_cast<uint8_t>(encoded_messages.size + 4);
    destination.bytes[1] = 0x16;
    destination.bytes[2] = 0xfa;
    destination.bytes[3] = 0xff;
    destination.bytes[4] = counter;
    std::memcpy(destination.bytes.data() + 5, encoded_messages.data, encoded_messages.size);
    return BleAdError::None;
}

BleTransport::BleTransport(BleGapBackend &backend) : backend_(backend) {
    backend_.set_completion_callback(completion_callback, this);
}

BleTransport::~BleTransport() {
    shutdown();
}

BleTransportError BleTransport::submit(const ScheduledPayload &payload) {
    if (shutting_down_ || shut_down_) return BleTransportError::BackendError;
    if (payload.transport != Transport::Ble4 && payload.transport != Transport::Ble5) {
        return BleTransportError::InvalidTransport;
    }
    if (payload.size == 0 || payload.size > payload.bytes.size()) {
        return BleTransportError::InvalidPayload;
    }
    const size_t maximum = payload.transport == Transport::Ble4
                               ? kBleLegacyMaxPayloadSize
                               : kBleExtendedMaxPayloadSize;
    if (payload.size > maximum) return BleTransportError::PayloadTooLarge;
    if (pending_count_ == pending_.size()) return BleTransportError::QueueFull;

    pending_[pending_count_++] = payload;
    increment(stats_[stats_index(payload.transport)].expected);
    return BleTransportError::None;
}

BleTransportError BleTransport::poll(uint64_t now_ms) {
    if (shutting_down_ || shut_down_) return BleTransportError::BackendError;
    drain_completions();
    if (busy_) return BleTransportError::None;

    while (pending_count_ > 0) {
        const size_t selected_index = earliest_pending_index();
        const ScheduledPayload selected = pending_[selected_index];
        erase_pending(selected_index);

        if (selected.expires_at_ms != 0 && now_ms > selected.expires_at_ms) {
            increment(stats_[stats_index(selected.transport)].dropped);
            continue;
        }

        const BleMode mode = selected.transport == Transport::Ble4 ? BleMode::Legacy
                                                                    : BleMode::Extended;
        auto &selected_stats = stats_[stats_index(selected.transport)];
        esp_err_t error = backend_.configure(mode, {selected.bytes.data(), selected.size});
        if (error == ESP_OK) error = backend_.start();
        if (error != ESP_OK) {
            increment(selected_stats.radio_errors);
            return BleTransportError::BackendError;
        }

        active_transport_ = selected.transport;
        busy_ = true;
        increment(selected_stats.submitted);
        if (now_ms > selected.deadline_ms) increment(selected_stats.late);
        return BleTransportError::None;
    }
    return BleTransportError::None;
}

esp_err_t BleTransport::shutdown() {
    if (shut_down_) return ESP_OK;
    if (!shutting_down_) {
        shutting_down_ = true;
        backend_.set_completion_callback(nullptr, nullptr);
    }
    const esp_err_t result = backend_.reset();
    if (result != ESP_OK) return result;
    shut_down_ = true;
    busy_ = false;
    pending_count_ = 0;
    return ESP_OK;
}

const TransportStats &BleTransport::stats(Transport transport) const {
    if (transport != Transport::Ble4 && transport != Transport::Ble5) return invalid_stats_;
    return stats_[stats_index(transport)];
}

void BleTransport::completion_callback(void *context, BleCompletionEvent event) {
    if (context != nullptr) static_cast<BleTransport *>(context)->enqueue_completion(event);
}

void BleTransport::enqueue_completion(BleCompletionEvent event) {
    const size_t write = completion_write_.load(std::memory_order_relaxed);
    const size_t next = (write + 1) % completions_.size();
    if (next == completion_read_.load(std::memory_order_acquire)) {
        completion_overflows_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    completions_[write] = event;
    completion_write_.store(next, std::memory_order_release);
}

void BleTransport::drain_completions() {
    size_t read = completion_read_.load(std::memory_order_relaxed);
    const size_t write = completion_write_.load(std::memory_order_acquire);
    while (read != write) {
        const BleCompletionEvent event = completions_[read];
        if (busy_) {
            auto &active_stats = stats_[stats_index(active_transport_)];
            if (event.status == ESP_OK && event.advertising_event_count > 0) {
                increment(active_stats.completed);
            } else {
                increment(active_stats.radio_errors);
            }
            busy_ = false;
        }
        read = (read + 1) % completions_.size();
    }
    completion_read_.store(read, std::memory_order_release);

    const uint32_t overflows = completion_overflows_.exchange(0, std::memory_order_acq_rel);
    if (overflows > 0 && busy_) {
        increment(stats_[stats_index(active_transport_)].radio_errors);
        busy_ = false;
    }
}

size_t BleTransport::earliest_pending_index() const {
    size_t result = 0;
    for (size_t index = 1; index < pending_count_; ++index) {
        if (earlier(pending_[index], pending_[result])) result = index;
    }
    return result;
}

void BleTransport::erase_pending(size_t index) {
    for (size_t current = index + 1; current < pending_count_; ++current) {
        pending_[current - 1] = pending_[current];
    }
    --pending_count_;
}

NimbleBleGapBackend::NimbleBleGapBackend(uint8_t own_address_type, uint8_t legacy_instance,
                                         uint8_t extended_instance)
    : own_address_type_(own_address_type), instances_{legacy_instance, extended_instance} {}

esp_err_t NimbleBleGapBackend::configure(BleMode mode, ByteView payload) {
    const size_t index = mode_index(mode);
    const size_t maximum = mode == BleMode::Legacy ? kBleLegacyMaxPayloadSize
                                                    : kBleExtendedMaxPayloadSize;
    if (payload.data == nullptr || payload.size == 0) return ESP_ERR_INVALID_ARG;
    if (payload.size > maximum) return ESP_ERR_INVALID_SIZE;

    const uint8_t instance = instances_[index];
    if (!configured_[index]) {
        ble_gap_ext_adv_params parameters{};
        parameters.legacy_pdu = mode == BleMode::Legacy;
        parameters.own_addr_type = own_address_type_;
        parameters.primary_phy = BLE_HCI_LE_PHY_1M;
        parameters.secondary_phy = BLE_HCI_LE_PHY_1M;
        parameters.tx_power = 127;
        parameters.sid = instance;
        parameters.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        parameters.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
        const int configure_result = ble_gap_ext_adv_configure(
            instance, &parameters, nullptr, gap_event, this);
        if (configure_result != 0) return static_cast<esp_err_t>(configure_result);
        configured_[index] = true;
    }

    os_mbuf *data = os_msys_get_pkthdr(payload.size, 0);
    if (data == nullptr) return ESP_ERR_NO_MEM;
    const int append_result = os_mbuf_append(data, payload.data, payload.size);
    if (append_result != 0) {
        os_mbuf_free_chain(data);
        return static_cast<esp_err_t>(append_result);
    }
    const int data_result = ble_gap_ext_adv_set_data(instance, data);
    if (data_result != 0) return static_cast<esp_err_t>(data_result);
    active_mode_ = mode;
    return ESP_OK;
}

esp_err_t NimbleBleGapBackend::start() {
    return static_cast<esp_err_t>(
        ble_gap_ext_adv_start(instances_[mode_index(active_mode_)], 0, 1));
}

esp_err_t NimbleBleGapBackend::stop() {
    const int result = ble_gap_ext_adv_stop(instances_[mode_index(active_mode_)]);
    return result == 0 || result == BLE_HS_EALREADY ? ESP_OK : static_cast<esp_err_t>(result);
}

esp_err_t NimbleBleGapBackend::reset() {
    esp_err_t first_error = ESP_OK;
    for (size_t index = 0; index < instances_.size(); ++index) {
        if (!configured_[index]) continue;
        const int stop_result = ble_gap_ext_adv_stop(instances_[index]);
        if (stop_result != 0 && stop_result != BLE_HS_EALREADY && first_error == ESP_OK) {
            first_error = static_cast<esp_err_t>(stop_result);
        }
        const int remove_result = ble_gap_ext_adv_remove(instances_[index]);
        if (remove_result != 0 && first_error == ESP_OK) {
            first_error = static_cast<esp_err_t>(remove_result);
        }
        if (remove_result == 0) configured_[index] = false;
    }
    return first_error;
}

void NimbleBleGapBackend::set_completion_callback(BleCompletionCallback callback,
                                                   void *context) {
    if (callback == nullptr) {
        completion_callback_.store(nullptr, std::memory_order_release);
        while (callbacks_in_flight_.load(std::memory_order_acquire) != 0) taskYIELD();
        completion_context_.store(nullptr, std::memory_order_release);
        return;
    }
    completion_context_.store(context, std::memory_order_relaxed);
    completion_callback_.store(callback, std::memory_order_release);
}

int NimbleBleGapBackend::gap_event(ble_gap_event *event, void *argument) {
    auto *backend = static_cast<NimbleBleGapBackend *>(argument);
    if (backend == nullptr || event == nullptr || event->type != BLE_GAP_EVENT_ADV_COMPLETE) {
        return 0;
    }
    const uint8_t active_instance = backend->instances_[mode_index(backend->active_mode_)];
    if (event->adv_complete.instance != active_instance) return 0;
    const esp_err_t status = event->adv_complete.reason == BLE_HS_ETIMEOUT
                                 ? ESP_OK
                                 : static_cast<esp_err_t>(event->adv_complete.reason);
    backend->callbacks_in_flight_.fetch_add(1, std::memory_order_acq_rel);
    const BleCompletionCallback callback =
        backend->completion_callback_.load(std::memory_order_acquire);
    if (callback != nullptr) {
        callback(backend->completion_context_.load(std::memory_order_relaxed),
                 BleCompletionEvent{status, event->adv_complete.num_ext_adv_events});
    }
    backend->callbacks_in_flight_.fetch_sub(1, std::memory_order_release);
    return 0;
}

size_t NimbleBleGapBackend::mode_index(BleMode mode) {
    return mode == BleMode::Extended ? 1 : 0;
}

}  // namespace rid

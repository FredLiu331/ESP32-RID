#include "rid/radio_coordinator.hpp"

namespace rid {
namespace {

void increment(uint64_t &value) {
    value = saturating_increment(value);
}

}  // namespace

RadioCoordinator::RadioCoordinator(WifiBackend &backend, uint64_t dwell_ms, uint64_t retry_ms)
    : backend_(backend), dwell_ms_(dwell_ms), retry_ms_(retry_ms) {}

size_t RadioCoordinator::band_index(Transport transport) {
    return transport == Transport::Wifi58 ? 1 : 0;
}

uint8_t RadioCoordinator::band_channel(size_t band) {
    return band == 0 ? 6 : 149;
}

esp_err_t RadioCoordinator::submit(const ScheduledPayload &payload) {
    if (payload.transport != Transport::Wifi24 && payload.transport != Transport::Wifi58) {
        return ESP_ERR_INVALID_ARG;
    }
    if (payload.size == 0 || payload.size > payload.bytes.size()) return ESP_ERR_INVALID_ARG;
    const size_t band = band_index(payload.transport);
    if (pending_count_[band] == kQueueCapacity) return ESP_ERR_NO_MEM;
    pending_[band][pending_count_[band]++] = payload;
    increment(stats_[band].expected);
    return ESP_OK;
}

size_t RadioCoordinator::earliest_index(size_t band) const {
    size_t earliest = 0;
    for (size_t i = 1; i < pending_count_[band]; ++i) {
        const auto &candidate = pending_[band][i];
        const auto &selected = pending_[band][earliest];
        if (candidate.deadline_ms < selected.deadline_ms ||
            (candidate.deadline_ms == selected.deadline_ms && candidate.sequence < selected.sequence)) {
            earliest = i;
        }
    }
    return earliest;
}

void RadioCoordinator::erase(size_t band, size_t index) {
    for (size_t i = index + 1; i < pending_count_[band]; ++i) {
        pending_[band][i - 1] = pending_[band][i];
    }
    --pending_count_[band];
}

esp_err_t RadioCoordinator::select_band(size_t band, uint64_t now_ms) {
    if (now_ms < retry_at_ms_) return ESP_ERR_INVALID_STATE;
    const esp_err_t result = backend_.set_channel(band_channel(band));
    if (result != ESP_OK) {
        retry_at_ms_ = now_ms + retry_ms_;
        increment(stats_[band].radio_errors);
        return result;
    }
    active_band_ = band;
    current_channel_ = band_channel(band);
    dwell_started_ms_ = now_ms;
    retry_at_ms_ = 0;
    return ESP_OK;
}

esp_err_t RadioCoordinator::poll(uint64_t now_ms) {
    const bool band0 = pending_count_[0] != 0;
    const bool band1 = pending_count_[1] != 0;
    if (!band0 && !band1) return ESP_OK;
    if (!configured_) {
        const esp_err_t result = backend_.set_country_cn();
        if (result != ESP_OK) return result;
        configured_ = true;
    }

    const size_t other = active_band_ ^ 1U;
    size_t target = active_band_;
    bool switch_needed = current_channel_ == 0;
    if (current_channel_ == 0 && pending_count_[active_band_] == 0) target = other;
    if (current_channel_ != 0 && pending_count_[active_band_] == 0 && pending_count_[other] != 0) {
        target = other;
        switch_needed = true;
    } else if (current_channel_ != 0 && pending_count_[active_band_] != 0 &&
               pending_count_[other] != 0 && now_ms - dwell_started_ms_ >= dwell_ms_) {
        target = other;
        switch_needed = true;
    }
    if (switch_needed) {
        const esp_err_t result = select_band(target, now_ms);
        if (result == ESP_ERR_INVALID_STATE) return ESP_OK;
        if (result != ESP_OK) return result;
    }

    if (pending_count_[active_band_] == 0) return ESP_OK;
    const size_t selected = earliest_index(active_band_);
    const ScheduledPayload &payload = pending_[active_band_][selected];
    const esp_err_t result = backend_.transmit({payload.bytes.data(), payload.size});
    if (result != ESP_OK) {
        increment(stats_[active_band_].radio_errors);
        return result;
    }
    increment(stats_[active_band_].submitted);
    if (now_ms > payload.deadline_ms) increment(stats_[active_band_].late);
    erase(active_band_, selected);
    return ESP_OK;
}

size_t RadioCoordinator::depth(Transport transport) const {
    if (transport != Transport::Wifi24 && transport != Transport::Wifi58) return 0;
    return pending_count_[band_index(transport)];
}

const TransportStats &RadioCoordinator::stats(Transport transport) const {
    if (transport != Transport::Wifi24 && transport != Transport::Wifi58) return invalid_stats_;
    return stats_[band_index(transport)];
}

}  // namespace rid

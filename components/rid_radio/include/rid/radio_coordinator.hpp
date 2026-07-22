#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rid/scheduler.hpp"
#include "rid/wifi_transport.hpp"

namespace rid {

class RadioCoordinator {
public:
    explicit RadioCoordinator(WifiBackend &backend, uint64_t dwell_ms = 100,
                              uint64_t retry_ms = 20);

    esp_err_t submit(const ScheduledPayload &payload);
    esp_err_t poll(uint64_t now_ms);
    size_t depth(Transport transport) const;
    // Wi-Fi has no completion callback in esp_wifi_80211_tx(); submitted counts
    // driver-accepted frames, while completed remains zero for this backend.
    const TransportStats &stats(Transport transport) const;
    uint8_t channel() const { return current_channel_; }

private:
    static constexpr size_t kQueueCapacity = 16;
    static size_t band_index(Transport transport);
    static uint8_t band_channel(size_t band);
    size_t earliest_index(size_t band) const;
    void erase(size_t band, size_t index);
    esp_err_t select_band(size_t band, uint64_t now_ms);

    WifiBackend &backend_;
    uint64_t dwell_ms_;
    uint64_t retry_ms_;
    std::array<std::array<ScheduledPayload, kQueueCapacity>, 2> pending_{};
    std::array<size_t, 2> pending_count_{};
    std::array<TransportStats, 2> stats_{};
    TransportStats invalid_stats_{};
    size_t active_band_{0};
    uint8_t current_channel_{0};
    uint64_t dwell_started_ms_{0};
    uint64_t retry_at_ms_{0};
    bool configured_{false};
};

}  // namespace rid

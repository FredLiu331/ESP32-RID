#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "rid/model.hpp"

namespace rid {

// 153-byte OpenDroneID Message Pack plus the 5-byte BLE Service Data envelope.
constexpr size_t kMaxScheduledPayloadSize = 158;

enum class ScheduleError : uint8_t {
    None,
    ZeroCapacity,
    InvalidTransport,
    InvalidPayload,
    PayloadTooLarge,
};

struct ScheduledPayload {
    uint32_t sequence{0};
    uint16_t aircraft_index{0};
    Transport transport{Transport::Ble4};
    MessageKind kind{MessageKind::Location};
    uint64_t deadline_ms{0};
    uint64_t expires_at_ms{0};
    uint16_t size{0};
    std::array<uint8_t, kMaxScheduledPayloadSize> bytes{};

    static ScheduleError try_copy_from(ScheduledPayload &destination, ByteView payload);
};

struct TransportStats {
    uint64_t expected{0};
    uint64_t submitted{0};
    uint64_t completed{0};
    uint64_t late{0};
    uint64_t dropped{0};
    uint64_t encode_errors{0};
    uint64_t radio_errors{0};
};

uint64_t saturating_increment(uint64_t value);

class Scheduler {
public:
    explicit Scheduler(size_t capacity_per_transport);

    bool valid() const { return capacity_per_transport_ > 0; }
    ScheduleError enqueue(const ScheduledPayload &payload);
    std::optional<ScheduledPayload> pop_next(Transport transport, uint64_t now_ms);
    size_t depth(Transport transport) const;
    const TransportStats &stats(Transport transport) const;

    void record_submitted(Transport transport);
    void record_completed(Transport transport);
    void record_encode_error(Transport transport);
    void record_radio_error(Transport transport);

private:
    struct TransportQueue {
        std::vector<ScheduledPayload> payloads;
        TransportStats stats;
    };

    static constexpr size_t kTransportCount = 4;
    static std::optional<size_t> transport_index(Transport transport);
    static void increment(uint64_t &counter);

    size_t capacity_per_transport_;
    std::array<TransportQueue, kTransportCount> queues_;
    TransportStats invalid_stats_{};
};

}  // namespace rid

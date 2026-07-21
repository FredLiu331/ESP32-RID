#include "rid/scheduler.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace rid {
namespace {

bool older_than(const ScheduledPayload &left, const ScheduledPayload &right) {
    if (left.deadline_ms != right.deadline_ms) return left.deadline_ms < right.deadline_ms;
    return left.sequence < right.sequence;
}

}  // namespace

ScheduleError ScheduledPayload::try_copy_from(ScheduledPayload &destination, ByteView payload) {
    if (payload.size > kMaxScheduledPayloadSize) return ScheduleError::PayloadTooLarge;
    if (payload.size > 0 && payload.data == nullptr) return ScheduleError::InvalidPayload;
    if (payload.size > 0) std::memcpy(destination.bytes.data(), payload.data, payload.size);
    destination.size = static_cast<uint16_t>(payload.size);
    return ScheduleError::None;
}

uint64_t saturating_increment(uint64_t value) {
    return value == std::numeric_limits<uint64_t>::max() ? value : value + 1;
}

Scheduler::Scheduler(size_t capacity_per_transport)
    : capacity_per_transport_(capacity_per_transport) {
    for (auto &queue : queues_) queue.payloads.reserve(capacity_per_transport_);
}

std::optional<size_t> Scheduler::transport_index(Transport transport) {
    const size_t index = static_cast<size_t>(transport);
    if (index >= kTransportCount) return std::nullopt;
    return index;
}

void Scheduler::increment(uint64_t &counter) {
    counter = saturating_increment(counter);
}

ScheduleError Scheduler::enqueue(const ScheduledPayload &payload) {
    if (!valid()) return ScheduleError::ZeroCapacity;
    const auto index = transport_index(payload.transport);
    if (!index) return ScheduleError::InvalidTransport;
    if (payload.size == 0) return ScheduleError::InvalidPayload;
    if (payload.size > kMaxScheduledPayloadSize) return ScheduleError::PayloadTooLarge;

    auto &queue = queues_[*index];
    increment(queue.stats.expected);
    if (queue.payloads.size() == capacity_per_transport_) {
        const auto oldest = std::min_element(queue.payloads.begin(), queue.payloads.end(),
                                             older_than);
        increment(queue.stats.dropped);
        if (older_than(payload, *oldest)) return ScheduleError::None;
        queue.payloads.erase(oldest);
    }
    queue.payloads.push_back(payload);
    return ScheduleError::None;
}

std::optional<ScheduledPayload> Scheduler::pop_next(Transport transport, uint64_t now_ms) {
    const auto index = transport_index(transport);
    if (!index) return std::nullopt;
    auto &queue = queues_[*index];

    for (auto item = queue.payloads.begin(); item != queue.payloads.end();) {
        if (item->expires_at_ms != 0 && now_ms > item->expires_at_ms) {
            item = queue.payloads.erase(item);
            increment(queue.stats.dropped);
        } else {
            ++item;
        }
    }
    if (queue.payloads.empty()) return std::nullopt;

    const auto next =
        std::min_element(queue.payloads.begin(), queue.payloads.end(), older_than);
    ScheduledPayload result = *next;
    queue.payloads.erase(next);
    if (now_ms > result.deadline_ms) increment(queue.stats.late);
    return result;
}

size_t Scheduler::depth(Transport transport) const {
    const auto index = transport_index(transport);
    return index ? queues_[*index].payloads.size() : 0;
}

const TransportStats &Scheduler::stats(Transport transport) const {
    const auto index = transport_index(transport);
    return index ? queues_[*index].stats : invalid_stats_;
}

void Scheduler::record_submitted(Transport transport) {
    const auto index = transport_index(transport);
    if (index) increment(queues_[*index].stats.submitted);
}

void Scheduler::record_completed(Transport transport) {
    const auto index = transport_index(transport);
    if (index) increment(queues_[*index].stats.completed);
}

void Scheduler::record_encode_error(Transport transport) {
    const auto index = transport_index(transport);
    if (index) increment(queues_[*index].stats.encode_errors);
}

void Scheduler::record_radio_error(Transport transport) {
    const auto index = transport_index(transport);
    if (index) increment(queues_[*index].stats.radio_errors);
}

}  // namespace rid

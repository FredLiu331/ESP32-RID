#include <array>
#include <cstdint>
#include <limits>

#include "rid/scheduler.hpp"
#include "unity.h"

namespace {

rid::ScheduledPayload payload(uint32_t sequence, uint64_t deadline_ms,
                              rid::Transport transport = rid::Transport::Ble4,
                              uint64_t expires_at_ms = 0) {
    const std::array<uint8_t, 3> bytes{static_cast<uint8_t>(sequence), 0xaa, 0x55};
    rid::ScheduledPayload result;
    result.sequence = sequence;
    result.transport = transport;
    result.kind = rid::MessageKind::Location;
    result.deadline_ms = deadline_ms;
    result.expires_at_ms = expires_at_ms;
    rid::ScheduledPayload::try_copy_from(result, {bytes.data(), bytes.size()});
    return result;
}

}  // namespace

TEST_CASE("scheduler drops stalest payload on overflow", "[scheduler]") {
    rid::Scheduler scheduler(2);
    TEST_ASSERT_EQUAL(rid::ScheduleError::None, scheduler.enqueue(payload(100, 1000)));
    TEST_ASSERT_EQUAL(rid::ScheduleError::None, scheduler.enqueue(payload(200, 1100)));
    TEST_ASSERT_EQUAL(rid::ScheduleError::None, scheduler.enqueue(payload(300, 1200)));

    TEST_ASSERT_EQUAL_UINT64(3, scheduler.stats(rid::Transport::Ble4).expected);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).dropped);
    const auto next = scheduler.pop_next(rid::Transport::Ble4, 1200);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT32(200, next->sequence);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).late);
}

TEST_CASE("scheduler uses earliest deadline and isolates transport queues", "[scheduler]") {
    rid::Scheduler scheduler(3);
    scheduler.enqueue(payload(1, 300, rid::Transport::Wifi24));
    scheduler.enqueue(payload(2, 100, rid::Transport::Wifi58));
    scheduler.enqueue(payload(3, 200, rid::Transport::Wifi24));

    auto next = scheduler.pop_next(rid::Transport::Wifi24, 200);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT32(3, next->sequence);
    next = scheduler.pop_next(rid::Transport::Wifi58, 200);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT32(2, next->sequence);
    TEST_ASSERT_FALSE(scheduler.pop_next(rid::Transport::Ble5, 200).has_value());
}

TEST_CASE("scheduler removes expired payloads without touching other queues", "[scheduler]") {
    rid::Scheduler scheduler(2);
    scheduler.enqueue(payload(10, 100, rid::Transport::Ble4, 150));
    scheduler.enqueue(payload(11, 120, rid::Transport::Ble4, 0));
    scheduler.enqueue(payload(20, 90, rid::Transport::Ble5, 100));

    const auto next = scheduler.pop_next(rid::Transport::Ble4, 151);
    TEST_ASSERT_TRUE(next.has_value());
    TEST_ASSERT_EQUAL_UINT32(11, next->sequence);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).dropped);
    TEST_ASSERT_EQUAL_UINT64(0, scheduler.stats(rid::Transport::Ble5).dropped);
    TEST_ASSERT_EQUAL_UINT32(1, scheduler.depth(rid::Transport::Ble5));
}

TEST_CASE("scheduler copies bounded payloads and saturates counters", "[scheduler]") {
    static_assert(rid::kMaxScheduledPayloadSize == 512);
    rid::Scheduler scheduler(1);
    TEST_ASSERT_EQUAL(rid::ScheduleError::InvalidPayload,
                      scheduler.enqueue(rid::ScheduledPayload{}));
    auto item = payload(7, 10);
    TEST_ASSERT_EQUAL_UINT32(3, item.size);
    TEST_ASSERT_EQUAL_HEX8(7, item.bytes[0]);
    TEST_ASSERT_EQUAL(rid::ScheduleError::None, scheduler.enqueue(item));

    std::array<uint8_t, rid::kMaxScheduledPayloadSize + 1> too_large{};
    TEST_ASSERT_EQUAL(rid::ScheduleError::PayloadTooLarge,
                      rid::ScheduledPayload::try_copy_from(
                          item, {too_large.data(), too_large.size()}));

    TEST_ASSERT_EQUAL_UINT64(std::numeric_limits<uint64_t>::max(),
                             rid::saturating_increment(
                                 std::numeric_limits<uint64_t>::max()));
    scheduler.record_submitted(rid::Transport::Ble4);
    scheduler.record_completed(rid::Transport::Ble4);
    scheduler.record_encode_error(rid::Transport::Ble4);
    scheduler.record_radio_error(rid::Transport::Ble4);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).submitted);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).completed);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).encode_errors);
    TEST_ASSERT_EQUAL_UINT64(1, scheduler.stats(rid::Transport::Ble4).radio_errors);
}

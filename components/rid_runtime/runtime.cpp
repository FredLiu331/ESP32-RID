#include "rid/runtime.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include "rid/ble_transport.hpp"
#include "rid/gb_encoder.hpp"
#include "rid/odid_encoder.hpp"
#include "rid/trajectory.hpp"
#include "rid/wifi_transport.hpp"

namespace rid {
namespace {

constexpr std::array<MessageKind, kMessageKindCount> kKinds{
    MessageKind::BasicId, MessageKind::Location, MessageKind::Authentication,
    MessageKind::SelfId, MessageKind::System, MessageKind::OperatorId,
};

uint64_t increment(uint64_t value) {
    return value == std::numeric_limits<uint64_t>::max() ? value : value + 1;
}

std::array<uint8_t, 6> address_for(const TestIdentity &identity, uint16_t index) {
    std::array<uint8_t, 6> address{0x02, 0, 0, 0, 0, static_cast<uint8_t>(index)};
    for (size_t i = 0; i < identity.value.size(); ++i) {
        address[1 + i % 5] ^= static_cast<uint8_t>(identity.value[i]);
    }
    address[0] = static_cast<uint8_t>((address[0] | 0x02U) & 0xfeU);
    return address;
}

uint32_t elapsed32(uint64_t now_ms, uint64_t started_ms) {
    return static_cast<uint32_t>(now_ms - started_ms);
}

}  // namespace

Runtime::Runtime(DeviceId device, RuntimeSink &sink) : device_(device), sink_(sink) {}

RuntimeError Runtime::apply(const SystemConfig &config, uint64_t now_ms) {
    const auto expanded = validate_and_expand(config, device_);
    if (!expanded.ok()) return RuntimeError::InvalidConfig;

    auto trajectory = std::make_unique<TrajectoryEngine>(*config.site);
    config_ = config;
    aircraft_ = expanded.value();
    trajectory_ = std::move(trajectory);
    for (size_t i = 0; i < aircraft_.size(); ++i) {
        const auto &aircraft = aircraft_[i];
        const auto &group = config_.groups[aircraft.group_index];
        const bool packed = aircraft.protocol == Protocol::OpenDroneId &&
                            aircraft.transport != Transport::Ble4;
        for (const auto kind : kKinds) {
            const size_t kind_index = static_cast<size_t>(kind);
            const uint64_t period =
                static_cast<uint64_t>(effective_period(config_, group, kind).count());
            const size_t slots = packed ? aircraft_.size() : aircraft_.size() * kMessageKindCount;
            const size_t slot = packed ? i : i * kMessageKindCount + kind_index;
            next_due_[i][kind_index] = now_ms + period * slot / slots;
        }
    }
    stats_ = {};
    started_ms_ = now_ms;
    sequence_ = 0;
    counter_ = 0;
    running_ = true;
    return RuntimeError::None;
}

void Runtime::advance_due(size_t aircraft_index, MessageKind kind, uint64_t now_ms) {
    const auto &aircraft = aircraft_[aircraft_index];
    const auto &group = config_.groups[aircraft.group_index];
    const uint64_t period = static_cast<uint64_t>(effective_period(config_, group, kind).count());
    auto &due = next_due_[aircraft_index][static_cast<size_t>(kind)];
    do {
        due += period;
    } while (due <= now_ms);
}

RuntimeError Runtime::submit(ScheduledPayload &payload) {
    stats_.expected = increment(stats_.expected);
    if (!sink_.submit(payload)) {
        stats_.submit_errors = increment(stats_.submit_errors);
        return RuntimeError::SubmitFailed;
    }
    stats_.submitted = increment(stats_.submitted);
    return RuntimeError::None;
}

RuntimeError Runtime::tick(uint64_t now_ms) {
    if (!running_ || now_ms < started_ms_) return RuntimeError::InvalidConfig;
    RuntimeError result = RuntimeError::None;
    for (size_t i = 0; i < aircraft_.size(); ++i) {
        const auto &aircraft = aircraft_[i];
        const auto &group = config_.groups[aircraft.group_index];
        const FlightState state = trajectory_->sample(group.trajectory, aircraft.instance_index,
                                                      elapsed32(now_ms, started_ms_));
        const bool wifi = aircraft.transport == Transport::Wifi24 ||
                          aircraft.transport == Transport::Wifi58;

        const bool packed_opendroneid =
            aircraft.protocol == Protocol::OpenDroneId && (wifi || aircraft.transport == Transport::Ble5);
        if (packed_opendroneid) {
            bool due = false;
            for (const auto kind : kKinds) {
                if (now_ms >= next_due_[i][static_cast<size_t>(kind)]) {
                    due = true;
                    advance_due(i, kind, now_ms);
                }
            }
            if (!due) continue;
            OdidOptions options;
            options.operator_position = config_.site;
            const auto encoded = OdidEncoder{}.encode_pack(state, aircraft.identity, options);
            ScheduledPayload payload;
            payload.sequence = sequence_++;
            payload.aircraft_index = static_cast<uint16_t>(i);
            payload.transport = aircraft.transport;
            payload.kind = MessageKind::Location;
            payload.deadline_ms = now_ms;
            payload.expires_at_ms = now_ms + static_cast<uint64_t>(config_.default_period.count());
            bool encoded_ok = encoded.ok();
            if (encoded_ok && wifi) {
                WifiFrame frame;
                const auto address = address_for(aircraft.identity, aircraft.instance_index);
                const ByteView ssid{
                    reinterpret_cast<const uint8_t *>(aircraft.identity.value.data()),
                    aircraft.identity.value.size()};
                encoded_ok = build_opendroneid_beacon(
                                 frame, address.data(), ssid, counter_++,
                                 {encoded.value().data(), encoded.value().size()}) ==
                                 WifiFrameError::None &&
                             ScheduledPayload::try_copy_from(payload, frame.view()) ==
                                 ScheduleError::None;
            } else if (encoded_ok) {
                BleAdvertisingData ad;
                encoded_ok = build_opendroneid_service_data(
                                 ad, {encoded.value().data(), encoded.value().size()}, counter_++) ==
                                 BleAdError::None &&
                             ScheduledPayload::try_copy_from(payload, ad.view()) ==
                                 ScheduleError::None;
            }
            if (!encoded_ok) {
                stats_.encode_errors = increment(stats_.encode_errors);
                result = RuntimeError::EncodeFailed;
                continue;
            }
            const auto error = submit(payload);
            if (error != RuntimeError::None) result = error;
            continue;
        }

        for (const auto kind : kKinds) {
            if (now_ms < next_due_[i][static_cast<size_t>(kind)]) continue;
            advance_due(i, kind, now_ms);
            ScheduledPayload payload;
            payload.sequence = sequence_++;
            payload.aircraft_index = static_cast<uint16_t>(i);
            payload.transport = aircraft.transport;
            payload.kind = kind;
            payload.deadline_ms = now_ms;
            payload.expires_at_ms = now_ms +
                static_cast<uint64_t>(effective_period(config_, group, kind).count());

            bool encoded_ok = false;
            if (aircraft.protocol == Protocol::OpenDroneId) {
                OdidOptions options;
                options.operator_position = config_.site;
                const auto encoded = OdidEncoder{}.encode(kind, state, aircraft.identity, options);
                BleAdvertisingData ad;
                encoded_ok = encoded.ok() &&
                    build_opendroneid_service_data(ad,
                        {encoded.value().data(), encoded.value().size()}, counter_++) == BleAdError::None &&
                    ScheduledPayload::try_copy_from(payload, ad.view()) == ScheduleError::None;
            } else {
                GbOptions options;
                options.gcs_position = config_.site;
                const auto encoded = GbEncoder{}.encode(kind, state, aircraft.identity, options);
                WifiFrame frame;
                const auto address = address_for(aircraft.identity, aircraft.instance_index);
                const ByteView ssid{reinterpret_cast<const uint8_t *>(aircraft.identity.value.data()),
                                    aircraft.identity.value.size()};
                encoded_ok = encoded.ok() &&
                    build_gb_beacon(frame, address.data(), ssid, counter_++,
                                    {encoded.value().data(), encoded.value().size()}) == WifiFrameError::None &&
                    ScheduledPayload::try_copy_from(payload, frame.view()) == ScheduleError::None;
            }
            if (!encoded_ok) {
                stats_.encode_errors = increment(stats_.encode_errors);
                result = RuntimeError::EncodeFailed;
                continue;
            }
            const auto error = submit(payload);
            if (error != RuntimeError::None) result = error;
        }
    }
    return result;
}

}  // namespace rid

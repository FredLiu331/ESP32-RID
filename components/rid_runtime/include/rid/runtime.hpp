#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "rid/config.hpp"
#include "rid/scheduler.hpp"

namespace rid {

class RuntimeSink {
public:
    virtual bool submit(const ScheduledPayload &payload) = 0;
    virtual ~RuntimeSink() = default;
};

enum class RuntimeError : uint8_t { None, InvalidConfig, EncodeFailed, SubmitFailed };

struct RuntimeStats {
    uint64_t expected{0};
    uint64_t submitted{0};
    uint64_t encode_errors{0};
    uint64_t submit_errors{0};
};

class Runtime {
public:
    Runtime(DeviceId device, RuntimeSink &sink);

    RuntimeError apply(const SystemConfig &config, uint64_t now_ms);
    RuntimeError tick(uint64_t now_ms);
    bool running() const { return running_; }
    size_t aircraft_count() const { return aircraft_.size(); }
    const SystemConfig &config() const { return config_; }
    const RuntimeStats &stats() const { return stats_; }

private:
    RuntimeError submit(ScheduledPayload &payload);
    void advance_due(size_t aircraft_index, MessageKind kind, uint64_t now_ms);

    DeviceId device_;
    RuntimeSink &sink_;
    SystemConfig config_;
    std::vector<AircraftConfig> aircraft_;
    std::unique_ptr<class TrajectoryEngine> trajectory_;
    std::array<std::array<uint64_t, kMessageKindCount>, kMaxAircraft> next_due_{};
    RuntimeStats stats_{};
    uint64_t started_ms_{0};
    uint32_t sequence_{0};
    uint8_t counter_{0};
    bool running_{false};
};

}  // namespace rid

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>

#include "rid/config.hpp"

namespace rid {

constexpr size_t kShellMaxLineSize = 255;
enum class ShellLineEvent : uint8_t { None, Ready, TooLong };

class ShellLineBuffer {
public:
    ShellLineEvent push(char character);
    std::string take();

private:
    std::array<char, kShellMaxLineSize> bytes_{};
    size_t size_{0};
    bool overflow_{false};
    bool last_was_cr_{false};
};

class Shell {
public:
    using ConfigCallback = std::function<bool(const SystemConfig &, uint32_t)>;

    Shell(SystemConfig staged, DeviceId device,
          std::optional<SystemConfig> applied = std::nullopt, uint32_t generation = 0);

    std::string handle(const std::string &line);
    const SystemConfig &staged() const { return staged_; }
    const std::optional<SystemConfig> &applied() const { return applied_; }
    uint32_t generation() const { return generation_; }
    void set_apply_callback(ConfigCallback callback) { apply_callback_ = std::move(callback); }
    void set_save_callback(ConfigCallback callback) { save_callback_ = std::move(callback); }

private:
    SystemConfig staged_;
    DeviceId device_;
    std::optional<SystemConfig> applied_;
    uint32_t generation_{0};
    ConfigCallback apply_callback_;
    ConfigCallback save_callback_;
};

}  // namespace rid

#pragma once

#include <functional>
#include <string>

#include "rid/config.hpp"

namespace rid {

class Shell {
public:
    using ConfigCallback = std::function<bool(const SystemConfig &)>;

    Shell(SystemConfig staged, DeviceId device);

    std::string handle(const std::string &line);
    const SystemConfig &staged() const { return staged_; }
    void set_apply_callback(ConfigCallback callback) { apply_callback_ = std::move(callback); }
    void set_save_callback(ConfigCallback callback) { save_callback_ = std::move(callback); }

private:
    SystemConfig staged_;
    DeviceId device_;
    ConfigCallback apply_callback_;
    ConfigCallback save_callback_;
};

}  // namespace rid

#include "rid/shell.hpp"

#include <charconv>
#include <sstream>
#include <string_view>

namespace rid {
namespace {

bool number(std::string_view text, uint16_t &value) {
    unsigned parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed > 65535U) {
        return false;
    }
    value = static_cast<uint16_t>(parsed);
    return true;
}

bool protocol(std::string_view text, Protocol &value) {
    if (text == "gb") { value = Protocol::Gb46750; return true; }
    if (text == "odid") { value = Protocol::OpenDroneId; return true; }
    return false;
}

bool transport(std::string_view text, Transport &value) {
    if (text == "ble4") { value = Transport::Ble4; return true; }
    if (text == "ble5") { value = Transport::Ble5; return true; }
    if (text == "wifi24") { value = Transport::Wifi24; return true; }
    if (text == "wifi58") { value = Transport::Wifi58; return true; }
    return false;
}

std::string error(std::string_view code) { return "ERR " + std::string(code) + "\n"; }

}  // namespace

Shell::Shell(SystemConfig staged, DeviceId device) : staged_(std::move(staged)), device_(device) {}

std::string Shell::handle(const std::string &line) {
    std::istringstream input(line);
    std::string root;
    std::string command;
    input >> root;
    if (root.empty() || root == "help") return "OK help config group add|list|clear|validate|apply|save status\n";
    if (root == "status") return "OK status groups=" + std::to_string(staged_.groups.size()) + "\n";
    if (root != "config") return error("UNKNOWN_COMMAND");
    input >> command;
    if (command == "group") {
        std::string action;
        input >> action;
        if (action == "list") {
            std::string result = "OK groups=" + std::to_string(staged_.groups.size());
            for (const auto &group : staged_.groups) {
                result += " " + group.name + ":" + std::to_string(group.count);
            }
            return result + "\n";
        }
        if (action == "clear") { staged_.groups.clear(); return "OK group_clear\n"; }
        if (action != "add") return error("UNKNOWN_COMMAND");
        std::string name, count_text, protocol_text, transport_text;
        input >> name >> count_text >> protocol_text >> transport_text;
        uint16_t count = 0;
        Protocol rid_protocol{};
        Transport radio_transport{};
        if (name.empty() || !number(count_text, count) || count == 0 ||
            !protocol(protocol_text, rid_protocol) || !transport(transport_text, radio_transport)) {
            return error("INVALID_ARGUMENT");
        }
        staged_.groups.emplace_back(name, count, rid_protocol, radio_transport);
        return "OK group_add\n";
    }
    if (command == "validate") {
        const auto result = rid::validate(staged_);
        return result.ok() ? "OK validate\n" : error("INVALID_CONFIG");
    }
    if (command == "apply" || command == "save") {
        const auto &callback = command == "apply" ? apply_callback_ : save_callback_;
        if (!callback || !callback(staged_)) return error("OPERATION_FAILED");
        return "OK " + command + "\n";
    }
    return error("UNKNOWN_COMMAND");
}

}  // namespace rid

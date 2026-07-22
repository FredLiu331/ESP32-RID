#include "rid/shell.hpp"

#include <charconv>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

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
    if (text == "wifi58" || text == "wifi5") { value = Transport::Wifi58; return true; }
    return false;
}

std::string error(std::string_view code) { return "ERR " + std::string(code) + "\n"; }

std::vector<std::string> words(const std::string &line) {
    std::istringstream input(line);
    std::vector<std::string> result;
    for (std::string value; input >> value;) result.push_back(std::move(value));
    return result;
}

bool decimal(std::string_view text, double &value) {
    std::string copy{text};
    char *end = nullptr;
    value = std::strtod(copy.c_str(), &end);
    return end != copy.c_str() && end == copy.c_str() + copy.size();
}

size_t aircraft_count(const SystemConfig &config) {
    size_t result = 0;
    for (const auto &group : config.groups) result += group.count;
    return result;
}

}  // namespace

ShellLineEvent ShellLineBuffer::push(char character) {
    if (character == '\n' && last_was_cr_) {
        last_was_cr_ = false;
        return ShellLineEvent::None;
    }
    if (character == '\r' || character == '\n') {
        last_was_cr_ = character == '\r';
        if (overflow_) {
            overflow_ = false;
            size_ = 0;
            return ShellLineEvent::TooLong;
        }
        return size_ == 0 ? ShellLineEvent::None : ShellLineEvent::Ready;
    }
    last_was_cr_ = false;
    if (overflow_) return ShellLineEvent::None;
    if (size_ == bytes_.size()) {
        overflow_ = true;
        return ShellLineEvent::None;
    }
    bytes_[size_++] = character;
    return ShellLineEvent::None;
}

std::string ShellLineBuffer::take() {
    std::string result{bytes_.data(), size_};
    size_ = 0;
    return result;
}

Shell::Shell(SystemConfig staged, DeviceId device, std::optional<SystemConfig> applied,
             uint32_t generation)
    : staged_(std::move(staged)),
      device_(device),
      applied_(std::move(applied)),
      generation_(generation) {}

std::string Shell::handle(const std::string &line) {
    auto tokens = words(line);
    if (tokens.empty() || tokens[0] == "help") {
        return "OK help site|group|config|fleet|status\n";
    }
    if (tokens[0] == "status") {
        return "OK status generation=" + std::to_string(generation_) +
               " staged=" + std::to_string(aircraft_count(staged_)) +
               " applied=" + std::to_string(applied_.has_value() ? aircraft_count(*applied_) : 0) +
               "\n";
    }
    if (tokens[0] == "site") {
        if (tokens.size() != 6 || tokens[1] != "set" || tokens[2] != "latitude" ||
            tokens[4] != "longitude") {
            return error("INVALID_ARGUMENT");
        }
        GeoPoint site{};
        if (!decimal(tokens[3], site.latitude_deg) ||
            !decimal(tokens[5], site.longitude_deg)) {
            return error("INVALID_ARGUMENT");
        }
        staged_.site = site;
        return "OK site staged\n";
    }

    size_t group_offset = 0;
    if (tokens[0] == "group") {
        group_offset = 1;
    } else if (tokens.size() >= 2 && tokens[0] == "config" && tokens[1] == "group") {
        group_offset = 2;
    }
    if (group_offset != 0) {
        if (tokens.size() <= group_offset) return error("UNKNOWN_COMMAND");
        const std::string &action = tokens[group_offset];
        if (action == "list") {
            std::string result = "OK groups=" + std::to_string(staged_.groups.size());
            for (const auto &group : staged_.groups) {
                result += " " + group.name + ":" + std::to_string(group.count);
            }
            return result + "\n";
        }
        if (action == "clear") {
            staged_.groups.clear();
            return "OK group_clear\n";
        }
        if (action == "delete" && tokens.size() == group_offset + 2) {
            const std::string &name = tokens[group_offset + 1];
            for (auto current = staged_.groups.begin(); current != staged_.groups.end(); ++current) {
                if (current->name == name) {
                    staged_.groups.erase(current);
                    return "OK group_delete\n";
                }
            }
            return error("GROUP_NOT_FOUND");
        }
        if (action != "add") return error("UNKNOWN_COMMAND");
        const size_t remaining = tokens.size() - group_offset;
        std::string name, count_text, protocol_text, transport_text;
        if (remaining == 5) {
            name = tokens[group_offset + 1];
            count_text = tokens[group_offset + 2];
            protocol_text = tokens[group_offset + 3];
            transport_text = tokens[group_offset + 4];
        } else if (remaining == 8 && tokens[group_offset + 2] == "count" &&
                   tokens[group_offset + 4] == "protocol" &&
                   tokens[group_offset + 6] == "transport") {
            name = tokens[group_offset + 1];
            count_text = tokens[group_offset + 3];
            protocol_text = tokens[group_offset + 5];
            transport_text = tokens[group_offset + 7];
        } else {
            return error("INVALID_ARGUMENT");
        }
        uint16_t count = 0;
        Protocol rid_protocol{};
        Transport radio_transport{};
        if (name.empty() || !number(count_text, count) || count == 0 ||
            !protocol(protocol_text, rid_protocol) || !transport(transport_text, radio_transport)) {
            return error("INVALID_ARGUMENT");
        }
        staged_.groups.emplace_back(name, count, rid_protocol, radio_transport);
        return "OK group " + name + " staged\n";
    }

    if (tokens[0] == "fleet" && tokens.size() == 2 && tokens[1] == "list") {
        const auto expanded = validate_and_expand(staged_, device_);
        if (!expanded.ok()) return error("INVALID_CONFIG");
        std::string result = "OK fleet=" + std::to_string(expanded.value().size());
        for (const auto &aircraft : expanded.value()) result += " " + aircraft.identity.value;
        return result + "\n";
    }

    if (tokens[0] != "config" || tokens.size() != 2) return error("UNKNOWN_COMMAND");
    const std::string &command = tokens[1];
    if (command == "validate" || command == "check") {
        const auto expanded = validate_and_expand(staged_, device_);
        return expanded.ok() ? "OK aircraft=" + std::to_string(expanded.value().size()) + "\n"
                             : error("INVALID_CONFIG");
    }
    if (command == "apply") {
        const auto expanded = validate_and_expand(staged_, device_);
        if (!expanded.ok()) return error("INVALID_CONFIG");
        const uint32_t next_generation = generation_ + 1U;
        if (!apply_callback_ || !apply_callback_(staged_, next_generation)) {
            return error("OPERATION_FAILED");
        }
        applied_ = staged_;
        generation_ = next_generation;
        return "OK generation=" + std::to_string(generation_) +
               " running=" + std::to_string(expanded.value().size()) + "\n";
    }
    if (command == "save") {
        if (!applied_.has_value()) return error("NO_APPLIED_CONFIG");
        if (!save_callback_ || !save_callback_(*applied_, generation_)) {
            return error("OPERATION_FAILED");
        }
        return "OK saved generation=" + std::to_string(generation_) + "\n";
    }
    if (command == "rollback") {
        if (!applied_.has_value()) return error("NO_APPLIED_CONFIG");
        staged_ = *applied_;
        return "OK rollback running=" + std::to_string(aircraft_count(staged_)) + "\n";
    }
    return error("UNKNOWN_COMMAND");
}

}  // namespace rid

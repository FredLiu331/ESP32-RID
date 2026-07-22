#include <string>

#include "rid/shell.hpp"
#include "unity.h"

namespace {
rid::SystemConfig base_config() {
    rid::SystemConfig config = rid::default_config();
    config.site = rid::GeoPoint{31.23, 121.47};
    return config;
}
}

TEST_CASE("shell group commands edit staged configuration", "[shell]") {
    rid::Shell shell(base_config(), rid::DeviceId{{1, 2, 3, 4, 5, 6}});
    TEST_ASSERT_TRUE(shell.handle(
        "group add fleet count 2 protocol odid transport ble5").rfind("OK ", 0) == 0);
    TEST_ASSERT_EQUAL_UINT32(1, shell.staged().groups.size());
    TEST_ASSERT_EQUAL_STRING("fleet", shell.staged().groups[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, shell.staged().groups[0].count);
    TEST_ASSERT_TRUE(shell.handle("config check").rfind("OK ", 0) == 0);
}

TEST_CASE("shell rejects invalid commands and invokes apply/save callbacks", "[shell]") {
    rid::Shell shell(base_config(), rid::DeviceId{{1, 2, 3, 4, 5, 6}});
    bool applied = false;
    bool saved = false;
    shell.set_apply_callback([&](const rid::SystemConfig &, uint32_t generation) {
        applied = generation == 1;
        return true;
    });
    shell.set_save_callback([&](const rid::SystemConfig &, uint32_t generation) {
        saved = generation == 1;
        return true;
    });
    TEST_ASSERT_TRUE(shell.handle("config group add x 1 nope ble4").rfind("ERR ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("group add x count 1 protocol odid transport ble4").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("config apply").find("OK generation=1 running=1") == 0);
    TEST_ASSERT_TRUE(shell.handle("config save").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_TRUE(saved);
}

TEST_CASE("shell rollback restores last applied configuration", "[shell]") {
    rid::Shell shell(base_config(), rid::DeviceId{{1, 2, 3, 4, 5, 6}});
    shell.set_apply_callback([](const rid::SystemConfig &, uint32_t) { return true; });
    TEST_ASSERT_TRUE(shell.handle("group add fleet count 2 protocol odid transport ble5").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("config apply").find("OK generation=1") == 0);
    TEST_ASSERT_TRUE(shell.handle("group clear").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("config rollback").find("OK rollback running=2") == 0);
    TEST_ASSERT_EQUAL_UINT32(1, shell.staged().groups.size());
}

TEST_CASE("shell line buffer waits for a complete UART command", "[shell]") {
    rid::ShellLineBuffer input;
    const std::string command = "config check";
    for (const char character : command) {
        TEST_ASSERT_EQUAL(rid::ShellLineEvent::None, input.push(character));
    }
    TEST_ASSERT_EQUAL(rid::ShellLineEvent::Ready, input.push('\r'));
    TEST_ASSERT_EQUAL_STRING(command.c_str(), input.take().c_str());
    TEST_ASSERT_EQUAL(rid::ShellLineEvent::None, input.push('\n'));

    for (size_t index = 0; index < rid::kShellMaxLineSize + 1; ++index) {
        TEST_ASSERT_EQUAL(rid::ShellLineEvent::None, input.push('x'));
    }
    TEST_ASSERT_EQUAL(rid::ShellLineEvent::TooLong, input.push('\n'));
}

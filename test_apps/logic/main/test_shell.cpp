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
    TEST_ASSERT_TRUE(shell.handle("config group add fleet 2 odid ble5").rfind("OK ", 0) == 0);
    TEST_ASSERT_EQUAL_UINT32(1, shell.staged().groups.size());
    TEST_ASSERT_EQUAL_STRING("fleet", shell.staged().groups[0].name.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, shell.staged().groups[0].count);
    TEST_ASSERT_TRUE(shell.handle("config validate").rfind("OK ", 0) == 0);
}

TEST_CASE("shell rejects invalid commands and invokes apply/save callbacks", "[shell]") {
    rid::Shell shell(base_config(), rid::DeviceId{{1, 2, 3, 4, 5, 6}});
    bool applied = false;
    bool saved = false;
    shell.set_apply_callback([&](const rid::SystemConfig &) { applied = true; return true; });
    shell.set_save_callback([&](const rid::SystemConfig &) { saved = true; return true; });
    TEST_ASSERT_TRUE(shell.handle("config group add x 1 nope ble4").rfind("ERR ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("config apply").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(shell.handle("config save").rfind("OK ", 0) == 0);
    TEST_ASSERT_TRUE(applied);
    TEST_ASSERT_TRUE(saved);
}

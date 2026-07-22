# 基于 ESP32-C5 的 RID 信号发生器实施计划

> **供智能体执行时使用：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`，严格按任务顺序实施，并用复选框跟踪步骤。

**目标：** 在微雪 ESP32-C5-WIFI6-KIT-N32R8 和 ESP-IDF v5.5.4 上实现可配置模拟 1～50 架无人机的 GB 46750-2025/OpenDroneID RID 信号发生器。

**架构：** 系统以不可变配置快照驱动机群展开、轨迹计算和统一飞行状态，再经独立协议编码器进入截止时间调度器。BLE4、BLE5 和 Wi-Fi Beacon 由射频协调器统一管理，单颗 ESP32-C5 在信道 6 与 149 间切换，实现逻辑双频并行；NAN 不属于产品 RID 承载。

**技术栈：** ESP-IDF v5.5.4、ESP32-C5、C++17、FreeRTOS、NVS、NimBLE、ESP-IDF Wi-Fi API、Unity、pytest-embedded、`opendroneid-core-c`。

---

## 一、文件结构

计划创建以下结构，各文件只承担一项明确职责：

```text
CMakeLists.txt                         ESP-IDF 工程入口
sdkconfig.defaults                     ESP32-C5、控制台、NVS、NimBLE 默认配置
partitions.csv                         应用与 NVS 分区
dependencies.lock                      第三方依赖提交哈希和许可证
main/CMakeLists.txt                    主程序组件定义
main/app_main.cpp                      启动、依赖装配、任务生命周期

components/rid_model/include/rid/model.hpp
components/rid_model/model.cpp         统一类型、单位和字段校验
components/rid_identity/identity.cpp   确定性测试 ID 派生
components/rid_config/config.cpp       配置模型、校验和机群展开
components/rid_config/nvs_store.cpp    配置快照原子持久化
components/rid_trajectory/trajectory.cpp 轨迹模型和坐标转换
components/rid_odid/odid_encoder.cpp   OpenDroneID 编码适配
components/rid_gb/gb_encoder.cpp       GB 46750 编码
components/rid_scheduler/scheduler.cpp 截止时间与队列过载策略
components/rid_radio/ble_transport.cpp BLE4/BLE5 复用
components/rid_radio/wifi_transport.cpp Beacon 原始帧发送
components/rid_radio/radio_coordinator.cpp 共存和信道切换
components/rid_runtime/runtime.cpp      配置快照到运行任务的协调
components/rid_shell/shell.cpp          交互式 Shell 命令

test_apps/logic/                        可在目标板运行的 Unity 逻辑测试应用
test_apps/radio_probe/                  射频可行性验证应用
test_apps/integration/                  板上集成与 pytest-embedded 测试应用
pytest_rid/                             串口驱动的集成/验收测试
docs/protocol/                          协议映射和兼容矩阵
docs/verification/                     射频探针及验收记录模板
```

每个组件均包含自己的 `CMakeLists.txt` 和 `include/rid/*.hpp` 公共接口。组件内部头文件放在 `private_include/`，不得被其他组件直接引用。

## 二、实施前置条件

- 执行环境已安装 ESP-IDF v5.5.4，并能运行 `idf.py --version`。
- 已取得合法可用的 GB 46750-2025 正文。标准正文不提交到仓库，只提交字段映射、来源条款编号和自行构造的测试向量。
- 开发板通过 USB 连接，串口可由 `idf.py monitor` 打开。
- 所有空口测试均在经过授权且边界受控的试验场完成。

### 任务 1：建立最小 ESP-IDF 工程与测试骨架

**文件：**
- 创建：`CMakeLists.txt`
- 创建：`sdkconfig.defaults`
- 创建：`partitions.csv`
- 创建：`main/CMakeLists.txt`
- 创建：`main/app_main.cpp`
- 创建：`test_apps/logic/CMakeLists.txt`
- 创建：`test_apps/logic/main/CMakeLists.txt`
- 创建：`test_apps/logic/main/test_main.cpp`
- 创建：`test_apps/logic/main/test_smoke.cpp`

- [ ] **步骤 1：确认工具链版本**

运行：

```bash
idf.py --version
```

预期：输出包含 `ESP-IDF v5.5.4`。版本不一致时停止，不生成 `sdkconfig`。

- [ ] **步骤 2：编写最小工程文件**

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_c5_rid_generator)
```

`main/app_main.cpp`：

```cpp
#include "esp_log.h"

extern "C" void app_main(void) {
    ESP_LOGI("rid", "RID generator boot");
}
```

`main/CMakeLists.txt`：

```cmake
idf_component_register(SRCS "app_main.cpp" INCLUDE_DIRS ".")
```

`sdkconfig.defaults`：

```text
CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_INIT=y
# CONFIG_COMPILER_CXX_EXCEPTIONS is not set
```

`partitions.csv`：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 4M,
```

`test_apps/logic/main/test_main.cpp`：

```cpp
#include "unity.h"

extern "C" void app_main(void) {
    unity_run_menu();
}
```

`test_apps/logic/main/CMakeLists.txt`：

```cmake
file(GLOB TEST_SOURCES "test_*.cpp")
idf_component_register(SRCS ${TEST_SOURCES} INCLUDE_DIRS "." REQUIRES unity)
```

- [ ] **步骤 3：构建主工程**

运行：

```bash
idf.py set-target esp32c5
idf.py build
```

预期：构建成功并生成 `build/esp32_c5_rid_generator.bin`。

- [ ] **步骤 4：加入最小 Unity 冒烟测试**

`test_smoke.cpp`：

```cpp
#include "unity.h"

TEST_CASE("logic test application boots", "[smoke]") {
    TEST_ASSERT_TRUE(true);
}
```

运行测试应用构建，预期编译成功：

```bash
idf.py -C test_apps/logic set-target esp32c5
idf.py -C test_apps/logic build
```

- [ ] **步骤 5：提交工程骨架**

```bash
git add CMakeLists.txt sdkconfig.defaults partitions.csv main test_apps/logic
git commit -m "build: 初始化 ESP32-C5 RID 工程"
```

### 任务 2：完成射频可行性验证关卡

**文件：**
- 创建：`test_apps/radio_probe/CMakeLists.txt`
- 创建：`test_apps/radio_probe/main/CMakeLists.txt`
- 创建：`test_apps/radio_probe/main/radio_probe.cpp`
- 创建：`pytest_rid/test_radio_probe.py`
- 创建：`docs/verification/radio-feasibility.md`

- [ ] **步骤 1：先写串口验收测试**

`pytest_rid/test_radio_probe.py`：

```python
import pytest

@pytest.mark.esp32c5
def test_all_public_radio_gates(dut):
    dut.expect_exact("PROBE wifi_beacon_ch6 PASS")
    dut.expect_exact("PROBE wifi_beacon_ch149 PASS")
    dut.expect_exact("PROBE wifi_nan_ch6 PASS")
    dut.expect_exact("PROBE wifi_nan_ch149 PASS")
    dut.expect_exact("PROBE ble4 PASS")
    dut.expect_exact("PROBE ble5 PASS")
    dut.expect_exact("PROBE coexist_hop PASS")
```

- [ ] **步骤 2：运行测试并确认失败**

运行：

```bash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5
```

预期：因为探针应用尚不存在或没有输出关卡结果而失败。

- [ ] **步骤 3：实现最小探针**

探针使用 `esp_wifi_80211_tx()` 分别发送合法最小 Beacon；使用 NimBLE GAP API 启动 Legacy 与 Extended Advertising。每个 API 调用必须检查 `esp_err_t`，只有提交成功且发送完成回调达到设定次数才输出 `PASS`。

核心结果类型：

```cpp
struct ProbeResult {
    const char *name;
    bool api_supported;
    uint32_t submitted;
    uint32_t completed;
    esp_err_t last_error;
};
```

共存关卡在 BLE 持续广播期间循环切换信道 `6 -> 149 -> 6` 共 100 次，要求 BLE 完成计数持续增长，且 Wi-Fi 信道切换无连续错误。

- [ ] **步骤 4：构建、烧录并执行探针**

运行：

```bash
idf.py -C test_apps/radio_probe set-target esp32c5
idf.py -C test_apps/radio_probe build flash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5
```

预期：七项关卡全部输出 `PASS`。任何一项失败时停止后续任务，将实测错误码和支持矩阵写入验证文档，并修订设计规格。

- [ ] **步骤 5：记录可复现证据并提交**

`docs/verification/radio-feasibility.md` 必须记录开发板版本、ESP-IDF 提交、`sdkconfig` 哈希、天线、国家码、每项关卡的完成计数和错误码。

```bash
git add test_apps/radio_probe pytest_rid/test_radio_probe.py docs/verification/radio-feasibility.md
git commit -m "test: 验证 ESP32-C5 RID 射频能力"
```

### 任务 3：定义统一飞行状态与测试 ID

**文件：**
- 创建：`components/rid_model/CMakeLists.txt`
- 创建：`components/rid_model/include/rid/model.hpp`
- 创建：`components/rid_model/model.cpp`
- 创建：`components/rid_identity/CMakeLists.txt`
- 创建：`components/rid_identity/include/rid/identity.hpp`
- 创建：`components/rid_identity/identity.cpp`
- 创建：`test_apps/logic/main/test_model_identity.cpp`

- [ ] **步骤 1：编写失败测试**

```cpp
TEST_CASE("test IDs are stable and unique", "[identity]") {
    const rid::DeviceId device{{0x10, 0x20, 0x30, 0x40, 0x50, 0x60}};
    const auto a = rid::derive_test_id(device, "odid_ble", 0, rid::Protocol::OpenDroneId);
    const auto b = rid::derive_test_id(device, "odid_ble", 1, rid::Protocol::OpenDroneId);
    TEST_ASSERT_EQUAL_STRING(a.value.c_str(), rid::derive_test_id(device, "odid_ble", 0, rid::Protocol::OpenDroneId).value.c_str());
    TEST_ASSERT_NOT_EQUAL(0, a.value.compare(b.value));
    TEST_ASSERT_TRUE(a.value.rfind("TEST", 0) == 0);
}
```

同时测试纬度超出 `[-90, 90]`、经度超出 `[-180, 180]`、负周期和非有限浮点数均被拒绝。

- [ ] **步骤 2：运行逻辑测试并确认失败**

```bash
idf.py -C test_apps/logic build flash monitor
```

预期：因 `rid/model.hpp` 和 `derive_test_id()` 不存在而编译失败。

- [ ] **步骤 3：实现最小公共类型**

`model.hpp` 定义：

```cpp
namespace rid {
enum class Protocol : uint8_t { Gb46750, OpenDroneId };
enum class Transport : uint8_t { Ble4, Ble5, Wifi24, Wifi58 };
enum class WifiMode : uint8_t { Beacon, Nan }; // Nan 保留枚举值但当前配置禁止
enum class MessageKind : uint8_t {
    BasicId,
    Location,
    Authentication,
    SelfId,
    System,
    OperatorId,
};

struct DeviceId { std::array<uint8_t, 6> bytes; };
struct TestIdentity { std::string value; };
struct ByteView { const uint8_t *data; size_t size; };

struct FlightState {
    double latitude_deg;
    double longitude_deg;
    float altitude_msl_m;
    float height_agl_m;
    float horizontal_speed_mps;
    float vertical_speed_mps;
    float heading_deg;
    bool airborne;
    uint32_t relative_time_ms;
};

bool valid(const FlightState &state);
}
```

ID 派生使用设备 MAC、UTF-8 组名和大端序组内索引计算 SHA-256，再编码为协议允许长度的 `TEST` 前缀字符串。不得把真实 MAC 直接暴露在广播 ID 中。

- [ ] **步骤 4：运行测试并确认通过**

运行逻辑测试，预期 `[model]` 和 `[identity]` 全部通过。

- [ ] **步骤 5：提交公共模型**

```bash
git add components/rid_model components/rid_identity test_apps/logic
git commit -m "feat: 添加统一飞行状态与测试身份"
```

### 任务 4：实现配置组、校验与机群展开

**文件：**
- 创建：`components/rid_config/CMakeLists.txt`
- 创建：`components/rid_config/include/rid/config.hpp`
- 创建：`components/rid_config/config.cpp`
- 创建：`test_apps/logic/main/test_config.cpp`

- [ ] **步骤 1：编写配置展开失败测试**

```cpp
TEST_CASE("groups expand to at most fifty aircraft", "[config]") {
    rid::SystemConfig cfg = rid::default_config();
    cfg.site = {31.2304, 121.4737};
    cfg.groups = {
        rid::GroupConfig{"ble", 10, rid::Protocol::OpenDroneId, rid::Transport::Ble5},
        rid::GroupConfig{"wifi", 40, rid::Protocol::Gb46750, rid::Transport::Wifi58},
    };
    auto fleet = rid::validate_and_expand(cfg, rid::DeviceId{{1,2,3,4,5,6}});
    TEST_ASSERT_TRUE(fleet.ok());
    TEST_ASSERT_EQUAL_UINT32(50, fleet.value().size());
    cfg.groups[1].count = 41;
    TEST_ASSERT_EQUAL(rid::ConfigError::TooManyAircraft,
                      rid::validate_and_expand(cfg, rid::DeviceId{{1,2,3,4,5,6}}).error());
}
```

增加测试覆盖重复组名、缺失中心坐标、100 ms 以下周期、60 s 以上周期、无效协议/承载组合和实例 ID 唯一性。

- [ ] **步骤 2：运行并确认测试失败**

预期：配置类型和 `validate_and_expand()` 尚不存在。

- [ ] **步骤 3：实现配置快照与分层周期**

公共接口必须包含：

```cpp
ValidationResult validate(const SystemConfig &config);
ExpandResult validate_and_expand(const SystemConfig &config, const DeviceId &device);
std::chrono::milliseconds effective_period(
    const SystemConfig &system, const GroupConfig &group, MessageKind kind);
```

周期优先级固定为“单消息覆盖值 > 组级默认值 > 全局默认值”。展开结果使用固定上限容器或启动时一次性分配，运行期间不得继续增长。

- [ ] **步骤 4：运行测试并提交**

```bash
idf.py -C test_apps/logic build flash monitor
git add components/rid_config test_apps/logic
git commit -m "feat: 添加配置组与机群展开"
```

### 任务 5：实现内置轨迹引擎

**文件：**
- 创建：`components/rid_trajectory/CMakeLists.txt`
- 创建：`components/rid_trajectory/include/rid/trajectory.hpp`
- 创建：`components/rid_trajectory/trajectory.cpp`
- 创建：`test_apps/logic/main/test_trajectory.cpp`

- [ ] **步骤 1：为六种轨迹编写失败测试**

圆周测试示例：

```cpp
TEST_CASE("circle returns to start after one period", "[trajectory]") {
    constexpr double kPi = 3.14159265358979323846;
    rid::TrajectoryConfig cfg = rid::circle_trajectory(50.0f, 5.0f, 80.0f);
    rid::TrajectoryEngine engine({31.2304, 121.4737});
    const auto start = engine.sample(cfg, 0, 0);
    const uint32_t period_ms = static_cast<uint32_t>((2.0 * kPi * 50.0 / 5.0) * 1000.0);
    const auto end = engine.sample(cfg, 0, period_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, start.height_agl_m, end.height_agl_m);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, start.latitude_deg, end.latitude_deg);
    TEST_ASSERT_DOUBLE_WITHIN(1e-5, start.longitude_deg, end.longitude_deg);
}
```

分别验证悬停不移动、直线折返、矩形拐点、航点循环、起降状态变化，以及同组不同索引产生确定性相位差。
同时验证 `elapsed_ms=0` 时相对消息时间归零，32 位毫秒计数回绕后轨迹相位仍连续；运行时重启不恢复上一次轨迹位置。

- [ ] **步骤 2：实现 ENU 到 WGS-84 小范围转换与轨迹策略**

接口：

```cpp
class TrajectoryEngine {
public:
    explicit TrajectoryEngine(GeoPoint site_center);
    FlightState sample(const TrajectoryConfig &config,
                       uint16_t instance_index,
                       uint32_t elapsed_ms) const;
};
```

坐标转换使用 WGS-84 曲率半径，禁止用固定“每度 111 km”同时处理经纬度。所有航向归一化到 `[0, 360)`。

- [ ] **步骤 3：运行全部轨迹测试并提交**

```bash
idf.py -C test_apps/logic build flash monitor
git add components/rid_trajectory test_apps/logic
git commit -m "feat: 添加合成飞行轨迹引擎"
```

### 任务 6：集成 OpenDroneID 编码器

**文件：**
- 创建：`dependencies.lock`
- 创建：`components/rid_odid/CMakeLists.txt`
- 创建：`components/rid_odid/include/rid/odid_encoder.hpp`
- 创建：`components/rid_odid/odid_encoder.cpp`
- 创建：`test_apps/logic/main/test_odid_encoder.cpp`
- 创建：`docs/protocol/opendroneid-mapping.md`

- [ ] **步骤 1：锁定依赖并记录许可证**

将 `opendroneid-core-c` 作为受控依赖引入，`dependencies.lock` 记录仓库 URL、Git 提交哈希、许可证文件哈希和 ASTM F3411-22a 基线。禁止跟踪浮动的 `master`。

- [ ] **步骤 2：先写黄金向量测试**

测试固定 `FlightState` 到 Basic ID、Location、System、Operator ID、Self-ID、Authentication 和 Message Pack 的逐字节结果。每个向量必须在测试名中标明来源条款或官方库测试用例名称，例如：

```cpp
TEST_CASE("ASTM F3411-22a location vector", "[odid]") {
    const rid::FlightState state = rid::test_state();
    const auto encoded = rid::OdidEncoder{}.encode_location(state, rid::test_identity());
    TEST_ASSERT_TRUE(encoded.ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kExpectedLocation.data(),
                                  encoded.value().data(),
                                  kExpectedLocation.size());
}
```

- [ ] **步骤 3：实现薄适配层**

`OdidEncoder` 只负责单位、枚举和缺省值映射，所有标准位编码调用官方库。不得复制官方库的位打包逻辑。

```cpp
EncodeResult encode(MessageKind kind,
                    const FlightState &state,
                    const TestIdentity &identity,
                    const OdidOptions &options) const;
```

- [ ] **步骤 4：运行官方库测试和项目测试**

预期：上游编码测试与本项目所有 `[odid]` 黄金向量通过。

- [ ] **步骤 5：提交 OpenDroneID 适配**

```bash
git add dependencies.lock components/rid_odid test_apps/logic docs/protocol/opendroneid-mapping.md
git commit -m "feat: 集成 OpenDroneID 编码"
```

### 任务 7：实现 GB 46750-2025 编码器

**文件：**
- 创建：`components/rid_gb/CMakeLists.txt`
- 创建：`components/rid_gb/include/rid/gb_encoder.hpp`
- 创建：`components/rid_gb/gb_encoder.cpp`
- 创建：`components/rid_gb/private_include/gb_wire.hpp`
- 创建：`test_apps/logic/main/test_gb_encoder.cpp`
- 创建：`docs/protocol/gb46750-mapping.md`

- [ ] **步骤 1：建立标准字段追踪表**

`gb46750-mapping.md` 必须逐消息列出：标准条款号、字段名、位宽、比例因子、无效值、字节序、更新来源和项目字段。只记录项目实现所需事实，不复制受版权保护的大段标准正文。

- [ ] **步骤 2：从标准示例建立黄金向量测试**

每种广播消息至少包含一个正常向量和边界向量。逐字节断言结构如下：

```cpp
TEST_CASE("GB 46750 location normal vector", "[gb46750]") {
    const auto result = rid::GbEncoder{}.encode(
        rid::MessageKind::Location, rid::gb_test_state(), rid::gb_test_identity());
    TEST_ASSERT_TRUE(result.ok());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(kGbLocationExpected.data(),
                                  result.value().data(),
                                  kGbLocationExpected.size());
}
```

- [ ] **步骤 3：运行测试并确认编码器不存在而失败**

预期：`GbEncoder` 未定义或黄金向量不匹配。

- [ ] **步骤 4：用显式字节操作实现编码**

禁止使用 C/C++ 位域或直接序列化结构体。`gb_wire.hpp` 提供明确的 `put_u16_be`、`put_i32_le` 等标准要求的写入函数，并在每次缩放前完成范围检查。

- [ ] **步骤 5：运行全部 GB 向量和边界测试并提交**

```bash
idf.py -C test_apps/logic build flash monitor
git add components/rid_gb test_apps/logic docs/protocol/gb46750-mapping.md
git commit -m "feat: 添加 GB 46750 报文编码"
```

### 任务 8：实现截止时间调度器

**文件：**
- 创建：`components/rid_scheduler/CMakeLists.txt`
- 创建：`components/rid_scheduler/include/rid/scheduler.hpp`
- 创建：`components/rid_scheduler/scheduler.cpp`
- 创建：`test_apps/logic/main/test_scheduler.cpp`

- [ ] **步骤 1：编写确定性调度测试**

使用注入的单调时钟，不在测试中调用真实 `vTaskDelay()`：

```cpp
TEST_CASE("scheduler drops stalest payload on overflow", "[scheduler]") {
    rid::Scheduler scheduler(/*capacity=*/2);
    scheduler.enqueue(rid::payload(100, 1000));
    scheduler.enqueue(rid::payload(200, 1100));
    scheduler.enqueue(rid::payload(300, 1200));
    TEST_ASSERT_EQUAL_UINT32(1, scheduler.stats().dropped);
    TEST_ASSERT_EQUAL_UINT32(200, scheduler.pop_next(1200)->sequence);
}
```

测试最早截止时间优先、周期覆盖、过期载荷、队列隔离和计数溢出饱和行为。

- [ ] **步骤 2：实现固定容量队列和统计**

```cpp
struct TransportStats {
    uint64_t expected;
    uint64_t submitted;
    uint64_t completed;
    uint64_t late;
    uint64_t dropped;
    uint64_t encode_errors;
    uint64_t radio_errors;
};
```

运行期间不得分配堆内存；载荷缓冲区在应用配置时按估算硬限制一次性建立。

- [ ] **步骤 3：运行测试并提交**

```bash
idf.py -C test_apps/logic build flash monitor
git add components/rid_scheduler test_apps/logic
git commit -m "feat: 添加 RID 截止时间调度器"
```

### 任务 9：实现 BLE4/BLE5 承载复用

**文件：**
- 创建：`components/rid_radio/CMakeLists.txt`
- 创建：`components/rid_radio/include/rid/ble_transport.hpp`
- 创建：`components/rid_radio/ble_transport.cpp`
- 创建：`test_apps/integration/main/test_ble_transport.cpp`

- [x] **步骤 1：编写带假 GAP 后端的失败测试**

```cpp
TEST_CASE("BLE transport rotates aircraft by deadline", "[ble]") {
    FakeBleGap gap;
    rid::BleTransport tx(gap);
    tx.submit(rid::ble_payload(1, rid::Transport::Ble4, 100));
    tx.submit(rid::ble_payload(2, rid::Transport::Ble5, 90));
    tx.poll(90);
    TEST_ASSERT_EQUAL_UINT16(2, gap.last_aircraft_index());
    TEST_ASSERT_EQUAL(rid::BleMode::Extended, gap.last_mode());
}
```

- [x] **步骤 2：实现后端接口与 NimBLE 适配**

```cpp
class BleGapBackend {
public:
    virtual esp_err_t configure(BleMode mode, ByteView payload) = 0;
    virtual esp_err_t start() = 0;
    virtual esp_err_t stop() = 0;
    virtual ~BleGapBackend() = default;
};
```

Legacy 和 Extended 广播分别校验载荷上限。广播完成回调只入队事件，不做编码或日志格式化。

- [x] **步骤 3：在开发板执行混合 BLE 测试**

至少轮换 25 个 BLE4 和 25 个 BLE5 实例 10 分钟，无控制器资源泄漏；提交数、完成数和错误数与串口统计一致。

- [x] **步骤 4：提交 BLE 承载**

```bash
git add components/rid_radio test_apps/integration
git commit -m "feat: 添加 BLE RID 承载复用"
```

### 任务 10：实现 Wi-Fi Beacon 与双频跳转

**文件：**
- 创建：`components/rid_radio/include/rid/wifi_transport.hpp`
- 创建：`components/rid_radio/wifi_transport.cpp`
- 创建：`components/rid_radio/include/rid/radio_coordinator.hpp`
- 创建：`components/rid_radio/radio_coordinator.cpp`
- 创建：`test_apps/integration/main/test_wifi_transport.cpp`
- 创建：`docs/protocol/transport-matrix.md`

- [x] **步骤 1：编写帧封装与驻留测试**

```cpp
TEST_CASE("coordinator stays on the only active band", "[wifi]") {
    FakeWifiBackend wifi;
    rid::RadioCoordinator radio(wifi, /*default_dwell_ms=*/100);
    radio.submit(rid::wifi_payload(rid::Transport::Wifi58, rid::WifiMode::Beacon));
    radio.poll(0);
    radio.poll(100);
    TEST_ASSERT_EQUAL_UINT8(149, wifi.channel());
    TEST_ASSERT_EQUAL_UINT32(0, wifi.switches_to(6));
}
```

逐字节测试 Beacon Vendor IE，测试信道 6/149 加权驻留、空队列不切换、切换失败重试和国家码固定为 `CN`。NAN 不属于产品 RID 承载，不实现原生或原始帧仿真。

- [x] **步骤 2：实现公开 API 后端**

```cpp
class WifiBackend {
public:
    virtual esp_err_t set_country_cn() = 0;
    virtual esp_err_t set_channel(uint8_t primary) = 0;
    virtual esp_err_t transmit(ByteView frame) = 0;
    virtual ~WifiBackend() = default;
};
```

生产后端只调用公开 ESP-IDF API。帧序列号由驱动管理时，`esp_wifi_80211_tx()` 的 `en_sys_seq` 必须与当前接口状态匹配。

射频初始化固定调用 `esp_wifi_set_max_tx_power()` 设置 Wi-Fi 传导功率 2 dBm，并通过 NimBLE 控制器公开 API 设置 BLE 传导功率 0 dBm。这两个值来自只读板级构建常量，Shell 不得暴露修改入口。

- [x] **步骤 3：完成支持矩阵**

`transport-matrix.md` 对 GB/OpenDroneID 与 BLE4、BLE5、Wi-Fi Beacon 的每种组合标注“标准允许/工程禁止/板上已验证”。NAN 组合统一标注“工程禁止”。运行时校验直接使用同一张编译期表。

- [x] **步骤 4：板上运行双频与 BLE 共存测试并提交**

```bash
idf.py -C test_apps/integration build flash
pytest -q pytest_rid --target esp32c5 -k "wifi or coexist"
git add components/rid_radio test_apps/integration docs/protocol/transport-matrix.md
git commit -m "feat: 添加 Wi-Fi RID 与双频协调"
```

### 任务 11：实现 NVS 原子持久化

**文件：**
- 创建：`components/rid_config/include/rid/nvs_store.hpp`
- 创建：`components/rid_config/nvs_store.cpp`
- 创建：`test_apps/integration/main/test_nvs_store.cpp`

- [x] **步骤 1：编写掉电与损坏测试**

测试无配置、有效配置、CRC 错误、结构版本不兼容和保存中断。有效配置往返后必须逐字段相等；损坏配置必须返回错误且不得产生可启动快照。测试还必须确认发送统计不会写入 NVS，重启后所有统计从零开始。

- [x] **步骤 2：实现双槽提交协议**

```cpp
struct StoredHeader {
    uint32_t magic;
    uint16_t schema_version;
    uint16_t payload_length;
    uint32_t generation;
    uint32_t crc32;
};
```

使用 `cfg_a`、`cfg_b` 两个 NVS blob。写入非当前槽并提交、校验读回后才报告成功；加载时独立校验两槽并选择 CRC 正确且代次最新的兼容槽。`active_generation` 不作为额外提交标记，因为 ESP-IDF v5.5.4 的 `nvs_commit()` 当前不提供独立原子屏障，冗余标记失败会与已完整写入的新槽产生矛盾语义。

- [x] **步骤 3：运行 NVS 集成测试并提交**

```bash
idf.py -C test_apps/integration build flash
pytest -q pytest_rid --target esp32c5 -k nvs
git add components/rid_config test_apps/integration
git commit -m "feat: 添加配置快照持久化"
```

### 任务 12：实现事务式交互 Shell

**文件：**
- 创建：`components/rid_shell/CMakeLists.txt`
- 创建：`components/rid_shell/include/rid/shell.hpp`
- 创建：`components/rid_shell/shell.cpp`
- 创建：`pytest_rid/test_shell.py`

- [ ] **步骤 1：编写端到端 Shell 失败测试**

```python
def test_group_apply_save_and_restore(dut):
    dut.write("site set latitude 31.2304 longitude 121.4737")
    dut.expect_exact("OK site staged")
    dut.write("group add odid_ble count 10 protocol odid transport ble5")
    dut.expect_exact("OK group odid_ble staged")
    dut.write("config check")
    dut.expect_exact("OK aircraft=10")
    dut.write("config apply")
    dut.expect_exact("OK generation=1 running=10")
    dut.write("config save")
    dut.expect_exact("OK saved generation=1")
```

另测总数 51、重复组名、无中心坐标、错误周期、消息启停与周期覆盖、回滚、`fleet show`、`radio status`、`system stop/start`。

- [ ] **步骤 2：实现稳定命令和错误码**

命令处理器只操作暂存配置或调用运行时接口，不直接访问编码器和射频。输出首行固定为 `OK <code>` 或 `ERR <code> <message>`，便于人和自动测试解析。

- [ ] **步骤 3：运行 Shell 测试并提交**

```bash
pytest -q pytest_rid/test_shell.py --target esp32c5
git add components/rid_shell pytest_rid/test_shell.py
git commit -m "feat: 添加 RID 配置 Shell"
```

### 任务 13：装配运行时与上电自动启动

**文件：**
- 创建：`components/rid_runtime/CMakeLists.txt`
- 创建：`components/rid_runtime/include/rid/runtime.hpp`
- 创建：`components/rid_runtime/runtime.cpp`
- 修改：`main/app_main.cpp`
- 创建：`pytest_rid/test_runtime.py`

- [ ] **步骤 1：编写启动状态机测试**

测试四条路径：无配置保持停发、有效配置自动启动、损坏配置拒绝启动、在线应用失败继续旧配置。

```cpp
enum class RuntimeState : uint8_t {
    NoConfig,
    Stopped,
    Starting,
    Running,
    Recovering,
    Faulted,
};
```

- [ ] **步骤 2：实现依赖装配和任务边界**

`app_main()` 只执行 NVS/控制台初始化、构造服务、加载快照、启动运行时和注册 Shell。轨迹、编码、调度、BLE、Wi-Fi 分属明确任务或事件循环，跨任务传递固定容量消息。

运行时只输出当前状态和汇总计数，不创建历史日志分区、文件或 NVS 键。启动新配置时以单调时钟零点重新初始化所有轨迹和相对消息时间。

- [ ] **步骤 3：实现无线故障恢复**

单条载荷失败只计数；连续射频失败达到 3 次时重启对应子系统；连续 3 次子系统恢复失败时进入 `Recovering` 并触发受控整机重启。每个阈值使用具名常量并接受测试注入。

- [ ] **步骤 4：运行运行时测试并提交**

```bash
pytest -q pytest_rid/test_runtime.py --target esp32c5
git add components/rid_runtime main/app_main.cpp pytest_rid/test_runtime.py
git commit -m "feat: 装配 RID 发生器运行时"
```

### 任务 14：完成整机验收与用户文档

**文件：**
- 创建：`pytest_rid/test_acceptance.py`
- 创建：`pytest_rid/receiver_log.py`
- 创建：`docs/hardware/setup.md`
- 创建：`docs/build-flash.md`
- 创建：`docs/shell-reference.md`
- 创建：`docs/configuration-reference.md`
- 创建：`docs/verification/acceptance-procedure.md`
- 创建：`docs/verification/acceptance-report.md`
- 创建：`docs/authorized-test-site-notice.md`
- 创建：`README.md`

- [ ] **步骤 1：自动化 1/10/50 架基准场景**

验收脚本通过 Shell 建立配置，并读取自研接收机导出的 JSON Lines 记录。`receiver_log.py` 固定要求每行包含 `timestamp_ms`、`test_id`、`message_kind`、`valid`、`latitude` 和 `longitude`；接收机无需修改时，可由独立转换脚本把其现有输出转换为该格式。验收断言：

```python
assert discovered_ids == expected_ids
assert malformed_payloads == 0
assert primary_delivery_ratio >= 0.95
assert discovery_time_s <= 15
```

50 架基准使用主要动态消息 1 s、其他启用消息 3 s、Wi-Fi 驻留目标 100 ms、距离 10 m，连续观测 10 分钟。

- [ ] **步骤 2：执行持续运行与掉电测试**

连续运行 24 小时，记录启动次数、最小剩余堆、最大队列深度和各承载错误计数。随后执行 10 次掉电重启，每次确认配置代次、机群 ID 和自动启动状态一致。

- [ ] **步骤 3：执行 0～50°C 与 10 m 固定功率验证**

在 USB 5 V 供电下，于 0°C、25°C、50°C 三个稳定温度点运行 50 架基准场景。确认 Wi-Fi 2 dBm、BLE 0 dBm 构建配置下 10 m 接收符合基准；如果不符合，只修改板级构建常量并重新执行三个温度点，最终数值写入验收报告。

- [ ] **步骤 4：完成全部文档**

Shell 参考必须列出每个命令的语法、参数、成功输出和错误码。配置参考必须列出全局、组级、消息级的默认值和覆盖顺序。试验场说明必须明确仅用于授权且边界受控的室外测试。

- [ ] **步骤 5：运行最终验证**

```bash
idf.py fullclean
idf.py set-target esp32c5
idf.py build
idf.py -C test_apps/logic build
idf.py -C test_apps/integration build
pytest -q pytest_rid --target esp32c5
git diff --check
```

预期：所有构建成功，所有自动化测试通过，`git diff --check` 无输出。

- [ ] **步骤 6：提交验收与文档**

```bash
git add README.md pytest_rid docs
git commit -m "docs: 完成 RID 发生器验收与使用说明"
```

## 三、完成判据

只有同时满足以下条件，实施计划才算完成：

1. ESP-IDF v5.5.4 在指定 ESP32-C5 开发板上通过全部七项射频可行性关卡。
2. GB 46750 和 OpenDroneID 编码器通过逐字节黄金向量测试。
3. 1、10、50 架机群均可通过 Shell 配置、应用、保存和掉电恢复。
4. BLE4、BLE5、Wi-Fi 2.4 GHz 和 Wi-Fi 5 GHz 可按支持矩阵工作。
5. 信道 6/149 跳频期间的延迟和丢弃均有可查看统计。
6. 50 架基准场景在 10 m 内满足 15 s 全发现和主要动态消息送达率不低于 95%。
7. 通过 24 小时运行、10 次掉电重启和 0～50°C 验证。
8. 构建、测试、Shell、配置、协议映射和授权试验场文档齐全。

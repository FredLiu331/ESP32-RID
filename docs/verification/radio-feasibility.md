# ESP32-C5 RID 射频可行性验证

## 当前结论

状态：`PENDING_HARDWARE`。探针已使用 ESP-IDF v5.5.4 为 ESP32-C5 构建成功，
但当前主机没有可用串口设备，七项射频关卡均未在实机执行。构建成功不代表射频
可行性通过，后续任务不得引用本文作为实机 `PASS` 证据。

NimBLE GAP 提供的是广播过程完成事件，不是每个广播 PDU 的空口确认。探针中 BLE4
通过三个有限时长 Legacy Advertising 过程计数，BLE5 通过
`num_ext_adv_events` 计数；Wi-Fi 使用 `esp_wifi_register_80211_tx_cb()` 的逐帧状态。

## 可复现环境

| 项目 | 值 |
| --- | --- |
| 开发板 | Waveshare ESP32-C5-WIFI6-KIT-N32R8 |
| 开发板硬件版本 | `PENDING: 板上丝印/采购批次` |
| ESP-IDF 版本 | v5.5.4 |
| ESP-IDF 提交 | `735507283d5b2f9fb363a1901172dbd9e847945d` |
| `sdkconfig` SHA-256 | `8d09aae8f78c01a7a0c90fe1170658947bdf7367ce3ad851a1a5973beff31263` |
| 天线 | `PENDING: 型号、连接端口、增益和摆放` |
| Wi-Fi 国家码 | `CN`，manual policy；2.4 GHz ch1-13，5 GHz mask ch149 |
| Wi-Fi 传导功率配置 | 2 dBm（`esp_wifi_set_max_tx_power(8)`） |
| 串口 | `PENDING`；2026-07-19 未发现 `/dev/ttyACM*` 或 `/dev/ttyUSB*` |

`sdkconfig` 哈希对应 `idf.py -C test_apps/radio_probe set-target esp32c5` 后的生成配置。
若重新配置或更换 ESP-IDF，实测前必须更新哈希。

## RED 与构建证据

先创建 `pytest_rid/test_radio_probe.py`，再执行要求的 RED 命令。当前 shell 和加载
ESP-IDF 环境后均返回 127：`pytest: command not found`。进一步确认 ESP-IDF Python
环境未安装 `pytest` 与 `pytest-embedded`。因此测试运行器在连接 DUT 前即被依赖缺失
阻塞；这不是七项关卡中的任何一项失败或通过。

构建命令：

```bash
. /home/tomato/esp/esp-idf-v5.5.4/export.sh
idf.py -C test_apps/radio_probe set-target esp32c5
idf.py -C test_apps/radio_probe build
```

构建结果：成功。提交前最终构建的 `radio_probe.bin` 大小 `0x12b0a0`，factory app
分区 `0x177000`，剩余 `0x4bf60`（20%）。

## 实机支持矩阵

下表只能用实机串口 `PROBE_DETAIL` 输出填写。ESP 错误使用 `esp_err_t` 数值；BLE4/5
使用 NimBLE host 错误码；共存项可能记录 ESP Wi-Fi 或 NimBLE 错误码，必须同时记录
日志中的 `domain`。

| 关卡 | API/完成依据 | submitted | completed | error | 结果 |
| --- | --- | ---: | ---: | ---: | --- |
| `wifi_beacon_ch6` | `esp_wifi_80211_tx` / TX callback success | PENDING | PENDING | PENDING | PENDING |
| `wifi_beacon_ch149` | `esp_wifi_80211_tx` / TX callback success | PENDING | PENDING | PENDING | PENDING |
| `wifi_nan_ch6` | `esp_wifi_80211_tx` / TX callback success | PENDING | PENDING | PENDING | PENDING |
| `wifi_nan_ch149` | `esp_wifi_80211_tx` / TX callback success | PENDING | PENDING | PENDING | PENDING |
| `ble4` | Legacy GAP procedure complete, 3 cycles | PENDING | PENDING | PENDING | PENDING |
| `ble5` | Extended GAP complete, `num_ext_adv_events >= 3` | PENDING | PENDING | PENDING | PENDING |
| `coexist_hop` | 100 次 `6 -> 149 -> 6`，无连续切换错误且 BLE 完成计数增长 | PENDING | PENDING | PENDING | PENDING |

实机判定还必须用受控试验环境中的抓包或目标 RID 接收机确认 Beacon/NAN 帧实际位于
指定信道，并确认 BLE 广播载荷可接收。驱动完成回调只能证明驱动报告发送完成，不能
替代空口协议与频谱验证。

## 待执行命令

先在 ESP-IDF v5.5.4 Python 环境安装项目测试依赖，再连接开发板并明确指定串口：

```bash
. /home/tomato/esp/esp-idf-v5.5.4/export.sh
idf.py -C test_apps/radio_probe set-target esp32c5
idf.py -C test_apps/radio_probe build
idf.py -C test_apps/radio_probe -p /dev/ttyACM0 flash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5 \
  --app-path test_apps/radio_probe --port /dev/ttyACM0
```

同时按任务要求保留不带额外定位参数的验收入口：

```bash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5
```

只有串口测试捕获七条精确 `PASS` 行，支持矩阵填入实测计数/错误码，且空口检查完成
后，本文状态才可改为 `PASS`。任一项 `FAIL` 时应停止后续实现，把完整
`PROBE_DETAIL`、板卡/天线信息和抓包观察写入本文，并同步修订设计规格中的支持声明。

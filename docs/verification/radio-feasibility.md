# ESP32-C5 RID 射频可行性验证

## 当前结论

状态：`PASS_DRIVER_GATES`。2026-07-21 在 ESP32-C5 rev1.2 实机上，七项驱动/API
关卡全部通过。该结果证明 ESP-IDF 接受发送请求并报告完成，不证明报文已在指定信道
以正确空口格式发出；外部抓包或目标 RID 接收机验证仍待执行。

NimBLE GAP 提供的是广播过程完成事件，不是每个广播 PDU 的空口确认。探针中 BLE4
通过扩展广播 API 配置 `legacy_pdu=1`，以一次有限事件过程计数，BLE5 通过
`num_ext_adv_events` 计数；Wi-Fi 使用 `esp_wifi_register_80211_tx_cb()` 的逐帧状态。

## 可复现环境

| 项目 | 值 |
| --- | --- |
| 开发板 | Waveshare ESP32-C5-WIFI6-KIT-N32R8 |
| 芯片版本 | ESP32-C5 rev1.2 |
| ESP-IDF 版本 | v5.5.4 |
| ESP-IDF 提交 | `735507283d5b2f9fb363a1901172dbd9e847945d` |
| `sdkconfig` SHA-256 | `e7afef1458e4d1f74feb030dfe49784c1cddca7ba24f2e849e8775d747c1ea29` |
| 天线 | `PENDING: 型号、连接端口、增益和摆放` |
| Wi-Fi 国家码 | `CN`，manual policy；2.4 GHz ch1-13，5 GHz mask ch149 |
| Wi-Fi 传导功率配置 | 2 dBm（`esp_wifi_set_max_tx_power(8)`） |
| USB 串口 | USB VID:PID `1a86:55d3`；稳定路径 `usb-1a86_USB_Single_Serial_5B90156485-if00` |
| 实测固件 | `33d8245-dirty`；`radio_probe.bin` 大小 `0x12aff0` |

`sdkconfig` 哈希对应 `idf.py -C test_apps/radio_probe set-target esp32c5` 后的生成配置。
若重新配置或更换 ESP-IDF，实测前必须更新哈希。

## RED 与构建证据

初次实机运行时，Wi-Fi Beacon ch6/ch149、Wi-Fi NAN ch6/ch149、BLE5 和共存关卡通过，
但 BLE4 返回 `submitted=0 completed=0 error=8 domain=nimble`。错误码 8 是
`BLE_HS_ENOTSUP`。根因是启用 `CONFIG_BT_NIMBLE_EXT_ADV` 后仍调用旧
`ble_gap_adv_set_data()` / `ble_gap_adv_start()`；ESP-IDF v5.5.4 在该配置下不支持这条
旧 API 路径。修复改用 `ble_gap_ext_adv_*()`，设置 `legacy_pdu=1`，并为 BLE4、BLE5、
共存探针分配三个独立广播实例。复测七项全部通过。

项目验收测试依赖 `pytest` 与 `pytest-embedded`；当前 ESP-IDF Python 环境仍未安装
这些包，因此本文实机证据来自烧录后的串口输出，不声称 pytest 入口已执行。

构建命令：

```bash
. /home/tomato/esp/esp-idf-v5.5.4/export.sh
idf.py -C test_apps/radio_probe set-target esp32c5
idf.py -C test_apps/radio_probe build
```

构建结果：成功。实测固件 `radio_probe.bin` 大小 `0x12aff0`，factory app 分区
`0x177000`，剩余 `0x4c010`（20%）。

## 实机支持矩阵

下表只能用实机串口 `PROBE_DETAIL` 输出填写。ESP 错误使用 `esp_err_t` 数值；BLE4/5
使用 NimBLE host 错误码；共存项可能记录 ESP Wi-Fi 或 NimBLE 错误码，必须同时记录
日志中的 `domain`。

| 关卡 | API/完成依据 | submitted | completed | error | 结果 |
| --- | --- | ---: | ---: | ---: | --- |
| `wifi_beacon_ch6` | `esp_wifi_80211_tx` / TX callback success | 3 | 3 | 0 (`esp`) | PASS |
| `wifi_beacon_ch149` | `esp_wifi_80211_tx` / TX callback success | 3 | 3 | 0 (`esp`) | PASS |
| `wifi_nan_ch6` | `esp_wifi_80211_tx` / TX callback success | 3 | 3 | 0 (`esp`) | PASS |
| `wifi_nan_ch149` | `esp_wifi_80211_tx` / TX callback success | 3 | 3 | 0 (`esp`) | PASS |
| `ble4` | Legacy PDU extended GAP complete, `num_ext_adv_events >= 3` | 1 | 3 | 0 (`nimble`) | PASS |
| `ble5` | Extended GAP complete, `num_ext_adv_events >= 3` | 1 | 3 | 0 (`nimble`) | PASS |
| `coexist_hop` | 100 次 `6 -> 149 -> 6`，无连续切换错误且 BLE 完成计数增长 | 456 | 455 | 0 (`none`) | PASS |

实机判定还必须用受控试验环境中的抓包或目标 RID 接收机确认 Beacon/NAN 帧实际位于
指定信道，并确认 BLE 广播载荷可接收。驱动完成回调只能证明驱动报告发送完成，不能
替代空口协议与频谱验证。

## 复现命令

先在 ESP-IDF v5.5.4 Python 环境安装项目测试依赖，再连接开发板并明确指定串口：

```bash
. /home/tomato/esp/esp-idf-v5.5.4/export.sh
idf.py -C test_apps/radio_probe set-target esp32c5
idf.py -C test_apps/radio_probe build
idf.py -C test_apps/radio_probe \
  -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90156485-if00 flash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5 \
  --app-path test_apps/radio_probe \
  --port /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90156485-if00
```

同时按任务要求保留不带额外定位参数的验收入口：

```bash
pytest -q pytest_rid/test_radio_probe.py --target esp32c5
```

串口已经捕获七条精确 `PASS` 行并填入实测计数/错误码。只有完成受控环境下的空口
检查后，本文状态才可从 `PASS_DRIVER_GATES` 改为完整 `PASS`。后续若任一项出现
`FAIL`，应记录完整 `PROBE_DETAIL`、板卡/天线信息和抓包观察，并重新评估支持声明。

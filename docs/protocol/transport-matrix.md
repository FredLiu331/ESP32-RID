# RID 承载支持矩阵

本矩阵记录当前工程实现和验证状态，不把公开驱动 API 返回成功等同于协议空口合规。

| 协议 | BLE4 | BLE5 | Wi-Fi Beacon | Wi-Fi NAN |
|---|---|---|---|---|
| GB 46750 | 工程禁止 | 工程禁止 | 已完成板上双频驱动门验证；待标准正文和空口验证 | 工程禁止 |
| OpenDroneID | 已完成板上驱动门验证；待空口验证 | 已完成板上驱动门验证；待空口验证 | 已完成协议构帧和双频驱动门验证；待空口验证 | 工程禁止 |

GB 46750 Beacon Vendor Specific IE 固定为 `DD | len | FA 0B BC | 0D | counter | GB payload`。项目编码器输出的 GB payload 从 `DataType` 开始，Wi-Fi 组件负责封装 OUI、OUI Type 和消息计数器。

Wi-Fi 协调器只接受 Beacon 数据，固定使用信道 6 和 149。生产后端设置国家码 `CN`、双频自动模式和 2 dBm 发射功率；单频队列保持所在信道，双频队列按确定驻留时间轮换，切换或发送失败不删除载荷。

微雪 ESP32-C5-WIFI6-KIT-N32R8（ESP32-C5 rev1.2）已用 ESP-IDF v5.5.4 完成 GB Beacon 在信道 6 和 149 的驱动提交测试。该结果证明公开 API 路径可用，不替代接收机或抓包设备的空口协议验证。

由于 `esp_wifi_80211_tx()` 没有逐帧完成回调，Wi-Fi 统计中的 `submitted` 表示驱动接受调用，`completed` 保持为零；这不代表空口已确认发送。

OpenDroneID Beacon 使用 `DD | len | FA 0B BC | 0D | counter | Message Pack`，构帧实现来自项目锁定的 OpenDroneID C 代码，并由逐字节测试验证管理帧头、MAC、SSID、Supported Rates、OUI、OUI Type 和计数器。仍需外部接收机或抓包设备完成空口协议验证。

NAN 不属于本产品 RID 承载。工程不实现 NAN API、NAN 原始帧或 NAN 发送路径。

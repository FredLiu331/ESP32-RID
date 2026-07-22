# GB 46750-2025 字段映射

## 实现依据与范围

- 协议基线：GB 46750-2025 广播式远程识别数据包。
- 当前可复核输入：用户提供的 `/home/tomato/RID/gb46750_rid.c` 与
  `gb46750_rid.h`。其中说明字段规则来自标准文件并与 DJI Lito1 抓包交叉验证。
- 输入文件 SHA-256：C 文件
  `b061d47787e400426e57c65436767ce6d0e0be1cd8fccecf1d8afb284dffedf2`，头文件
  `a1e9fbeb248f186da9537cc4dd962a038b0dbc53a0d1651fa504c9d74cc6618a`。
- 输入实现没有携带标准页码或分条编号，因此下表使用“广播数据包/数据项 001～021”
  作为可核对定位，不虚构条款号。取得带页码的合法标准正文后应补充精确条款号。
- 本组件输出从 `DataType` 开始的协议载荷。OUI、OUI Type、消息计数器和 Wi-Fi
  Vendor Specific IE 由后续 Wi-Fi 承载组件封装。

包头依次为 `DataType=0xFF`、`Version=0x20`、一字节 `DataLength`。`DataLength`
只统计数据项内容，不含 DataIdentifier。DataIdentifier 每字节的 bit7～bit1 对应连续
七个数据项，bit0 为后续字节扩展标志。

## 数据项追踪表

除文本外，多字节数值均为小端。`raw` 表示空口无符号或有符号原始值。

| 标准定位 | 字段 | 位宽 | 比例/偏移 | 无效值 | 字节序 | 更新来源 | 项目字段 |
|---|---|---:|---|---|---|---|---|
| 数据项 001 | 唯一产品标识码 | 160 | 20 字节 ASCII | 未给出 | 不适用 | 配置展开时固定 | `TestIdentity.value` |
| 数据项 002 | 登记标志 | 64 | 8 字节 ASCII，不足补零 | 未给出 | 不适用 | 配置应用时固定 | `GbOptions.registration_mark` |
| 数据项 003 | 运行类别 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `operation_category` |
| 数据项 004 | 无人机分类 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `ua_classification` |
| 数据项 005 | 地面站定位类型 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `gcs_position_type` |
| 数据项 006 | 地面站经纬度 | 32+32 | 经度在前；`raw × 10^-7 deg` | 每个坐标 `0xFFFFFFFF` | LE | 配置位置；缺省取当前模拟位置 | `gcs_position` / `FlightState` |
| 数据项 007 | 地面站高度 | 16 | `raw / 2 - 1000 m` | `raw=0` | LE | 配置高度；缺省取模拟大地高度 | `gcs_altitude_m` / `altitude_msl_m` |
| 数据项 008 | 无人机经纬度 | 32+32 | 经度在前；`raw × 10^-7 deg` | 每个坐标 `0xFFFFFFFF` | LE | 每次轨迹采样 | `longitude_deg`, `latitude_deg` |
| 数据项 009 | 航迹角 | 16 | `raw × 0.1 deg` | `0xFFFF` | LE | 每次轨迹采样 | `heading_deg` |
| 数据项 010 | 地速 | 16 | `raw × 0.1 m/s` | `0xFFFF` | LE | 每次轨迹采样 | `horizontal_speed_mps` |
| 数据项 011 | 相对高度 | 16 | `raw / 2 - 9000 m` | `raw=0` | LE | 每次轨迹采样 | `height_agl_m` |
| 数据项 012 | 垂直速度 | 8 | bit7 符号，bit6～0 幅值；`0.5 m/s` | `0xFF` | 不适用 | 每次轨迹采样 | `vertical_speed_mps` |
| 数据项 013 | 大地高度 | 16 | `raw / 2 - 1000 m` | `raw=0` | LE | 每次轨迹采样 | `altitude_msl_m` |
| 数据项 014 | 气压高度 | 16 | `raw / 2 - 1000 m` | `raw=0` | LE | 配置或模拟大地高度 | `barometric_altitude_m` |
| 数据项 015 | 运行状态 | 8 | `0=地面，1=空中`（项目使用值） | 未给出 | 不适用 | 每次轨迹采样 | `airborne` |
| 数据项 016 | 坐标系类型 | 8 | `0=WGS-84`，`1=CGCS2000` | 未给出 | 不适用 | 构建时固定 WGS-84 | 常量 `0` |
| 数据项 017 | 水平精度 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `horizontal_accuracy` |
| 数据项 018 | 垂直精度 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `vertical_accuracy` |
| 数据项 019 | 速度精度 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `speed_accuracy` |
| 数据项 020 | 时间戳 | 48 | Unix 毫秒原值 | 未给出 | LE | 配置的测试时刻 | `timestamp_ms` |
| 数据项 021 | 时间戳精度 | 8 | 枚举原值 | 未给出 | 不适用 | 配置应用时固定 | `timestamp_accuracy` |

## 调度消息映射

GB 数据项由存在位指示，因此项目按更新频率拆分为四类合法载荷：

| `MessageKind` | GB 数据项 | DataIdentifier | 内容长度 |
|---|---|---|---:|
| `BasicId` | 001 | `80` | 20 |
| `OperatorId` | 002 | `40` | 8 |
| `System` | 003～007 | `3E` | 13 |
| `Location` | 008～021 | `01 FF FE` | 31 |

GB 46750 没有本项目 `Authentication` 和 `SelfId` 对应的数据项，编码器对这两种请求返回
`UnsupportedMessage`。所有缩放在范围检查后使用显式小端写入函数完成，禁止位域和结构体
直接序列化。

设备首版没有可信 UTC。`timestamp_ms` 是明确配置的合成测试值，不由启动后的相对毫秒
冒充 Unix 时间；默认零值用于接收机边界测试，不声明满足可信时间一致性要求。

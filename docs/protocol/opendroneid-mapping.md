# OpenDroneID 字段映射

## 实现基线

- 项目协议基线：ASTM F3411-22a，OpenDroneID 协议版本 2。
- 编码库：`opendroneid-core-c`，固定提交见根目录 `dependencies.lock`。
- 上游许可证：Apache-2.0；许可证及两个导入源码文件保留在
  `components/rid_odid/vendor/`，文件哈希记录在依赖锁中。
- 黄金向量来源：固定提交的 `test/test_inout.c`。项目测试保留其输入字段和编码后的
  逐字节结果，不复制标准正文。

适配器只把项目语义字段填入上游名义结构，所有压缩、缩放、枚举编码、字节序和 Pack
布局均调用上游 `encode*()` 实现。适配器在调用上游前拒绝非有限浮点值和越界的操作者
经纬度，避免无效配置进入数值缩放。

## 消息映射

| OpenDroneID 消息 | 项目来源 | 上游名义字段 | 缺省策略 |
|---|---|---|---|
| Basic ID | `TestIdentity.value` | `UASID` | 多旋翼；测试 ID 默认按序列号类型 |
| Location | `FlightState` | 状态、航向、速度、经纬度、高度、时间 | 高度参考地面；相对毫秒折算为小时内秒数 |
| Authentication | `OdidOptions` | 类型、时间戳、认证数据 | 首版单页，最多 17 字节，超长拒绝 |
| Self-ID | `OdidOptions.self_description` | 文本描述 | 最多 23 字节，超长拒绝 |
| System | `FlightState` 与 `OdidOptions` | 操作者位置、区域、EU 分类、时间戳 | 未配置操作者位置时使用无人机当前位置 |
| Operator ID | `OdidOptions.operator_id` | 操作者 ID | 最多 20 字节，超长拒绝 |
| Message Pack | 上述六种消息 | `ODID_MessagePack_data` | 固定顺序打包六条消息 |

## 时间限制

设备没有掉电保持的绝对 UTC。Location 使用启动后的相对毫秒并限制到一小时内；System
和 Authentication 时间戳由配置明确给出，缺省为零。该模式用于接收机压力与边界测试，
不声明满足需要可信 UTC 的一致性认证场景。

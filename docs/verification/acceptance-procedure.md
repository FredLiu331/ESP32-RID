# 整机验收流程

## 记录格式

接收机导出 JSON Lines，每行必须包含：`timestamp_ms`、`test_id`、`message_kind`、
`valid`、`latitude`、`longitude`。使用 `pytest_rid/receiver_log.py` 校验：

```bash
PYTHONPATH=pytest_rid python pytest_rid/receiver_log.py receiver.jsonl
```

送达率分母必须由架次、消息周期和有效观测时长计算得到，不能用接收日志行数代替；验收断言
调用 `evaluate_baseline(..., expected_primary_messages=N)` 时必须传入该预期总数。

## 1/10/50 架基准

对 1、10、50 架分别执行：清空暂存组、设置中心点、添加一个或多个组、`config check`、
`config apply`、`config save`。接收机连续记录至少 10 分钟。验收断言为：发现 ID 集合与
`fleet list` 一致、无畸形载荷、15 秒内发现全部 ID、主要动态消息送达率不低于 95%。

混合承载场景应至少覆盖 BLE4、BLE5、Wi-Fi 2.4 GHz 和 Wi-Fi 5.8 GHz；混合协议场景应覆盖
`gb` 和 `odid`。Wi-Fi 双频结果必须注明信道驻留和时分切换，不得声称物理同时发射。

## 重启恢复

保存后使用串口监视器的 `Ctrl+T`、`Ctrl+R` 复位，确认启动日志包含相同代次和架次；随后执行
`status`，确认 `generation`、`staged`、`applied` 一致。重复至少 10 次并记录任何启动失败。

## 长时间和温度项目

24 小时连续运行、10 次掉电重启，以及 0°C、25°C、50°C 三个温度点仍是现场项目，不能由
本地编译结果替代。验收报告应记录最小剩余堆、队列深度、承载错误数、环境温度和接收率。

当前报告模板见 [acceptance-report.md](acceptance-report.md)。

# ESP32-C5 RID 信号发生器

本项目用于授权、边界受控的 RID 接收设备测试。硬件为微雪
ESP32-C5-WIFI6-KIT-N32R8，软件基于 ESP-IDF v5.5.4。设备可生成 1～50 架
测试无人机，支持 GB 46750 和 OpenDroneID，以及 BLE4、BLE5、Wi-Fi 2.4 GHz
信道 6 和 Wi-Fi 5.8 GHz 信道 149 的 Beacon 承载。NAN 不在产品范围内。

当前配置入口是 USB 串口交互式 Shell。上电后加载最后一次成功保存的配置并持续运行；
没有有效 NVS 配置时保持停发，但仍启动 Shell，便于首次配置。

快速开始：

```text
site set latitude 31.2304 longitude 121.4737
group add fleet count 1 protocol odid transport ble5
config check
config apply
config save
```

构建、刷写、Shell 命令和验收步骤见：

- [构建与刷写](docs/build-flash.md)
- [硬件准备](docs/hardware/setup.md)
- [Shell 参考](docs/shell-reference.md)
- [配置参考](docs/configuration-reference.md)
- [整机验收流程](docs/verification/acceptance-procedure.md)

产品不得在未经授权的公共空域发射。试验边界和功率约束见
[授权试验场说明](docs/authorized-test-site-notice.md)。

# 构建与刷写

## 环境

- ESP-IDF v5.5.4：`/home/tomato/esp/esp-idf-v5.5.4`
- 目标：`esp32c5`
- 默认串口：`/dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90156485-if00`

```bash
source /home/tomato/esp/esp-idf-v5.5.4/export.sh
idf.py set-target esp32c5
idf.py build
idf.py -p /dev/serial/by-id/usb-1a86_USB_Single_Serial_5B90156485-if00 flash monitor
```

串口监视器为 115200 8N1。退出监视器使用 `Ctrl+]`。逻辑测试工程和集成测试工程：

```bash
idf.py -C test_apps/logic build
idf.py -C test_apps/integration build
```

逻辑测试刷写后，在菜单输入 `[shell]`。产品工程刷写成功后应看到
`rid: RID shell ready`；存在有效 NVS 配置时还应看到
`auto-start generation=N aircraft=M`。

## 注意

刷写会覆盖应用分区，但不会主动擦除 NVS。要清除保存配置，应明确执行擦除操作后重新刷写，
不要把擦除作为普通升级步骤。

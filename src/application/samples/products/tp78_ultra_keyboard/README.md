# TP78 Ultra Keyboard

这是 TP78 Ultra 主键盘固件入口，不是接收器固件。

当前结构只保留主键盘必需的有线链路：

- KEYSCAN 扫描键盘矩阵。
- 标准 USB Keyboard HID 向电脑发送按键。
- 使用开发板串口 Bootloader 直接烧录。
- 使用 SDK 默认 `bs21e-standard` Flash 分区。

接收器固件中的 SLE Client、协议转发、Vendor HID OTA 和专用 Flash
分区均已移除。USB HID 仍然保留，因为键盘通过 USB 连接电脑时必须使用
HID 键盘协议；它与 HID 烧录通道不是一回事。

## 需要按 PCB 调整的文件

`tp78_keymap.c` 是唯一的基础键位映射入口。当前表沿用 SDK 的 16 行 x 8
列参考矩阵，以保证工程可以直接编译。量产前应按 TP78 PCB 的实际行列接线
和键位修改该表。

## 构建

工程目标为 `standard-bs21e-1100e`，产品选项为：

```text
CONFIG_SAMPLE_SUPPORT_TP78_ULTRA_KEYBOARD=y
```

HiSpark Studio 项目文件为 `src/tp78_ultra_keyboard.hiproj`，烧录协议使用
`serial`。

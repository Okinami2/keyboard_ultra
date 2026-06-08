# TP78 Ultra Keyboard Firmware

TP78 Ultra 主键盘固件，基于 BS2X SDK 的 `standard-bs21e-1100e`
目标维护。

## 当前固件范围

- 16 x 8 KEYSCAN 矩阵扫描。
- 标准 USB Keyboard HID。
- 串口 Bootloader 直接烧录。
- SDK 标准 `bs21e-standard` Flash 分区。

本仓库不再包含接收器 SLE Client、接收器协议转发、Vendor HID 烧录通道、
HID 诊断工具或 TP78 专用分区。

## 目录

```text
src/
  application/samples/products/tp78_ultra_keyboard/  主键盘产品代码
  build/config/target_config/bs21e/                  BS21E 构建配置
  tp78_ultra_keyboard.hiproj                         HiSpark Studio 工程
docs/
  SDK_README.md                                      原 SDK 说明
```

## 构建和烧录

在 HiSpark Studio 中打开 `src/tp78_ultra_keyboard.hiproj`，构建目标为
`standard-bs21e-1100e`。工程已配置为 `serial` 烧录，不依赖 HID
烧录设备。

命令行构建：

```powershell
cd src
python build.py -c standard-bs21e-1100e
```

生成的固件包位于：

```text
src/tools/pkg/fwpkg/bs21e/bs21e_all.fwpkg
```

## 硬件适配

量产前必须按实际 PCB 修改
`src/application/samples/products/tp78_ultra_keyboard/tp78_keymap.c`。
当前键位表是 SDK 16 x 8 参考映射，只用于建立可编译、可烧录的主键盘工程。

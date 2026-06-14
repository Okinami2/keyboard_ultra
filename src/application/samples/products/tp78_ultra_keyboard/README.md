# TP78 Ultra Keyboard v2

这是从 TP78 v2（CH582M）迁移到 TP78 Ultra/v3（BS21E）的第二版主键盘固件。
工程使用 SDK 标准 `bs21e-standard` Flash 分区和串口 Bootloader，不包含接收器工程的 Vendor HID OTA 或专用升级分区。

## 已实现

- 6 行 x 14 列 GPIO 矩阵扫描和 5 次采样消抖。
- v2 默认键位层和 CapsLock 功能层。
- USB、BLE、SLE 三模 Keyboard、Mouse、Consumer Control HID。
- `Fn+F10` / `Fn+F11` / `Fn+F12` 切换 USB / BLE / SLE。
- `Fn+1` / `Fn+2` / `Fn+3` / `Fn+4` 切换 BLE 设备槽位。
- 长按 `Fn+F11` 2 秒为当前 BLE 槽位进入配对，长按 `Fn+F12` 2 秒进入 SLE 接收器配对。
- `Fn+-` / `Fn+=` 音量减/加。
- `Fn+R` 长按 2 秒软件复位。
- `左 Ctrl + 左 Alt + Backspace` 长按 2 秒软件复位。
- GPIO5 驱动 83 颗 WS2812，灯光由独立低优先级任务刷新。
- 按键位置到 LED 编号的映射与 TP78 v2 一致。
- 上电执行一次约 12 秒的“核心唤醒”动画：中央点亮、三圈青紫波纹连续扩散、三道冷白/冰蓝光束连续扫描闪烁，再渐变进入默认灯效。

## 无线连接与配对

- 上电默认处于 USB 模式，BLE 和 SLE 不开放配对广播。
- 切换到 BLE 或 SLE 后，正常模式只允许已经绑定的设备连接。
- BLE 保存 4 个独立设备槽位；每个槽位使用独立的 BLE 本机地址和名称，槽位选择及绑定设备地址保存在 NV 中。
- 长按 `Fn+F11` 会为当前 BLE 槽位开放配对；新设备配对成功后才替换旧绑定，不影响其他三个槽位。
- SLE 只保存一个接收器；长按 `Fn+F12` 会清除并替换原接收器。
- BLE/SLE 配对窗口为 120 秒，配对成功或超时后自动退出开放配对模式。
- 首次升级到四槽位本机身份方案时会清除旧 BLE 绑定，需要在各槽位长按 `Fn+F11` 重新配对一次。

## RGB 控制

| 组合键 | 功能 |
| --- | --- |
| `Fn+F1` | 关闭灯光 |
| `Fn+F2` | 冰蓝常亮 |
| `Fn+F3` | 紫色呼吸 |
| `Fn+F4` | 横向流水 |
| `Fn+F5` | 按键响应渐隐 |
| `Fn+F6` | 彩虹波浪 |
| `Fn+↑` / `Fn+↓` | 增加 / 降低亮度 |
| `Fn+→` / `Fn+←` | 加快 / 减慢动画 |

默认效果为彩虹波浪，默认亮度限制为 32/255，避免 USB 供电时 83 颗 LED 同时满亮造成过流。

## Ultra 管脚

板级映射集中在 `tp78_board.h`：

- ROW0..5: `22, 18, 17, 14, 15, 16`
- COL0..13: `25, 2, 26, 27, 11, 12, 13, 28, 29, 30, 0, 1, 3, 4`
- WS2812: `GPIO5`
- I2C SCL/SDA: `GPIO6/GPIO9`
- TrackPoint INT: `GPIO23`
- Battery ADC/CHRG: `GPIO31/GPIO24`
- Motor: `GPIO10`

## WS2812 时序

`tp78_ws2812.c` 按 TP78 v3 量产固件的实现使用 SPI0 驱动 GPIO5。
SPI 时钟为 8 MHz，每个 WS2812 数据位编码为一个 SPI 字节：逻辑 0 为 `0xC0`，
逻辑 1 为 `0xF8`。83 颗 LED 的一帧数据为 1992 字节，按 GRB 顺序发送。

这种实现由 SPI 硬件保持连续时序，不依赖 CPU 忙等，也不会在整帧发送期间关闭中断。
在 8 MHz 下，逻辑 0 的高电平约为 0.25 us，逻辑 1 的高电平约为 0.625 us，
单个位周期为 1 us；帧结束后额外保持低电平 80 us。

## 构建

目标为 `standard-bs21e-1100e`，产品配置：

```text
CONFIG_SAMPLE_SUPPORT_TP78_ULTRA_KEYBOARD=y
CONFIG_TP78_ULTRA_BLE_NAME="TP78 Ultra BLE"
CONFIG_TP78_ULTRA_SLE_NAME="TP78_ULTRA"
```

SLE 模式使用版本 2 完整状态帧连接 `tp78_ultra_receiver`。接收器默认扫描名称
`TP78_ULTRA`，现有接收器协议无需修改。

HiSpark Studio 工程为 `src/tp78_ultra_keyboard.hiproj`，烧录协议为 `serial`。

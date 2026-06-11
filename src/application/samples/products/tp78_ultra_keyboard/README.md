# TP78 Ultra Keyboard v2

这是从 TP78 v2（CH582M）迁移到 TP78 Ultra/v3（BS21E）的第二版主键盘固件。
工程使用 SDK 标准 `bs21e-standard` Flash 分区和串口 Bootloader，不包含接收器工程的 Vendor HID OTA 或专用升级分区。

## 已实现

- 6 行 x 14 列 GPIO 矩阵扫描和 5 次采样消抖。
- v2 默认键位层和 CapsLock 功能层。
- USB Keyboard、Mouse、Consumer Control 复合 HID。
- `Fn+-` / `Fn+=` 音量减/加。
- `Fn+R` 长按 2 秒软件复位。
- `左 Ctrl + 左 Alt + Backspace` 长按 2 秒软件复位。
- GPIO5 驱动 83 颗 WS2812，灯光由独立低优先级任务刷新。
- 按键位置到 LED 编号的映射与 TP78 v2 一致。
- 上电执行一次约 12 秒的“核心唤醒”动画：中央点亮、三圈青紫波纹连续扩散、三道冷白/冰蓝光束连续扫描闪烁，再渐变进入默认灯效。

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
```

HiSpark Studio 工程为 `src/tp78_ultra_keyboard.hiproj`，烧录协议为 `serial`。

# TP78 Ultra Keyboard v1

这是从 TP78 v2（CH582M）迁移到 TP78 Ultra/v3（BS21E）的第一版主键盘固件。
工程使用 SDK 标准 `bs21e-standard` Flash 分区和串口 Bootloader，不包含接收器工程里的
Vendor HID OTA、专用升级分区或 U 盘配置模式。

## 已迁移

- 按金手指信号顺序实现的 6 行 x 14 列 GPIO 矩阵扫描和 5 次采样消抖。
- v2 默认主键位层。
- v2 CapsLock 功能层：单击 CapsLock 仍发送 CapsLock；按住 CapsLock 再按其他键进入功能层。
- Caps 功能层中的方向键、翻页、Home/End、PrintScreen 和左右鼠标键。
- USB Keyboard、Mouse、Consumer Control 复合 HID 报告。
- `Fn+-` / `Fn+=` 音量减/加。
- `Fn+R` 长按 2 秒软件复位。
- `左 Ctrl + 左 Alt + Backspace` 长按 2 秒软件复位。
- `Fn+F1` 至 `Fn+F6` 灯效选择入口，当前打印调试信息，供 WS2812 驱动接入。

## Ultra 管脚

板级映射集中在 `tp78_board.h`。矩阵使用以下 BS21E GPIO：

- ROW0..5: `22, 18, 17, 14, 15, 16`
- COL0..13: `25, 2, 26, 27, 11, 12, 13, 28, 29, 30, 0, 1, 3, 4`

保留外设定义：

- I2C SCL/SDA: `GPIO6/GPIO9`
- TrackPoint INT: `GPIO23`
- Battery ADC/CHRG: `GPIO31/GPIO24`
- Motor: `GPIO10`
- WS2812: `GPIO5`

## 未迁移到第一版

- MPR121 触摸条、PS/2、UART3 和 Boot 金手指管脚：v3 硬件已移除。
- CH582M 私有 2.4G RF：后续应改为 BS21E SLE，不能直接复用旧 RF PHY。
- FATFS/U 盘配置：后续使用 BS21E NV 和 VIA Raw HID。
- OLED、I2C TrackPoint 数据读取、电池显示、马达和 WS2812 实际驱动。
- VIA 动态改键、宏和持久化。

## 构建

目标为 `standard-bs21e-1100e`，产品配置：

```text
CONFIG_SAMPLE_SUPPORT_TP78_ULTRA_KEYBOARD=y
```

HiSpark Studio 工程为 `src/tp78_ultra_keyboard.hiproj`，烧录协议为 `serial`。

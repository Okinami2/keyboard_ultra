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


现状
I2C 设备识别已经没问题，启动时能按预期得到 3 个设备：
0x15 TrackPoint
0x3C SSD1306 OLED
0x5A MPR121
非 DMA 方向之前已经证明 OLED 能响应，AT+TP78OLED 曾经可以让屏幕全亮。所以硬件、地址、管脚复用、基本 I2C 通路大概率是通的。
现在卡在 I2C DMA 写 OLED：
DMA 配置成功：ret:0x0
DMA 启动成功：ret:0x0
DMA 有搬运进度：block:8/12/32
但最后失败：0x80001317
总线表现为传输中卡住：scl:0 sda:0，恢复后 scl:1 sda:0
这说明问题不是 OLED 地址或 AT 逻辑，而是 I2C + DMA + I2C FIFO/STOP/中断完成判定 这一段没有跑顺。
发现的问题
SDK 的 I2C DMA 等待原本是永久等待
drivers/drivers/driver/i2c/i2c.c 里 DMA 等待用 osal_sem_down()，DMA 中断不回来就会永久卡住，导致 AT 一直 BUSY，甚至异常重启。

DMA IRQ/回调没有稳定回来
日志里没有看到 DMA irq / I2C DMA isr，但是 block 在增长，说明 DMA 确实搬了数据，只是完成中断/回调链路不可靠或没进来。

长包 DMA 会卡在 FIFO 附近
len:32 时经常停在 block:8 或 block:12，很像 I2C FIFO 没继续消耗，DMA request 停了。也就是说“单纯加大包”不是正确方向。

IRQ 定义存在可疑不一致
interrupt_adapter.c 注释里 32 是 M_DMA_IRQn，但 chip_core_irq.h 里 32 叫 RESERVED32_IRQN。我尝试把 DMA porting 改到 32 号 IRQ，但日志看起来还没有解决。

已经改动过的内容
application/samples/products/tp78_ultra_keyboard/tp78_i2c.c
增加/保留 I2C DMA 模式初始化日志。
去掉了 I2C 模块里重复 uapi_dma_init/open，因为系统启动阶段已经全局初始化 DMA。

application/samples/products/tp78_ultra_keyboard/tp78_oled.c
做过多轮 OLED 发送策略调整：合并 SSD1306 初始化命令。
尝试命令包补齐到 32 字节。
尝试数据块改成 32 字节。
最后刚刚又改回更小的 8 字节分块方向，但这一步还没有编译和打包。


drivers/drivers/driver/i2c/i2c.c
把 DMA/INT 等待从永久等待改成超时等待。
DMA 超时时释放 DMA channel，避免 AT worker 永久 BUSY。
加了 I2C DMA 诊断日志：DMA 配置
DMA 启动
DMA ISR
DMA block 计数

尝试用 uapi_dma_get_block_ts() 判断 DMA 是否已经搬完，而不是完全依赖 DMA IRQ。

drivers/chips/bs2x/porting/dma/dma_porting.c
尝试把 DMA IRQ 从 M_SDMA_IRQN 改到 32 号 M_DMA_IRQN。
加了 DMA IRQ 入口日志。

当前最后状态
RGB 隔离测试已经确认不是 OLED/I2C 失败根因，当前已恢复 RGB/WS2812 初始化和 RGB 任务。
当前优先目标是先跑通 OLED，而不是继续在 I2C DMA 路径里扩大或缩小 OLED 包。
设备驱动开发指南里说明：I2C DMA 读写要求 DMA 驱动已初始化，同时 I2C 支持轮询/DMA 自动切换和阈值配置。
本工程当前 target 配置是 `CONFIG_I2C_SUPPORT_DMA=y`，但没有打开 `CONFIG_I2C_SUPPORT_POLL_AND_DMA_AUTO_SWITCH`；如果应用调用 `uapi_i2c_set_dma_mode(..., true)`，即使 OLED 分成 8 字节小包，也仍然会强制走 DMA。

因此当前解决路径是：TP78 I2C 默认固定回轮询模式，先绕开不稳定的 DMA IRQ/STOP 完成判定；后续需要继续查 DMA 时，再显式打开 `CONFIG_TP78_ULTRA_I2C_USE_DMA`。
当前版本已增加 `CONFIG_TP78_ULTRA_I2C_USE_DMA`，默认关闭；启动日志应看到 `[tp78] I2C poll mode enabled`。
补充检查结果：当前 WS2812 代码实际使用 `SPI_BUS_0` + `HAL_PIO_SPI0_TXD` + `GPIO5`，没有使用 SPI1，也没有把 `GPIO6` 配成 SPI。`HAL_PIO_SPI1_RXD = 6` 里的 6 是 pinmux 功能编号，不是 GPIO6 管脚号。
当前更可能的问题不是 RGB/SPI 抢占，而是 GPIO9/SDA 物理线或同总线从设备在启动阶段已经把 SDA 拉低。
最新实测显示 `pre-init-gpio` 阶段 SCL/SDA 已经是 `scl:1 sda:0`，此时管脚处于 GPIO 输入上拉，I2C 控制器还没有介入；bus clear 后仍为 `scl:1 sda:0`。随后单条 OLED 命令 `len:2 first:0xae` 失败，abort 诊断为 `lost:1`，符合 SDA 被外部拉低导致主机仲裁丢失/总线异常。
当前版本新增 I2C 引脚和控制器 abort 诊断：I2C 控制器初始化前会打印 `I2C pins pre-init-gpio`；首次传输会打印 `I2C pins prepared`；失败时会打印 `write-fail`、`I2C abort regs` 和 `post-recover`。正常 pinmux 应为 `scl_mode:26 sda_mode:27`，分别对应 `HAL_PIO_I2C0_CLK` 和 `HAL_PIO_I2C0_DATA`。如果出现 `before-prepare-conflict`，说明 GPIO6/GPIO9 在两次 I2C 事务之间被其他代码改过。
结合设备驱动开发指南的 I2C 注意事项，当前日志更符合“某次发送失败后，总线挂死”的情况。当前验证版把 SCL/SDA 内部上拉打开，并把 OLED 初始化命令改为一次只发 1 条命令。若 `pre-init-gpio` 阶段 SDA 就是 0，说明 GPIO9/SDA 物理线在 I2C 控制器介入前已经被拉低；若 `pre-init-gpio` 是 1/1，但写 0x3C 后失败，则看 `I2C abort regs` 中的 `addr_nack/data_nack/sda_stuck/lost` 来判断是无 ACK、数据 NACK、SDA stuck 还是仲裁丢失。
当前构建在 `pre-init-gpio` 检测到总线不空闲时，会跳过预期设备表，不再假报 0x15/0x3C/0x5A 已存在；后续 I2C 读写也会直接跳过，避免反复触发 TX_ABRT。

烧录后验证顺序：
1. 启动日志先看 `I2C pins pre-init-gpio`。如果还是 `scl:1 sda:0`，优先查硬件：GPIO9/SDA 是否短地、OLED/MPR121/TrackPoint 是否有未上电或损坏从设备拉低 SDA、SDA 管脚定义是否和原理图一致。
2. 如果 `pre-init-gpio` 变成 `scl:1 sda:1`，再看设备表是否恢复到 0x15、0x3C、0x5A。
3. 启动日志确认 I2C 模式为 `poll`，不应再出现 OLED 写入阶段的 I2C DMA tx 日志。
4. 再执行 `AT+TP78OLEDON` 和 `AT+TP78OLED`，确认 SSD1306 能全亮和完整图片刷新。
5. 如果 `pre-init-gpio` 是 1/1 但 OLED 仍失败，再看 `I2C abort regs` 中的 `addr_nack/data_nack/sda_stuck/lost`。

编译命令在仓库根目录 D:\garfieldMiao\fbb_bs2x\src 执行：
ninja -C output/bs21e/acore/standard-bs21e-1100e
编译成功后打包：
C:\Users\GreatMagicianGarfiel\AppData\Local\Programs\Python\Python312\python.exe tools\pkg\packet.py bs21e standard-bs21e-1100e None bs21e-standard
生成的刷机包路径：
D:\garfieldMiao\fbb_bs2x\src\output\bs21e\fwpkg\standard-bs21e-1100e\bs21e_loadapp_only.fwpkg
我每次流程就是：先 ninja 编译，确认无错误，再跑 packet.py 打包。

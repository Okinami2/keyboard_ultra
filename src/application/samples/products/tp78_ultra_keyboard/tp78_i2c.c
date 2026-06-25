#include <stddef.h>
#include <string.h>

#include "errcode.h"
#include "gpio.h"
#include "i2c.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "tp78_board.h"
#include "tp78_i2c.h"

#define TP78_I2C_BUS I2C_BUS_0
#define TP78_I2C_BAUDRATE 100000
#define TP78_I2C_HSCODE 0
#define TP78_I2C_PROBE_ON_INIT 0
#define TP78_I2C_PROBE_BYTE 0x00
#define TP78_I2C_RECOVER_DELAY_MS 5
#define TP78_I2C_BUS_CLEAR_PULSES 18
#define TP78_I2C_BUS_CLEAR_DELAY_MS 1
#define TP78_I2C_WRITE_GAP_MS 1

#define TP78_SSD1306_CTRL_CMD 0x00
#define TP78_SSD1306_CMD_NOP 0xE3

static bool g_i2c_initialized;
static bool g_i2c_scanned;
static tp78_i2c_device_t g_i2c_devices[TP78_I2C_EXPECTED_DEVICE_COUNT];
static uint8_t g_i2c_device_count;

static const tp78_i2c_device_t g_tp78_i2c_known_devices[TP78_I2C_EXPECTED_DEVICE_COUNT] = {
    { TP78_I2C_ADDR_TRACKPOINT, TP78_I2C_DEVICE_TRACKPOINT },
    { TP78_I2C_ADDR_OLED, TP78_I2C_DEVICE_OLED },
    { TP78_I2C_ADDR_MPR121, TP78_I2C_DEVICE_MPR121 },
};

static void tp78_i2c_gpio_release(pin_t pin)
{
    (void)uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    (void)uapi_pin_set_pull(pin, PIN_PULL_NONE);
    (void)uapi_pin_set_ie(pin, PIN_IE_1);
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_INPUT);
}

static void tp78_i2c_gpio_drive_low(pin_t pin)
{
    (void)uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    (void)uapi_pin_set_pull(pin, PIN_PULL_NONE);
    (void)uapi_gpio_set_val(pin, GPIO_LEVEL_LOW);
    (void)uapi_gpio_set_dir(pin, GPIO_DIRECTION_OUTPUT);
}

static bool tp78_i2c_gpio_is_high(pin_t pin)
{
    return uapi_gpio_get_val(pin) == GPIO_LEVEL_HIGH;
}

static void tp78_i2c_bus_clear(void)
{
    tp78_i2c_gpio_release(TP78_I2C_SCL_PIN);
    tp78_i2c_gpio_release(TP78_I2C_SDA_PIN);
    osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);

    bool scl_high = tp78_i2c_gpio_is_high(TP78_I2C_SCL_PIN);
    bool sda_high = tp78_i2c_gpio_is_high(TP78_I2C_SDA_PIN);
    if (!scl_high || !sda_high) {
        osal_printk("[tp78] I2C bus before clear scl:%u sda:%u\r\n", scl_high ? 1U : 0U, sda_high ? 1U : 0U);
    }

    for (uint8_t i = 0; i < TP78_I2C_BUS_CLEAR_PULSES && !tp78_i2c_gpio_is_high(TP78_I2C_SDA_PIN); i++) {
        tp78_i2c_gpio_drive_low(TP78_I2C_SCL_PIN);
        osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);
        tp78_i2c_gpio_release(TP78_I2C_SCL_PIN);
        osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);
    }

    tp78_i2c_gpio_drive_low(TP78_I2C_SDA_PIN);
    osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);
    tp78_i2c_gpio_release(TP78_I2C_SCL_PIN);
    osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);
    tp78_i2c_gpio_release(TP78_I2C_SDA_PIN);
    osal_msleep(TP78_I2C_BUS_CLEAR_DELAY_MS);

    scl_high = tp78_i2c_gpio_is_high(TP78_I2C_SCL_PIN);
    sda_high = tp78_i2c_gpio_is_high(TP78_I2C_SDA_PIN);
    if (!scl_high || !sda_high) {
        osal_printk("[tp78] I2C bus still stuck scl:%u sda:%u\r\n", scl_high ? 1U : 0U, sda_high ? 1U : 0U);
    }
}

static void tp78_i2c_init_bus_pins(void)
{
    (void)uapi_pin_set_ie(TP78_I2C_SCL_PIN, PIN_IE_1);
    (void)uapi_pin_set_ie(TP78_I2C_SDA_PIN, PIN_IE_1);
    (void)uapi_pin_set_pull(TP78_I2C_SCL_PIN, PIN_PULL_NONE);
    (void)uapi_pin_set_pull(TP78_I2C_SDA_PIN, PIN_PULL_NONE);
    (void)uapi_pin_set_mode(TP78_I2C_SCL_PIN, HAL_PIO_I2C0_CLK);
    (void)uapi_pin_set_mode(TP78_I2C_SDA_PIN, HAL_PIO_I2C0_DATA);
}

static void tp78_i2c_init_pins(void)
{
    tp78_i2c_init_bus_pins();
    (void)uapi_pin_set_mode(TP78_TRACKPOINT_INT_PIN, HAL_PIO_FUNC_GPIO);
    (void)uapi_pin_set_pull(TP78_TRACKPOINT_INT_PIN, PIN_PULL_NONE);
    (void)uapi_pin_set_ie(TP78_TRACKPOINT_INT_PIN, PIN_IE_1);
    (void)uapi_gpio_set_dir(TP78_TRACKPOINT_INT_PIN, GPIO_DIRECTION_INPUT);
}

static void tp78_i2c_prepare_transfer(void)
{
    tp78_i2c_init_bus_pins();
}

static errcode_t tp78_i2c_master_start(void)
{
    if (g_i2c_initialized) {
        return ERRCODE_SUCC;
    }

    tp78_i2c_init_pins();
    i2c_port_clock_enable(TP78_I2C_BUS, true);
    errcode_t status = uapi_i2c_master_init(TP78_I2C_BUS, TP78_I2C_BAUDRATE, TP78_I2C_HSCODE);
    if (status == ERRCODE_I2C_ALREADY_INIT) {
        (void)uapi_i2c_deinit(TP78_I2C_BUS);
        status = uapi_i2c_master_init(TP78_I2C_BUS, TP78_I2C_BAUDRATE, TP78_I2C_HSCODE);
    }
    if (status == ERRCODE_SUCC) {
#if defined(CONFIG_I2C_SUPPORT_DMA) && (CONFIG_I2C_SUPPORT_DMA == 1)
        errcode_t dma_status = uapi_i2c_set_dma_mode(TP78_I2C_BUS, true);
        if (dma_status != ERRCODE_SUCC) {
            osal_printk("[tp78] I2C DMA mode failed:0x%x\r\n", (unsigned int)dma_status);
        } else {
            osal_printk("[tp78] I2C DMA mode enabled\r\n");
        }
#endif
        g_i2c_initialized = true;
    }
    return status;
}

static void tp78_i2c_master_stop(void)
{
    if (!g_i2c_initialized) {
        return;
    }
    (void)uapi_i2c_deinit(TP78_I2C_BUS);
    i2c_port_clock_enable(TP78_I2C_BUS, false);
    g_i2c_initialized = false;
}

static void tp78_i2c_recover(void)
{
    tp78_i2c_master_stop();
    osal_msleep(TP78_I2C_RECOVER_DELAY_MS);
    tp78_i2c_bus_clear();
    (void)tp78_i2c_master_start();
}

static void tp78_i2c_log_abort_source(void)
{
}

static const char *tp78_i2c_type_name(tp78_i2c_device_type_t type)
{
    switch (type) {
        case TP78_I2C_DEVICE_TRACKPOINT:
            return "trackpoint";
        case TP78_I2C_DEVICE_OLED:
            return "oled";
        case TP78_I2C_DEVICE_MPR121:
            return "mpr121";
        default:
            return "unknown";
    }
}

#if TP78_I2C_PROBE_ON_INIT
static bool tp78_i2c_probe(uint8_t address)
{
    if (tp78_i2c_master_start() != ERRCODE_SUCC) {
        return false;
    }

    uint8_t probe = TP78_I2C_PROBE_BYTE;
    uint8_t oled_probe[] = { TP78_SSD1306_CTRL_CMD, TP78_SSD1306_CMD_NOP };
    i2c_data_t data = {
        .send_buf = &probe,
        .send_len = sizeof(probe),
    };

    if (address == TP78_I2C_ADDR_OLED) {
        data.send_buf = oled_probe;
        data.send_len = sizeof(oled_probe);
    }

    errcode_t status = uapi_i2c_master_write(TP78_I2C_BUS, address, &data);
    if (status == ERRCODE_SUCC) {
        osal_msleep(1);
        return true;
    }

    tp78_i2c_recover();
    return false;
}
#endif

static void tp78_i2c_log_scan_result(void)
{
    bool has_oled = tp78_i2c_has_device(TP78_I2C_DEVICE_OLED);
    bool has_mpr121 = tp78_i2c_has_device(TP78_I2C_DEVICE_MPR121);
    bool has_trackpoint = tp78_i2c_has_device(TP78_I2C_DEVICE_TRACKPOINT);

    osal_printk("[tp78] I2C probe found %u device(s)\r\n", (unsigned int)g_i2c_device_count);
    for (uint8_t i = 0; i < g_i2c_device_count; i++) {
        osal_printk("[tp78] I2C device 0x%02x %s\r\n",
            (unsigned int)g_i2c_devices[i].address, tp78_i2c_type_name(g_i2c_devices[i].type));
    }

    if (g_i2c_device_count != TP78_I2C_EXPECTED_DEVICE_COUNT || !has_oled || !has_mpr121 || !has_trackpoint) {
        osal_printk("[tp78] I2C expected trackpoint/oled/mpr121, got count:%u tp:%u oled:%u mpr121:%u\r\n",
            (unsigned int)g_i2c_device_count, has_trackpoint ? 1U : 0U,
            has_oled ? 1U : 0U, has_mpr121 ? 1U : 0U);
    }
}

static void tp78_i2c_scan_bus(void)
{
    g_i2c_device_count = 0;
    (void)memset(g_i2c_devices, 0, sizeof(g_i2c_devices));

#if TP78_I2C_PROBE_ON_INIT
    for (uint8_t i = 0; i < TP78_I2C_EXPECTED_DEVICE_COUNT; i++) {
        if (!tp78_i2c_probe(g_tp78_i2c_known_devices[i].address)) {
            continue;
        }
        g_i2c_devices[g_i2c_device_count] = g_tp78_i2c_known_devices[i];
        g_i2c_device_count++;
    }
#else
    for (uint8_t i = 0; i < TP78_I2C_EXPECTED_DEVICE_COUNT; i++) {
        g_i2c_devices[g_i2c_device_count] = g_tp78_i2c_known_devices[i];
        g_i2c_device_count++;
    }
    osal_printk("[tp78] I2C boot probe disabled, using expected device table\r\n");
#endif

    g_i2c_scanned = true;
    tp78_i2c_log_scan_result();
}

int32_t tp78_i2c_init(void)
{
    errcode_t status = tp78_i2c_master_start();
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] I2C init failed:0x%x\r\n", (unsigned int)status);
        return -1;
    }

    if (!g_i2c_scanned) {
        tp78_i2c_scan_bus();
    }
    return 0;
}

int32_t tp78_i2c_reset(void)
{
    tp78_i2c_master_stop();
    osal_msleep(TP78_I2C_RECOVER_DELAY_MS);
    return tp78_i2c_master_start() == ERRCODE_SUCC ? 0 : -1;
}

uint8_t tp78_i2c_get_device_count(void)
{
    return g_i2c_device_count;
}

uint8_t tp78_i2c_copy_devices(tp78_i2c_device_t *devices, uint8_t max_devices)
{
    if (devices == NULL || max_devices == 0) {
        return 0;
    }

    uint8_t count = g_i2c_device_count < max_devices ? g_i2c_device_count : max_devices;
    (void)memcpy(devices, g_i2c_devices, count * sizeof(g_i2c_devices[0]));
    return count;
}

bool tp78_i2c_device_present(uint8_t address)
{
    for (uint8_t i = 0; i < g_i2c_device_count; i++) {
        if (g_i2c_devices[i].address == address) {
            return true;
        }
    }
    return false;
}

bool tp78_i2c_has_device(tp78_i2c_device_type_t type)
{
    for (uint8_t i = 0; i < g_i2c_device_count; i++) {
        if (g_i2c_devices[i].type == type) {
            return true;
        }
    }
    return false;
}

bool tp78_i2c_trackpoint_irq_active(void)
{
    return uapi_gpio_get_val(TP78_TRACKPOINT_INT_PIN) == GPIO_LEVEL_LOW;
}

int32_t tp78_i2c_write(uint8_t address, const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0 || tp78_i2c_master_start() != ERRCODE_SUCC) {
        return -1;
    }
    tp78_i2c_prepare_transfer();

    i2c_data_t i2c_data = {
        .send_buf = (uint8_t *)data,
        .send_len = length,
    };
    errcode_t status = uapi_i2c_master_write(TP78_I2C_BUS, address, &i2c_data);
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] I2C write 0x%02x len:%u failed:0x%x scl:%u sda:%u\r\n",
            (unsigned int)address, (unsigned int)length, (unsigned int)status,
            tp78_i2c_gpio_is_high(TP78_I2C_SCL_PIN) ? 1U : 0U,
            tp78_i2c_gpio_is_high(TP78_I2C_SDA_PIN) ? 1U : 0U);
        tp78_i2c_log_abort_source();
        tp78_i2c_recover();
        return -1;
    }
    osal_msleep(TP78_I2C_WRITE_GAP_MS);
    return 0;
}

int32_t tp78_i2c_read(uint8_t address, uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0 || tp78_i2c_master_start() != ERRCODE_SUCC) {
        return -1;
    }
    tp78_i2c_prepare_transfer();

    i2c_data_t i2c_data = {
        .receive_buf = data,
        .receive_len = length,
    };
    errcode_t status = uapi_i2c_master_read(TP78_I2C_BUS, address, &i2c_data);
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] I2C read 0x%02x failed:0x%x\r\n", (unsigned int)address, (unsigned int)status);
        tp78_i2c_log_abort_source();
        tp78_i2c_recover();
        return -1;
    }
    return 0;
}

int32_t tp78_i2c_write_read(uint8_t address, const uint8_t *tx_data, uint32_t tx_length,
    uint8_t *rx_data, uint32_t rx_length)
{
    if (tx_data == NULL || tx_length == 0 || rx_data == NULL || rx_length == 0 ||
        tp78_i2c_master_start() != ERRCODE_SUCC) {
        return -1;
    }
    tp78_i2c_prepare_transfer();

    i2c_data_t i2c_data = {
        .send_buf = (uint8_t *)tx_data,
        .send_len = tx_length,
        .receive_buf = rx_data,
        .receive_len = rx_length,
    };
    errcode_t status = uapi_i2c_master_writeread(TP78_I2C_BUS, address, &i2c_data);
    if (status != ERRCODE_SUCC) {
        osal_printk("[tp78] I2C write-read 0x%02x failed:0x%x\r\n", (unsigned int)address,
            (unsigned int)status);
        tp78_i2c_log_abort_source();
        tp78_i2c_recover();
        return -1;
    }
    return 0;
}

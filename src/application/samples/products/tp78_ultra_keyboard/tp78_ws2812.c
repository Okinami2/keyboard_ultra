#include "common_def.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "spi.h"
#include "tcxo.h"
#include "tp78_board.h"
#include "tp78_ws2812.h"

#define TP78_WS2812_LED_COUNT 83U
#define TP78_WS2812_BITS_PER_LED 24U
#define TP78_WS2812_BUFFER_SIZE (TP78_WS2812_LED_COUNT * TP78_WS2812_BITS_PER_LED)
#define TP78_WS2812_SPI_BUS 0U
#define TP78_WS2812_SPI_BUS_CLOCK 32000000U
#define TP78_WS2812_SPI_FREQUENCY 8U
#define TP78_WS2812_SPI_ZERO 0xC0U
#define TP78_WS2812_SPI_ONE 0xF8U
#define TP78_WS2812_SPI_TIMEOUT 0xFFFFFFFFU

static uint8_t g_ws2812_buffer[TP78_WS2812_BUFFER_SIZE] __attribute__((aligned(4)));
static bool g_ws2812_ready;

static uint16_t tp78_ws2812_encode_byte(uint8_t value, uint8_t *output)
{
    for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
        *output++ = (value & mask) != 0U ? TP78_WS2812_SPI_ONE : TP78_WS2812_SPI_ZERO;
    }
    return 8U;
}

void tp78_ws2812_init(void)
{
    spi_attr_t config = { 0 };
    spi_extra_attr_t extra_config = { 0 };

    (void)uapi_pin_set_mode(TP78_WS2812_PIN, (pin_mode_t)HAL_PIO_SPI0_TXD);
    (void)uapi_pin_set_pull(TP78_WS2812_PIN, PIN_PULL_NONE);
    (void)uapi_pin_set_ds(TP78_WS2812_PIN, PIN_DS_3);

    config.is_slave = false;
    config.slave_num = 1U;
    config.bus_clk = TP78_WS2812_SPI_BUS_CLOCK;
    config.freq_mhz = TP78_WS2812_SPI_FREQUENCY;
    config.clk_polarity = 0U;
    config.clk_phase = 0U;
    config.frame_format = 0U;
    config.spi_frame_format = HAL_SPI_FRAME_FORMAT_STANDARD;
    config.frame_size = HAL_SPI_FRAME_SIZE_32;
    config.tmod = HAL_SPI_TRANS_MODE_TXRX;
    config.sste = 1U;
    extra_config.qspi_param.wait_cycles = 0x10U;

    errcode_t ret = uapi_spi_init(TP78_WS2812_SPI_BUS, &config, &extra_config);
    if (ret == ERRCODE_SUCC) {
        g_ws2812_ready = true;
        osal_printk("[tp78] WS2812 SPI ready\r\n");
    } else {
        osal_printk("[tp78] WS2812 SPI init failed: 0x%x\r\n", ret);
    }
}

void tp78_ws2812_write(const tp78_rgb_color_t *pixels, uint16_t count)
{
    if (!g_ws2812_ready || pixels == NULL || count == 0U) {
        return;
    }

    if (count > TP78_WS2812_LED_COUNT) {
        count = TP78_WS2812_LED_COUNT;
    }

    uint16_t offset = 0U;
    for (uint16_t pixel = 0U; pixel < count; pixel++) {
        offset += tp78_ws2812_encode_byte(pixels[pixel].green, &g_ws2812_buffer[offset]);
        offset += tp78_ws2812_encode_byte(pixels[pixel].red, &g_ws2812_buffer[offset]);
        offset += tp78_ws2812_encode_byte(pixels[pixel].blue, &g_ws2812_buffer[offset]);
    }

    spi_xfer_data_t transfer = {
        .tx_buff = g_ws2812_buffer,
        .tx_bytes = offset,
        .rx_buff = NULL,
        .rx_bytes = 0U
    };
    errcode_t ret = uapi_spi_master_write(TP78_WS2812_SPI_BUS, &transfer, TP78_WS2812_SPI_TIMEOUT);
    if (ret != ERRCODE_SUCC) {
        osal_printk("[tp78] WS2812 SPI write failed: 0x%x\r\n", ret);
    }

    (void)uapi_tcxo_delay_us(80U);
}

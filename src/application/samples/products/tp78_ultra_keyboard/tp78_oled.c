#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "soc_osal.h"
#include "tp78_i2c.h"
#include "tp78_oled.h"
#include "tp78_oled_assets.h"

#define TP78_OLED_WIDTH 64
#define TP78_OLED_HEIGHT 48
#define TP78_OLED_PAGES (TP78_OLED_HEIGHT / 8)
#define TP78_OLED_COLUMN_OFFSET 0
#define TP78_OLED_PAGE_OFFSET 0
#define TP78_OLED_CTRL_CMD 0x00
#define TP78_OLED_CTRL_DATA 0x40
#define TP78_OLED_CMD_NOP 0xE3
#define TP78_OLED_MIN_DMA_WRITE_LEN 8
#define TP78_OLED_DATA_CHUNK 8
#define TP78_OLED_DEBUG_DATA_MAX 16
#define TP78_OLED_COMMAND_DELAY_MS 1
#define TP78_OLED_PAGE_YIELD_MS 1
#define TP78_OLED_INIT_DELAY_MS 20

static bool g_oled_initialized;
static uint8_t g_oled_next_art;
static uint8_t g_oled_framebuffer[TP78_OLED_PAGES][TP78_OLED_WIDTH];

static void tp78_oled_invalidate(void)
{
    g_oled_initialized = false;
}

static int32_t tp78_oled_send_commands(const uint8_t *commands, uint32_t length)
{
    uint8_t buffer[32];
    uint32_t offset = 0;

    if (commands == NULL || length == 0 || length + 1 > sizeof(buffer)) {
        return -1;
    }

    while (offset < length) {
        uint32_t chunk = length - offset;
        uint32_t write_len;

        if (chunk > (TP78_OLED_MIN_DMA_WRITE_LEN - 1)) {
            chunk = TP78_OLED_MIN_DMA_WRITE_LEN - 1;
        }

        buffer[0] = TP78_OLED_CTRL_CMD;
        for (uint32_t i = 0; i < chunk; i++) {
            buffer[i + 1] = commands[offset + i];
        }
        write_len = chunk + 1;
        while (write_len < TP78_OLED_MIN_DMA_WRITE_LEN) {
            buffer[write_len++] = TP78_OLED_CMD_NOP;
        }

        if (tp78_i2c_write(TP78_I2C_ADDR_OLED, buffer, write_len) != 0) {
            return -1;
        }

        offset += chunk;
        osal_msleep(TP78_OLED_COMMAND_DELAY_MS);
    }
    return 0;
}

static int32_t tp78_oled_send_command(uint8_t command)
{
    const uint8_t commands[] = { command };

    return tp78_oled_send_commands(commands, sizeof(commands));
}

static int32_t tp78_oled_set_window(uint8_t x, uint8_t page, uint8_t width, uint8_t pages)
{
    uint8_t controller_x_start;
    uint8_t controller_x_end;
    uint8_t controller_page_start;
    uint8_t controller_page_end;

    if (width == 0 || pages == 0 || x >= TP78_OLED_WIDTH || page >= TP78_OLED_PAGES ||
        (uint16_t)x + width > TP78_OLED_WIDTH || (uint16_t)page + pages > TP78_OLED_PAGES) {
        return -1;
    }

    controller_x_start = (uint8_t)(x + TP78_OLED_COLUMN_OFFSET);
    controller_x_end = (uint8_t)(controller_x_start + width - 1);
    controller_page_start = (uint8_t)(page + TP78_OLED_PAGE_OFFSET);
    controller_page_end = (uint8_t)(controller_page_start + pages - 1);

    const uint8_t commands[] = {
        0x21, controller_x_start, controller_x_end,
        0x22, controller_page_start, controller_page_end,
    };
    if (tp78_oled_send_commands(commands, sizeof(commands)) != 0) {
        osal_printk("[tp78] OLED window failed x:%u page:%u width:%u pages:%u col_offset:%u page_offset:%u\r\n",
            (unsigned int)x, (unsigned int)page, (unsigned int)width, (unsigned int)pages,
            (unsigned int)TP78_OLED_COLUMN_OFFSET, (unsigned int)TP78_OLED_PAGE_OFFSET);
        return -1;
    }

    return 0;
}

static int32_t tp78_oled_set_cursor(uint8_t x, uint8_t page)
{
    return tp78_oled_set_window(x, page, (uint8_t)(TP78_OLED_WIDTH - x), 1);
}

static int32_t tp78_oled_write_chunk(const uint8_t *data, uint8_t length)
{
    uint8_t buffer[1 + TP78_OLED_DATA_CHUNK];
    uint8_t write_len;

    if (data == NULL || length == 0 || length > TP78_OLED_DATA_CHUNK) {
        return -1;
    }

    buffer[0] = TP78_OLED_CTRL_DATA;
    for (uint8_t i = 0; i < length; i++) {
        buffer[i + 1] = data[i];
    }
    write_len = (uint8_t)(length + 1);
    while (write_len < TP78_OLED_MIN_DMA_WRITE_LEN) {
        buffer[write_len++] = 0x00;
    }

    if (tp78_i2c_write(TP78_I2C_ADDR_OLED, buffer, write_len) != 0) {
        return -1;
    }

    return 0;
}

int32_t tp78_oled_init(void)
{
    static const uint8_t init_commands[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x2F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF,
    };

    if (g_oled_initialized) {
        return 0;
    }

    if (tp78_i2c_init() != 0 || !tp78_i2c_has_device(TP78_I2C_DEVICE_OLED)) {
        return -1;
    }

    if (tp78_oled_send_commands(init_commands, sizeof(init_commands)) != 0) {
        return -1;
    }

    osal_msleep(TP78_OLED_INIT_DELAY_MS);
    g_oled_initialized = true;
    return 0;
}

int32_t tp78_oled_clear(void)
{
    static const uint8_t zeros[TP78_OLED_DATA_CHUNK] = { 0 };

    if (tp78_oled_init() != 0) {
        return -1;
    }

    if (tp78_oled_set_window(0, 0, TP78_OLED_WIDTH, TP78_OLED_PAGES) != 0) {
        tp78_oled_invalidate();
        return -1;
    }

    for (uint8_t page = 0; page < TP78_OLED_PAGES; page++) {
        for (uint8_t x = 0; x < TP78_OLED_WIDTH; x += TP78_OLED_DATA_CHUNK) {
            if (tp78_oled_write_chunk(zeros, TP78_OLED_DATA_CHUNK) != 0) {
                tp78_oled_invalidate();
                return -1;
            }
        }
        osal_msleep(TP78_OLED_PAGE_YIELD_MS);
    }

    return 0;
}

static int32_t tp78_oled_unpack_frame(const tp78_oled_frame_t *frame)
{
    const uint8_t *data;
    uint16_t offset;
    uint8_t regions;

    if (frame == NULL || frame->data == NULL || frame->length == 0) {
        return -1;
    }

    data = frame->data;
    offset = 0;
    regions = data[offset++];
    for (uint8_t page = 0; page < TP78_OLED_PAGES; page++) {
        for (uint8_t x = 0; x < TP78_OLED_WIDTH; x++) {
            g_oled_framebuffer[page][x] = 0;
        }
    }

    for (uint8_t i = 0; i < regions; i++) {
        uint8_t x;
        uint8_t page;
        uint8_t width;

        if ((uint16_t)(offset + 3) > frame->length) {
            return -1;
        }

        x = data[offset++];
        page = data[offset++];
        width = data[offset++];

        if (width == 0 || (uint16_t)(offset + width) > frame->length) {
            return -1;
        }
        if (x >= TP78_OLED_WIDTH || page >= TP78_OLED_PAGES ||
            (uint16_t)x + width > TP78_OLED_WIDTH) {
            return -1;
        }
        for (uint8_t column = 0; column < width; column++) {
            g_oled_framebuffer[page][x + column] = data[offset + column];
        }
        offset = (uint16_t)(offset + width);
    }

    return 0;
}

static int32_t tp78_oled_flush_framebuffer(void)
{
    if (tp78_oled_set_window(0, 0, TP78_OLED_WIDTH, TP78_OLED_PAGES) != 0) {
        osal_printk("[tp78] OLED flush window failed col_offset:%u page_offset:%u\r\n",
            (unsigned int)TP78_OLED_COLUMN_OFFSET, (unsigned int)TP78_OLED_PAGE_OFFSET);
        return -1;
    }

    for (uint8_t page = 0; page < TP78_OLED_PAGES; page++) {
        for (uint8_t x = 0; x < TP78_OLED_WIDTH; x += TP78_OLED_DATA_CHUNK) {
            if (tp78_oled_write_chunk(&g_oled_framebuffer[page][x], TP78_OLED_DATA_CHUNK) != 0) {
                osal_printk("[tp78] OLED flush failed page:%u x:%u len:%u col_offset:%u page_offset:%u\r\n",
                    (unsigned int)page, (unsigned int)x, (unsigned int)TP78_OLED_DATA_CHUNK,
                    (unsigned int)TP78_OLED_COLUMN_OFFSET, (unsigned int)TP78_OLED_PAGE_OFFSET);
                return -1;
            }
        }
        osal_msleep(TP78_OLED_PAGE_YIELD_MS);
    }

    return 0;
}

int32_t tp78_oled_show_art(uint8_t index)
{
    if (g_tp78_oled_frame_count == 0 || index >= g_tp78_oled_frame_count) {
        return -1;
    }

    if (tp78_oled_init() != 0 ||
        tp78_oled_send_command(0xA4) != 0 ||
        tp78_oled_unpack_frame(&g_tp78_oled_frames[index]) != 0 ||
        tp78_oled_flush_framebuffer() != 0) {
        tp78_oled_invalidate();
        return -1;
    }

    return 0;
}

int32_t tp78_oled_show_next_art(void)
{
    uint8_t index;

    if (g_tp78_oled_frame_count == 0) {
        return -1;
    }

    index = g_oled_next_art;
    if (index >= g_tp78_oled_frame_count) {
        index = 0;
    }

    if (tp78_oled_show_art(index) != 0) {
        return -1;
    }

    g_oled_next_art = (uint8_t)(index + 1);
    if (g_oled_next_art >= g_tp78_oled_frame_count) {
        g_oled_next_art = 0;
    }
    return 0;
}

int32_t tp78_oled_debug_all_on(void)
{
    if (tp78_oled_init() != 0 ||
        tp78_oled_send_command(0xA5) != 0) {
        tp78_oled_invalidate();
        return -1;
    }

    return 0;
}

int32_t tp78_oled_debug_cursor(void)
{
    if (tp78_oled_init() != 0 ||
        tp78_oled_send_command(0xA4) != 0 ||
        tp78_oled_set_cursor(0, 0) != 0) {
        osal_printk("[tp78] OLED debug cursor failed col_offset:%u\r\n",
            (unsigned int)TP78_OLED_COLUMN_OFFSET);
        tp78_oled_invalidate();
        return -1;
    }

    return 0;
}

int32_t tp78_oled_debug_write_data(uint8_t length)
{
    uint8_t data[TP78_OLED_DEBUG_DATA_MAX];

    if (length == 0 || length > TP78_OLED_DEBUG_DATA_MAX) {
        return -1;
    }

    for (uint8_t i = 0; i < length; i++) {
        data[i] = 0xFF;
    }

    if (tp78_oled_init() != 0 ||
        tp78_oled_send_command(0xA4) != 0 ||
        tp78_oled_set_window(0, 0, length, 1) != 0) {
        osal_printk("[tp78] OLED debug data failed len:%u col_offset:%u\r\n",
            (unsigned int)length, (unsigned int)TP78_OLED_COLUMN_OFFSET);
        tp78_oled_invalidate();
        return -1;
    }

    for (uint8_t offset = 0; offset < length; offset = (uint8_t)(offset + TP78_OLED_DATA_CHUNK)) {
        uint8_t chunk = (uint8_t)(length - offset);
        if (chunk > TP78_OLED_DATA_CHUNK) {
            chunk = TP78_OLED_DATA_CHUNK;
        }
        if (tp78_oled_write_chunk(&data[offset], chunk) != 0) {
            osal_printk("[tp78] OLED debug data failed len:%u offset:%u chunk:%u col_offset:%u\r\n",
                (unsigned int)length, (unsigned int)offset, (unsigned int)chunk,
                (unsigned int)TP78_OLED_COLUMN_OFFSET);
            tp78_oled_invalidate();
            return -1;
        }
    }

    return 0;
}

uint8_t tp78_oled_get_art_count(void)
{
    return g_tp78_oled_frame_count;
}

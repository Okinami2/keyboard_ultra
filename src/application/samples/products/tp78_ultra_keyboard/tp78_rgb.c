#include <stdbool.h>
#include <string.h>
#include "common_def.h"
#include "soc_osal.h"
#include "tp78_board.h"
#include "tp78_rgb.h"
#include "tp78_ws2812.h"

#define TP78_LED_COUNT 83U
#define TP78_NO_LED 0xFFU
#define TP78_RGB_TASK_PRIORITY 28
#define TP78_RGB_TASK_STACK_SIZE 0x1000
#define TP78_RGB_FRAME_MS 32U
#define TP78_RGB_BRIGHTNESS_STEP 24U
#define TP78_RGB_DEFAULT_BRIGHTNESS 32U
#define TP78_RGB_MIN_BRIGHTNESS 8U
#define TP78_RGB_SPEED_MIN 1U
#define TP78_RGB_SPEED_MAX 8U

static const uint8_t g_key_to_led[TP78_MATRIX_ROWS][TP78_MATRIX_COLS] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 },
    { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 },
    { 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41 },
    { 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55 },
    { 56, TP78_NO_LED, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68 },
    { 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82 }
};

static tp78_rgb_color_t g_pixels[TP78_LED_COUNT];
static volatile uint8_t g_reactive[TP78_LED_COUNT];
static volatile tp78_rgb_effect_t g_effect = TP78_RGB_EFFECT_RAINBOW;
static volatile uint8_t g_brightness = TP78_RGB_DEFAULT_BRIGHTNESS;
static volatile uint8_t g_speed = 3U;
static volatile uint32_t g_frame_count;
static uint16_t g_phase;

static uint8_t tp78_scale(uint8_t value, uint8_t scale)
{
    return (uint8_t)(((uint16_t)value * ((uint16_t)scale + 1U)) >> 8U);
}

static tp78_rgb_color_t tp78_color_scale(tp78_rgb_color_t color, uint8_t scale)
{
    color.red = tp78_scale(color.red, scale);
    color.green = tp78_scale(color.green, scale);
    color.blue = tp78_scale(color.blue, scale);
    return color;
}

static tp78_rgb_color_t tp78_color_wheel(uint8_t position)
{
    tp78_rgb_color_t color;

    if (position < 85U) {
        color.red = (uint8_t)(255U - position * 3U);
        color.green = (uint8_t)(position * 3U);
        color.blue = 0U;
    } else if (position < 170U) {
        position = (uint8_t)(position - 85U);
        color.red = 0U;
        color.green = (uint8_t)(255U - position * 3U);
        color.blue = (uint8_t)(position * 3U);
    } else {
        position = (uint8_t)(position - 170U);
        color.red = (uint8_t)(position * 3U);
        color.green = 0U;
        color.blue = (uint8_t)(255U - position * 3U);
    }
    return color;
}

static void tp78_fill(tp78_rgb_color_t color)
{
    for (uint16_t i = 0; i < TP78_LED_COUNT; i++) {
        g_pixels[i] = color;
    }
}

static uint8_t tp78_triangle(uint8_t phase)
{
    return phase < 128U ? (uint8_t)(phase * 2U) : (uint8_t)((255U - phase) * 2U);
}

static void tp78_render_static(void)
{
    tp78_fill((tp78_rgb_color_t){ 80U, 180U, 255U });
}

static void tp78_render_breath(void)
{
    uint8_t level = tp78_triangle((uint8_t)g_phase);
    tp78_fill(tp78_color_scale((tp78_rgb_color_t){ 70U, 40U, 255U }, level));
}

static void tp78_render_waterfall(void)
{
    const uint8_t head = (uint8_t)((g_phase >> 2U) % TP78_MATRIX_COLS);
    (void)memset(g_pixels, 0, sizeof(g_pixels));

    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
            uint8_t led = g_key_to_led[row][col];
            if (led == TP78_NO_LED) {
                continue;
            }
            uint8_t distance = (uint8_t)((col + TP78_MATRIX_COLS - head) % TP78_MATRIX_COLS);
            uint8_t level = distance < 6U ? (uint8_t)(255U - distance * 42U) : 0U;
            g_pixels[led] = tp78_color_scale(tp78_color_wheel((uint8_t)(g_phase + row * 18U)), level);
        }
    }
}

static void tp78_render_reactive(void)
{
    for (uint16_t i = 0; i < TP78_LED_COUNT; i++) {
        uint8_t level = g_reactive[i];
        g_pixels[i] = tp78_color_scale(tp78_color_wheel((uint8_t)(i * 7U + g_phase)), level);
        g_reactive[i] = level > 12U ? (uint8_t)(level - 12U) : 0U;
    }
}

static void tp78_render_rainbow(void)
{
    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
            uint8_t led = g_key_to_led[row][col];
            if (led != TP78_NO_LED) {
                g_pixels[led] = tp78_color_wheel((uint8_t)(g_phase + col * 13U + row * 7U));
            }
        }
    }
}

static void tp78_render_frame(void)
{
    tp78_rgb_effect_t effect = g_effect;

    switch (effect) {
        case TP78_RGB_EFFECT_STATIC:
            tp78_render_static();
            break;
        case TP78_RGB_EFFECT_BREATH:
            tp78_render_breath();
            break;
        case TP78_RGB_EFFECT_WATERFALL:
            tp78_render_waterfall();
            break;
        case TP78_RGB_EFFECT_REACTIVE:
            tp78_render_reactive();
            break;
        case TP78_RGB_EFFECT_RAINBOW:
            tp78_render_rainbow();
            break;
        case TP78_RGB_EFFECT_OFF:
        default:
            (void)memset(g_pixels, 0, sizeof(g_pixels));
            break;
    }

    uint8_t brightness = g_brightness;
    for (uint16_t i = 0; i < TP78_LED_COUNT; i++) {
        g_pixels[i] = tp78_color_scale(g_pixels[i], brightness);
    }
    g_phase = (uint16_t)(g_phase + g_speed);
}

static int tp78_rgb_task(void *arg)
{
    unused(arg);
    osal_printk("[tp78] RGB task started\r\n");

    while (true) {
        tp78_render_frame();
        tp78_ws2812_write(g_pixels, TP78_LED_COUNT);
        g_frame_count++;
        osal_msleep(TP78_RGB_FRAME_MS);
    }

    return 0;
}

void tp78_rgb_init(void)
{
    osal_task *task = NULL;

    (void)memset((void *)g_reactive, 0, sizeof(g_reactive));
    tp78_ws2812_init();

    osal_kthread_lock();
    task = osal_kthread_create(tp78_rgb_task, NULL,
        "Tp78RgbTask", TP78_RGB_TASK_STACK_SIZE);
    if (task != NULL) {
        osal_kthread_set_priority(task, TP78_RGB_TASK_PRIORITY);
    } else {
        osal_printk("[tp78] RGB task creation failed\r\n");
    }
    osal_kthread_unlock();
}

void tp78_rgb_key_event(uint8_t row, uint8_t col)
{
    if (g_effect != TP78_RGB_EFFECT_REACTIVE ||
        row >= TP78_MATRIX_ROWS || col >= TP78_MATRIX_COLS) {
        return;
    }

    uint8_t led = g_key_to_led[row][col];
    if (led != TP78_NO_LED) {
        g_reactive[led] = 255U;
    }
}

void tp78_rgb_set_effect(tp78_rgb_effect_t effect)
{
    if (effect >= TP78_RGB_EFFECT_COUNT) {
        return;
    }
    g_effect = effect;
    g_phase = 0U;
    osal_printk("[tp78] RGB effect %u, frames %u\r\n",
        (uint8_t)effect, g_frame_count);
}

void tp78_rgb_adjust_brightness(int8_t direction)
{
    uint8_t brightness = g_brightness;

    if (direction > 0) {
        brightness = brightness > (255U - TP78_RGB_BRIGHTNESS_STEP) ?
            255U : (uint8_t)(brightness + TP78_RGB_BRIGHTNESS_STEP);
    } else if (direction < 0) {
        brightness = brightness <= (TP78_RGB_MIN_BRIGHTNESS + TP78_RGB_BRIGHTNESS_STEP) ?
            TP78_RGB_MIN_BRIGHTNESS : (uint8_t)(brightness - TP78_RGB_BRIGHTNESS_STEP);
    }
    g_brightness = brightness;
    osal_printk("[tp78] RGB brightness %u\r\n", brightness);
}

void tp78_rgb_adjust_speed(int8_t direction)
{
    uint8_t speed = g_speed;

    if (direction > 0 && speed < TP78_RGB_SPEED_MAX) {
        speed++;
    } else if (direction < 0 && speed > TP78_RGB_SPEED_MIN) {
        speed--;
    }
    g_speed = speed;
    osal_printk("[tp78] RGB speed %u\r\n", speed);
}

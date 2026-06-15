#include <stdbool.h>
#include <string.h>
#include "cpu_utils.h"
#include "soc_osal.h"
#include "tcxo.h"
#include "tp78_keycodes.h"
#include "tp78_keymap.h"
#include "tp78_matrix.h"
#include "tp78_rgb.h"
#include "tp78_transport.h"
#include "tp78_keyboard.h"

#define TP78_RESET_HOLD_MS 2000
#define TP78_PAIR_HOLD_MS 2000

static bool g_previous[TP78_MATRIX_ROWS][TP78_MATRIX_COLS];
static bool g_caps_used;
static bool g_caps_tap_pending;
static uint64_t g_reset_started;
static uint8_t g_last_modifiers;
static uint8_t g_last_keys[6];
static uint8_t g_last_mouse_buttons;
static uint16_t g_last_consumer;
static tp78_transport_mode_t g_last_mode;
static bool g_last_transport_ready;
static uint64_t g_pair_started;
static tp78_transport_mode_t g_pair_mode;
static bool g_pair_triggered;

static bool tp78_pressed(uint8_t row, uint8_t col)
{
    return tp78_matrix_is_pressed(row, col);
}

static bool tp78_fn_pressed(void)
{
    return tp78_pressed(5, 6);
}

static bool tp78_caps_pressed(void)
{
    return tp78_pressed(3, 0);
}

static bool tp78_rising(uint8_t row, uint8_t col)
{
    return tp78_pressed(row, col) && !g_previous[row][col];
}

static void tp78_handle_fn(bool fn)
{
    uint16_t consumer = 0;
    bool reset_combo = false;

    if (fn) {
        if (tp78_rising(0, 10)) {
            tp78_transport_set_mode(TP78_TRANSPORT_USB);
        } else if (tp78_rising(0, 11)) {
            tp78_transport_set_mode(TP78_TRANSPORT_BLE);
        } else if (tp78_rising(0, 12)) {
            tp78_transport_set_mode(TP78_TRANSPORT_SLE);
        }
        for (uint8_t slot = 0; slot < 4; slot++) {
            if (tp78_rising(1, (uint8_t)(slot + 1U))) {
                tp78_transport_select_ble_slot(slot);
                break;
            }
        }

        tp78_transport_mode_t pair_mode = TP78_TRANSPORT_USB;
        if (tp78_pressed(0, 11)) {
            pair_mode = TP78_TRANSPORT_BLE;
        } else if (tp78_pressed(0, 12)) {
            pair_mode = TP78_TRANSPORT_SLE;
        }
        if (pair_mode == TP78_TRANSPORT_USB) {
            g_pair_started = 0;
            g_pair_triggered = false;
        } else if (g_pair_started == 0 || g_pair_mode != pair_mode) {
            g_pair_mode = pair_mode;
            g_pair_started = uapi_tcxo_get_ms();
            g_pair_triggered = false;
        } else if (!g_pair_triggered &&
            uapi_tcxo_get_ms() - g_pair_started >= TP78_PAIR_HOLD_MS) {
            tp78_transport_start_pairing(pair_mode);
            g_pair_triggered = true;
        }
        if (tp78_pressed(1, 11)) {
            consumer = TP78_CONSUMER_VOLUME_DOWN;
        } else if (tp78_pressed(1, 12)) {
            consumer = TP78_CONSUMER_VOLUME_UP;
        }

        reset_combo = tp78_pressed(2, 4);
        for (uint8_t style = 1; style <= 6; style++) {
            if (tp78_rising(0, style)) {
                tp78_rgb_set_effect((tp78_rgb_effect_t)(style - 1U));
                break;
            }
        }
        if (tp78_rising(5, 11)) {
            tp78_rgb_adjust_brightness(1);
        } else if (tp78_rising(5, 9)) {
            tp78_rgb_adjust_brightness(-1);
        }
        if (tp78_rising(5, 10)) {
            tp78_rgb_adjust_speed(1);
        } else if (tp78_rising(5, 8)) {
            tp78_rgb_adjust_speed(-1);
        }
        if (tp78_rising(2, 5)) {
            osal_printk("[tp78] TrackPoint toggle requested\r\n");
        }
    } else {
        g_pair_started = 0;
        g_pair_triggered = false;
    }

    if (consumer != g_last_consumer) {
        if (tp78_transport_consumer_send(consumer) == 0) {
            g_last_consumer = consumer;
        }
    }

    if (!reset_combo) {
        g_reset_started = 0;
    } else if (g_reset_started == 0) {
        g_reset_started = uapi_tcxo_get_ms();
        osal_printk("[tp78] hold Fn+R for 2 seconds to reboot\r\n");
    } else if (uapi_tcxo_get_ms() - g_reset_started >= TP78_RESET_HOLD_MS) {
        cpu_utils_reset_chip_with_cause(REBOOT_CAUSE_APPLICATION_SYSRESETREQ);
    }
}

static void tp78_add_key(uint8_t key, uint8_t *modifiers, uint8_t keys[6], uint8_t *mouse_buttons)
{
    if (key >= TP78_KEY_LEFT_CTRL && key <= TP78_KEY_RIGHT_GUI) {
        *modifiers |= (uint8_t)(1U << (key - TP78_KEY_LEFT_CTRL));
        return;
    }
    if (key >= TP78_KEY_MOUSE_LEFT && key <= TP78_KEY_MOUSE_MIDDLE) {
        *mouse_buttons |= (uint8_t)(1U << (key - TP78_KEY_MOUSE_LEFT));
        return;
    }
    if (key == TP78_KEY_NONE || key == TP78_KEY_FN) {
        return;
    }

    for (uint8_t i = 0; i < 6; i++) {
        if (keys[i] == key) {
            return;
        }
        if (keys[i] == TP78_KEY_NONE) {
            keys[i] = key;
            return;
        }
    }
}

static void tp78_save_matrix(void)
{
    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
            g_previous[row][col] = tp78_pressed(row, col);
        }
    }
}

void tp78_keyboard_init(void)
{
    (void)memset(g_previous, 0, sizeof(g_previous));
    (void)memset(g_last_keys, 0, sizeof(g_last_keys));
    g_last_mode = tp78_transport_get_mode();
    g_last_transport_ready = tp78_transport_is_ready();
    tp78_matrix_init();
}

void tp78_keyboard_process(void)
{
    uint8_t modifiers = 0;
    uint8_t keys[6] = { 0 };
    uint8_t mouse_buttons = 0;
    bool caps = tp78_caps_pressed();
    bool fn = tp78_fn_pressed();

    if (caps && !g_previous[3][0]) {
        g_caps_used = false;
    }
    if (caps) {
        for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
            for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
                if ((row != 3 || col != 0) && tp78_pressed(row, col)) {
                    g_caps_used = true;
                }
            }
        }
    } else if (g_previous[3][0] && !g_caps_used) {
        g_caps_tap_pending = true;
    }

    tp78_transport_process();
    tp78_handle_fn(fn);
    tp78_transport_mode_t mode = tp78_transport_get_mode();
    bool transport_ready = tp78_transport_is_ready();
    if (mode != g_last_mode || transport_ready != g_last_transport_ready) {
        g_last_modifiers = 0xFF;
        (void)memset(g_last_keys, 0xFF, sizeof(g_last_keys));
        g_last_mouse_buttons = 0xFF;
        g_last_consumer = 0xFFFF;
        g_last_mode = mode;
        g_last_transport_ready = transport_ready;
    }

    for (uint8_t row = 0; row < TP78_MATRIX_ROWS; row++) {
        for (uint8_t col = 0; col < TP78_MATRIX_COLS; col++) {
            if (tp78_rising(row, col)) {
                tp78_rgb_key_event(row, col);
            }
            if (!tp78_pressed(row, col) || (row == 3 && col == 0)) {
                continue;
            }
            uint8_t base_key = tp78_keymap_get(0, row, col);
            if (fn && base_key != TP78_KEY_FN) {
                continue;
            }
            tp78_add_key(tp78_keymap_get(caps ? 1 : 0, row, col),
                &modifiers, keys, &mouse_buttons);
        }
    }

    if (g_caps_tap_pending) {
        tp78_add_key(TP78_KEY_CAPS_LOCK, &modifiers, keys, &mouse_buttons);
        g_caps_tap_pending = false;
    }

    bool legacy_reset_combo = modifiers == ((1U << 0) | (1U << 2)) &&
        keys[0] == TP78_KEY_BACKSPACE && keys[1] == 0;
    if (legacy_reset_combo && g_reset_started == 0) {
        g_reset_started = uapi_tcxo_get_ms();
    } else if (!legacy_reset_combo && !fn) {
        g_reset_started = 0;
    } else if (legacy_reset_combo &&
        uapi_tcxo_get_ms() - g_reset_started >= TP78_RESET_HOLD_MS) {
        cpu_utils_reset_chip_with_cause(REBOOT_CAUSE_APPLICATION_SYSRESETREQ);
    }

    if (modifiers != g_last_modifiers || memcmp(keys, g_last_keys, sizeof(keys)) != 0) {
        if (tp78_transport_keyboard_send(modifiers, keys) == 0) {
            g_last_modifiers = modifiers;
            (void)memcpy(g_last_keys, keys, sizeof(keys));
        }
    }
    if (mouse_buttons != g_last_mouse_buttons) {
        if (tp78_transport_mouse_send(mouse_buttons, 0, 0, 0) == 0) {
            g_last_mouse_buttons = mouse_buttons;
        }
    }

    tp78_save_matrix();
}

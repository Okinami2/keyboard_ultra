#include "nv.h"
#include "soc_osal.h"
#include "tp78_ble_keyboard.h"
#include "tp78_sle_keyboard.h"
#include "tp78_transport.h"
#include "tp78_usb_keyboard.h"

static tp78_transport_mode_t g_mode = TP78_TRANSPORT_USB;

static void tp78_transport_set_wireless_active(tp78_transport_mode_t mode, bool active)
{
    if (mode == TP78_TRANSPORT_BLE) {
        tp78_ble_keyboard_set_active(active);
    } else if (mode == TP78_TRANSPORT_SLE) {
        tp78_sle_keyboard_set_active(active);
    }
}

static void tp78_transport_release(tp78_transport_mode_t mode)
{
    uint8_t keys[6] = { 0 };

    switch (mode) {
        case TP78_TRANSPORT_USB:
            (void)tp78_usb_keyboard_send(0, keys);
            (void)tp78_usb_mouse_send(0, 0, 0, 0);
            (void)tp78_usb_consumer_send(0);
            break;
        case TP78_TRANSPORT_BLE:
            (void)tp78_ble_keyboard_send(0, keys);
            (void)tp78_ble_mouse_send(0, 0, 0, 0);
            (void)tp78_ble_consumer_send(0);
            break;
        case TP78_TRANSPORT_SLE:
            (void)tp78_sle_keyboard_send(0, keys);
            (void)tp78_sle_mouse_send(0, 0, 0, 0);
            (void)tp78_sle_consumer_send(0);
            break;
        default:
            break;
    }
}

int32_t tp78_transport_init(void)
{
    uapi_nv_init();
    int32_t result = tp78_usb_keyboard_init();
    if (tp78_ble_keyboard_init() != 0) {
        osal_printk("[tp78] BLE initialization failed\r\n");
    }
    if (tp78_sle_keyboard_init() != 0) {
        osal_printk("[tp78] SLE initialization failed\r\n");
    }
    return result;
}

tp78_transport_mode_t tp78_transport_get_mode(void)
{
    return g_mode;
}

bool tp78_transport_is_ready(void)
{
    switch (g_mode) {
        case TP78_TRANSPORT_USB:
            return true;
        case TP78_TRANSPORT_BLE:
            return tp78_ble_keyboard_is_ready();
        case TP78_TRANSPORT_SLE:
            return tp78_sle_keyboard_is_ready();
        default:
            return false;
    }
}

void tp78_transport_set_mode(tp78_transport_mode_t mode)
{
    static const char *names[] = { "USB", "BLE", "SLE" };

    if (mode > TP78_TRANSPORT_SLE || mode == g_mode) {
        return;
    }
    tp78_transport_release(g_mode);
    tp78_transport_set_wireless_active(g_mode, false);
    g_mode = mode;
    tp78_transport_set_wireless_active(g_mode, true);
    osal_printk("[tp78] transport switched to %s\r\n", names[mode]);
}

void tp78_transport_process(void)
{
    tp78_ble_keyboard_process();
    tp78_sle_keyboard_process();
}

void tp78_transport_start_pairing(tp78_transport_mode_t mode)
{
    if (mode == TP78_TRANSPORT_BLE) {
        tp78_transport_set_mode(TP78_TRANSPORT_BLE);
        tp78_ble_keyboard_start_pairing();
    } else if (mode == TP78_TRANSPORT_SLE) {
        tp78_transport_set_mode(TP78_TRANSPORT_SLE);
        tp78_sle_keyboard_start_pairing();
    }
}

void tp78_transport_select_ble_slot(uint8_t slot)
{
    tp78_transport_set_mode(TP78_TRANSPORT_BLE);
    tp78_ble_keyboard_select_slot(slot);
}

uint8_t tp78_transport_get_ble_slot(void)
{
    return tp78_ble_keyboard_get_slot();
}

int32_t tp78_transport_keyboard_send(uint8_t modifiers, const uint8_t keys[6])
{
    switch (g_mode) {
        case TP78_TRANSPORT_USB:
            return tp78_usb_keyboard_send(modifiers, keys);
        case TP78_TRANSPORT_BLE:
            return tp78_ble_keyboard_send(modifiers, keys);
        case TP78_TRANSPORT_SLE:
            return tp78_sle_keyboard_send(modifiers, keys);
        default:
            return -1;
    }
}

int32_t tp78_transport_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    switch (g_mode) {
        case TP78_TRANSPORT_USB:
            return tp78_usb_mouse_send(buttons, x, y, wheel);
        case TP78_TRANSPORT_BLE:
            return tp78_ble_mouse_send(buttons, x, y, wheel);
        case TP78_TRANSPORT_SLE:
            return tp78_sle_mouse_send(buttons, x, y, wheel);
        default:
            return -1;
    }
}

int32_t tp78_transport_consumer_send(uint16_t usage)
{
    switch (g_mode) {
        case TP78_TRANSPORT_USB:
            return tp78_usb_consumer_send(usage);
        case TP78_TRANSPORT_BLE:
            return tp78_ble_consumer_send(usage);
        case TP78_TRANSPORT_SLE:
            return tp78_sle_consumer_send(usage);
        default:
            return -1;
    }
}

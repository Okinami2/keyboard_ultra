#include <stddef.h>
#include "gadget/f_hid.h"
#include "implementation/usb_init.h"
#include "tp78_usb_keyboard.h"

#define TP78_REPORT_KEYBOARD 1
#define TP78_REPORT_MOUSE 2
#define TP78_REPORT_CONSUMER 3

static int32_t g_hid_index = -1;

static const uint8_t g_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, TP78_REPORT_KEYBOARD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x15, 0x00, 0x25, 0x65,
    0x95, 0x06, 0x75, 0x08, 0x81, 0x00, 0xC0,

    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, TP78_REPORT_MOUSE,
    0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x03, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x05, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,

    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, TP78_REPORT_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
};

int32_t tp78_usb_keyboard_init(void)
{
    static const char manufacturer[] = { 'T', 0, 'P', 0, '7', 0, '8', 0 };
    static const char product[] = {
        'T', 0, 'P', 0, '7', 0, '8', 0, ' ', 0, 'U', 0, 'l', 0, 't', 0, 'r', 0, 'a', 0
    };
    static const char serial[] = { 'T', 0, '7', 0, '8', 0, 'U', 0, '0', 0, '1', 0 };
    struct device_string manufacturer_string = { .str = manufacturer, .len = sizeof(manufacturer) };
    struct device_string product_string = { .str = product, .len = sizeof(product) };
    struct device_string serial_string = { .str = serial, .len = sizeof(serial) };
    struct device_id device_id = {
        .vendor_id = CONFIG_TP78_ULTRA_USB_VID,
        .product_id = CONFIG_TP78_ULTRA_USB_PID,
        .release_num = CONFIG_TP78_ULTRA_USB_RELEASE,
    };

    g_hid_index = hid_add_report_descriptor(g_report_descriptor, sizeof(g_report_descriptor), 0);
    if (g_hid_index < 0) {
        return g_hid_index;
    }
    if (usbd_set_device_info(DEV_HID, &manufacturer_string, &product_string,
        &serial_string, device_id) != 0) {
        return -1;
    }
    return usb_init(DEVICE, DEV_HID);
}

int32_t tp78_usb_keyboard_send(uint8_t modifiers, const uint8_t keys[6])
{
    uint8_t report[9] = {
        TP78_REPORT_KEYBOARD, modifiers, 0,
        keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]
    };
    return g_hid_index < 0 ? -1 : (int32_t)fhid_send_data(g_hid_index, (char *)report, sizeof(report));
}

int32_t tp78_usb_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel)
{
    uint8_t report[5] = {
        TP78_REPORT_MOUSE, buttons, (uint8_t)x, (uint8_t)y, (uint8_t)wheel
    };
    return g_hid_index < 0 ? -1 : (int32_t)fhid_send_data(g_hid_index, (char *)report, sizeof(report));
}

int32_t tp78_usb_consumer_send(uint16_t usage)
{
    uint8_t report[3] = {
        TP78_REPORT_CONSUMER, (uint8_t)usage, (uint8_t)(usage >> 8)
    };
    return g_hid_index < 0 ? -1 : (int32_t)fhid_send_data(g_hid_index, (char *)report, sizeof(report));
}

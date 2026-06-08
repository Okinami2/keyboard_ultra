#include <stddef.h>
#include "gadget/f_hid.h"
#include "implementation/usb_init.h"
#include "tp78_usb_keyboard.h"

#define TP78_USB_REPORT_ID 1

typedef struct {
    uint8_t report_id;
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} tp78_usb_keyboard_report_t;

static int32_t g_hid_index = -1;

static const uint8_t g_keyboard_report_descriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, TP78_USB_REPORT_ID,
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,
    0x15, 0x00,
    0x25, 0x01,
    0x75, 0x01,
    0x95, 0x08,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x08,
    0x81, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x01,
    0x95, 0x06,
    0x75, 0x08,
    0x15, 0x00,
    0x25, 0x65,
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0x65,
    0x81, 0x00,
    0xC0,
};

int32_t tp78_usb_keyboard_init(void)
{
    static const char manufacturer[] = {
        'T', 0, 'P', 0, '7', 0, '8', 0
    };
    static const char product[] = {
        'T', 0, 'P', 0, '7', 0, '8', 0, ' ', 0, 'U', 0, 'l', 0, 't', 0, 'r', 0, 'a', 0
    };
    static const char serial[] = {
        'T', 0, 'P', 0, '7', 0, '8', 0, 'K', 0, 'B', 0
    };
    struct device_string manufacturer_string = {
        .str = manufacturer,
        .len = sizeof(manufacturer),
    };
    struct device_string product_string = {
        .str = product,
        .len = sizeof(product),
    };
    struct device_string serial_string = {
        .str = serial,
        .len = sizeof(serial),
    };
    struct device_id device_id = {
        .vendor_id = CONFIG_TP78_ULTRA_USB_VID,
        .product_id = CONFIG_TP78_ULTRA_USB_PID,
        .release_num = CONFIG_TP78_ULTRA_USB_RELEASE,
    };

    g_hid_index = hid_add_report_descriptor(g_keyboard_report_descriptor,
        sizeof(g_keyboard_report_descriptor), 0);
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
    if (g_hid_index < 0 || keys == NULL) {
        return -1;
    }

    tp78_usb_keyboard_report_t report = {
        .report_id = TP78_USB_REPORT_ID,
        .modifiers = modifiers,
        .reserved = 0,
        .keys = { keys[0], keys[1], keys[2], keys[3], keys[4], keys[5] },
    };
    return fhid_send_data(g_hid_index, (char *)&report, sizeof(report));
}

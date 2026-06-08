#ifndef TP78_USB_KEYBOARD_H
#define TP78_USB_KEYBOARD_H

#include <stdint.h>

int32_t tp78_usb_keyboard_init(void);
int32_t tp78_usb_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);

#endif

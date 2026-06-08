#ifndef TP78_USB_KEYBOARD_H
#define TP78_USB_KEYBOARD_H

#include <stdint.h>

int32_t tp78_usb_keyboard_init(void);
int32_t tp78_usb_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);
int32_t tp78_usb_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
int32_t tp78_usb_consumer_send(uint16_t usage);

#endif

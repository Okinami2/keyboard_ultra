#ifndef TP78_SLE_KEYBOARD_H
#define TP78_SLE_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

int32_t tp78_sle_keyboard_init(void);
bool tp78_sle_keyboard_is_ready(void);
int32_t tp78_sle_keyboard_send(uint8_t modifiers, const uint8_t keys[6]);
int32_t tp78_sle_mouse_send(uint8_t buttons, int8_t x, int8_t y, int8_t wheel);
int32_t tp78_sle_consumer_send(uint16_t usage);

#endif
